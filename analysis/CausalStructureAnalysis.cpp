#include "CausalStructureAnalysis.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace DrosofilaLive
{
namespace
{
bool finite(double v) { return std::isfinite(v); }

bool ciExcludesZero(const StatisticalSummary& s)
{
    return s.uncertaintyAvailable && (s.ci95Low > 0.0 || s.ci95High < 0.0);
}

bool passesGate(const CausalDiscoveryEntry& e, const CausalDiscoveryProvenance& p)
{
    if (!e.validationValid || e.validationDelta.n <= 0)
        return false;

    const QString gate = p.epistasisGateMode.trimmed().toUpper();
    const bool ci = ciExcludesZero(e.validationDelta);
    const bool sign = e.validationDelta.signConsistency + 1e-12 >= p.epistasisMinimumSignConsistency;
    if (gate == QStringLiteral("CI95")) return ci;
    if (gate == QStringLiteral("SIGN")) return sign;
    if (gate == QStringLiteral("BOTH")) return ci && sign;
    return true;
}

QVector<double> averageRanksDescending(const QVector<double>& values)
{
    QVector<int> order(values.size());
    for (int i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&values](int a, int b) {
        return values.at(a) > values.at(b);
    });

    QVector<double> ranks(values.size(), 0.0);
    int i = 0;
    while (i < order.size())
    {
        int j = i + 1;
        const double vi = values.at(order.at(i));
        const double eps = 1e-12 * std::max(1.0, std::fabs(vi));
        while (j < order.size() && std::fabs(values.at(order.at(j)) - vi) <= eps) ++j;
        const double rank = 0.5 * ((i + 1) + j); // ranks are 1-based; j is one-past
        for (int k = i; k < j; ++k) ranks[order.at(k)] = rank;
        i = j;
    }
    return ranks;
}

double percentileFromRank(double rank, int count)
{
    if (count <= 1) return 1.0;
    return 1.0 - (rank - 1.0) / static_cast<double>(count - 1);
}

double pearson(const QVector<double>& x, const QVector<double>& y)
{
    if (x.size() != y.size() || x.size() < 2) return std::numeric_limits<double>::quiet_NaN();
    double mx = 0.0, my = 0.0;
    for (int i = 0; i < x.size(); ++i) { mx += x.at(i); my += y.at(i); }
    mx /= x.size(); my /= y.size();
    double sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (int i = 0; i < x.size(); ++i)
    {
        const double dx = x.at(i) - mx;
        const double dy = y.at(i) - my;
        sxx += dx * dx; syy += dy * dy; sxy += dx * dy;
    }
    if (sxx <= 0.0 || syy <= 0.0) return std::numeric_limits<double>::quiet_NaN();
    return sxy / std::sqrt(sxx * syy);
}

QString esc(const QString& s) { return s.toHtmlEscaped(); }
QString pct(double v) { return QString::number(100.0 * v, 'f', 1) + QStringLiteral("%"); }
}

QString CausalStructureAnalyzer::quadrantLabel(double s, double c)
{
    const bool structuralHigh = s >= 0.5;
    const bool causalHigh = c >= 0.5;
    if (!structuralHigh && causalHigh) return QStringLiteral("CAUSAL SURPRISE");
    if (structuralHigh && !causalHigh) return QStringLiteral("STRUCTURAL OVERESTIMATE");
    if (structuralHigh && causalHigh) return QStringLiteral("EXPECTED STRONG");
    return QStringLiteral("EXPECTED WEAK");
}

CausalStructureAnalysisResult CausalStructureAnalyzer::analyze(const CircuitExploreResult& explorer,
                                                               const CausalDiscoveryResult& discovery)
{
    CausalStructureAnalysisResult out;
    out.createdUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    out.targetRootId = discovery.targetRootId;
    out.targetType = discovery.targetType;
    out.gateMode = discovery.provenance.epistasisGateMode.trimmed().toUpper();
    if (out.gateMode.isEmpty()) out.gateMode = QStringLiteral("NONE");
    out.minimumSignConsistency = discovery.provenance.epistasisMinimumSignConsistency;

    if (!explorer.valid)
    {
        out.error = QStringLiteral("Circuit Explorer result is missing or invalid.");
        return out;
    }
    if (!discovery.valid || discovery.entries.isEmpty())
    {
        out.error = QStringLiteral("Discovery result is missing or invalid.");
        return out;
    }

    QHash<qulonglong, CircuitExploreCandidate> explorerByRoot;
    QHash<qulonglong, int> explorerRankByRoot;
    for (int i = 0; i < explorer.candidates.size(); ++i)
    {
        const qulonglong root = static_cast<qulonglong>(explorer.candidates.at(i).rootId);
        explorerByRoot.insert(root, explorer.candidates.at(i));
        explorerRankByRoot.insert(root, i + 1);
    }

    QVector<int> validatedEntryIndices;
    QVector<double> graphValues;
    QVector<double> causalValues;
    int structurallyUnmatched = 0;
    for (int i = 0; i < discovery.entries.size(); ++i)
    {
        const CausalDiscoveryEntry& d = discovery.entries.at(i);
        if (!d.validationValid || d.validationDelta.n <= 0 || !finite(d.validationDelta.mean))
            continue;
        const qulonglong root = static_cast<qulonglong>(d.candidate.rootId);
        const QHash<qulonglong, CircuitExploreCandidate>::const_iterator it = explorerByRoot.constFind(root);
        if (it == explorerByRoot.constEnd() || !finite(it.value().score))
        {
            ++structurallyUnmatched;
            continue;
        }
        validatedEntryIndices.append(i);
        graphValues.append(it.value().score);
        causalValues.append(std::fabs(d.validationDelta.mean));
    }
    if (structurallyUnmatched > 0)
    {
        out.error = QStringLiteral("Explorer/Discovery state mismatch: %1 validated root(s) are missing from the current structural result.")
            .arg(structurallyUnmatched);
        return out;
    }
    out.validationRows = validatedEntryIndices.size();
    if (validatedEntryIndices.isEmpty())
    {
        out.error = QStringLiteral("No statistically validated Discovery rows are available. Run Validation first.");
        return out;
    }

    const QVector<double> graphRanks = averageRanksDescending(graphValues);
    const QVector<double> causalRanks = averageRanksDescending(causalValues);

    QVector<double> eligibleGraph;
    QVector<double> eligibleCausal;
    for (int k = 0; k < validatedEntryIndices.size(); ++k)
    {
        const CausalDiscoveryEntry& d = discovery.entries.at(validatedEntryIndices.at(k));
        CausalSurpriseEntry e;
        e.rootId = d.candidate.rootId;
        const qulonglong root = static_cast<qulonglong>(e.rootId);
        const QHash<qulonglong, CircuitExploreCandidate>::const_iterator structuralIt = explorerByRoot.constFind(root);
        e.type = structuralIt.value().primaryType.isEmpty() ? d.candidate.type : structuralIt.value().primaryType;
        e.graphRank = explorerRankByRoot.value(root, d.candidate.graphRank);
        e.causalRank = static_cast<int>(std::floor(causalRanks.at(k) + 0.5)); // rank by |validated delta|, not coarse screen
        e.depth = structuralIt.value().depth;
        e.pathCount = structuralIt.value().pathCount;
        e.synapseStrength = structuralIt.value().synapseStrength;
        e.directSynapses = structuralIt.value().directSynapses;
        e.graphScore = structuralIt.value().score;
        e.validationDeltaHz = d.validationDelta.mean;
        e.absoluteCausalEffectHz = std::fabs(d.validationDelta.mean);
        e.ci95LowHz = d.validationDelta.ci95Low;
        e.ci95HighHz = d.validationDelta.ci95High;
        e.signConsistency = d.validationDelta.signConsistency;
        e.validationValid = true;
        e.gatePassed = passesGate(d, discovery.provenance);
        e.structuralPercentile = percentileFromRank(graphRanks.at(k), validatedEntryIndices.size());
        e.causalPercentile = percentileFromRank(causalRanks.at(k), validatedEntryIndices.size());
        e.surpriseScore = e.causalPercentile - e.structuralPercentile;
        e.quadrant = quadrantLabel(e.structuralPercentile, e.causalPercentile);

        if (e.gatePassed)
        {
            ++out.gatePassedRows;
            eligibleGraph.append(e.graphScore);
            eligibleCausal.append(e.absoluteCausalEffectHz);
        }
        out.entries.append(e);
    }

    // Surprise ranks are meaningful only for statistically gate-passed rows.
    QVector<int> eligibleIndices;
    for (int i = 0; i < out.entries.size(); ++i) if (out.entries.at(i).gatePassed) eligibleIndices.append(i);
    std::stable_sort(eligibleIndices.begin(), eligibleIndices.end(), [&out](int a, int b) {
        const CausalSurpriseEntry& ea = out.entries.at(a);
        const CausalSurpriseEntry& eb = out.entries.at(b);
        if (ea.surpriseScore != eb.surpriseScore) return ea.surpriseScore > eb.surpriseScore;
        if (ea.absoluteCausalEffectHz != eb.absoluteCausalEffectHz) return ea.absoluteCausalEffectHz > eb.absoluteCausalEffectHz;
        return ea.graphRank < eb.graphRank;
    });
    for (int rank = 0; rank < eligibleIndices.size(); ++rank)
        out.entries[eligibleIndices.at(rank)].surpriseRank = rank + 1;

    // Present statistically meaningful rows first, ordered by surprise rank,
    // then retain non-passing validated rows for context.
    std::stable_sort(out.entries.begin(), out.entries.end(), [](const CausalSurpriseEntry& a, const CausalSurpriseEntry& b) {
        if (a.gatePassed != b.gatePassed) return a.gatePassed;
        if (a.gatePassed && b.gatePassed && a.surpriseRank != b.surpriseRank) return a.surpriseRank < b.surpriseRank;
        return a.graphRank < b.graphRank;
    });

    if (eligibleGraph.size() >= 2)
    {
        out.pearson = pearson(eligibleGraph, eligibleCausal);
        const QVector<double> rg = averageRanksDescending(eligibleGraph);
        const QVector<double> rc = averageRanksDescending(eligibleCausal);
        out.spearman = pearson(rg, rc);
        out.correlationsAvailable = finite(out.pearson) && finite(out.spearman);
    }

    out.valid = true;
    return out;
}

bool CausalStructureAnalyzer::writeHtmlReport(const QString& filePath,
                                              const CausalStructureAnalysisResult& r,
                                              QString* error)
{
    if (error) error->clear();
    if (!r.valid)
    {
        if (error) *error = QStringLiteral("Causal Surprise result is invalid: %1").arg(r.error);
        return false;
    }

    if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
    {
        if (error) *error = QStringLiteral("Cannot create Causal Surprise report directory.");
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error) *error = QStringLiteral("Cannot create Causal Surprise report: %1").arg(file.errorString());
        return false;
    }
    file.write("\xEF\xBB\xBF", 3);
    QTextStream out(&file);
    out.setCodec("UTF-8");

    out << "<!doctype html><html><head><meta charset=\"utf-8\"><title>drosofilaLive Causal Surprise</title>";
    out << "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:28px;background:#f7f8fa;color:#202124}"
           "h1,h2{margin-bottom:.35em}.card{background:white;border:1px solid #d9dde3;border-radius:9px;padding:14px 16px;margin:12px 0 22px}"
           "table{border-collapse:collapse;width:100%;background:white;margin:12px 0 24px}th,td{border:1px solid #d9dde3;padding:6px 8px;text-align:right}"
           "th{background:#eef1f5}th:first-child,td:first-child,th:nth-child(2),td:nth-child(2),th:last-child,td:last-child{text-align:left}"
           ".surprise{font-weight:700;color:#b45309}.fail{color:#777}.small{font-size:90%;color:#555}"
           ".chart{background:white;border:1px solid #d9dde3;border-radius:9px;padding:12px;margin:12px 0 24px}"
           "svg text{font-family:Segoe UI,Arial,sans-serif;font-size:12px}</style></head><body>";
    out << "<h1>drosofilaLive &mdash; Graph &harr; Causality / Causal Surprise</h1>";
    out << "<div class=\"card\"><b>Target:</b> " << esc(r.targetType) << " [" << static_cast<qulonglong>(r.targetRootId) << "]<br>";
    out << "<b>Created UTC:</b> " << esc(r.createdUtc) << "<br>";
    out << "<b>Validation rows:</b> " << r.validationRows << " &nbsp; <b>Gate-passed:</b> " << r.gatePassedRows << "<br>";
    out << "<b>Gate:</b> " << esc(r.gateMode) << " &nbsp; <b>minimum sign consistency:</b> " << pct(r.minimumSignConsistency) << "<br>";
    if (r.correlationsAvailable)
        out << "<b>Pearson(graph score, |causal effect|):</b> " << QString::number(r.pearson,'f',4)
            << " &nbsp; <b>Spearman:</b> " << QString::number(r.spearman,'f',4) << "<br>";
    else
        out << "<b>Correlation:</b> insufficient gate-passed rows<br>";
    out << "<span class=\"small\">Surprise = causal percentile &minus; structural percentile. Positive values identify candidates whose validated causal importance exceeds their structural rank. Correlations use only rows passing the archived statistical gate.</span></div>";

    // SVG percentile scatter.  Coordinates are deliberately percentile-based:
    // both axes are dimensionless and immediately comparable.
    const int W = 900, H = 650, L = 80, T = 35, PW = 760, PH = 530;
    out << "<h2>Structural percentile vs causal percentile</h2><div class=\"chart\"><svg viewBox=\"0 0 " << W << ' ' << H << "\" width=\"100%\">";
    out << "<rect x=\"" << L << "\" y=\"" << T << "\" width=\"" << PW/2 << "\" height=\"" << PH/2 << "\" fill=\"#fff3e6\"/>";
    out << "<rect x=\"" << L+PW/2 << "\" y=\"" << T << "\" width=\"" << PW/2 << "\" height=\"" << PH/2 << "\" fill=\"#edf6ff\"/>";
    out << "<rect x=\"" << L << "\" y=\"" << T+PH/2 << "\" width=\"" << PW/2 << "\" height=\"" << PH/2 << "\" fill=\"#f4f5f7\"/>";
    out << "<rect x=\"" << L+PW/2 << "\" y=\"" << T+PH/2 << "\" width=\"" << PW/2 << "\" height=\"" << PH/2 << "\" fill=\"#f5effa\"/>";
    out << "<rect x=\"" << L << "\" y=\"" << T << "\" width=\"" << PW << "\" height=\"" << PH << "\" fill=\"none\" stroke=\"#475569\"/>";
    out << "<line x1=\"" << L+PW/2 << "\" x2=\"" << L+PW/2 << "\" y1=\"" << T << "\" y2=\"" << T+PH << "\" stroke=\"#94a3b8\" stroke-dasharray=\"5 5\"/>";
    out << "<line x1=\"" << L << "\" x2=\"" << L+PW << "\" y1=\"" << T+PH/2 << "\" y2=\"" << T+PH/2 << "\" stroke=\"#94a3b8\" stroke-dasharray=\"5 5\"/>";
    out << "<text x=\"" << L+15 << "\" y=\"" << T+20 << "\" fill=\"#b45309\">CAUSAL SURPRISE</text>";
    out << "<text x=\"" << L+PW/2+15 << "\" y=\"" << T+20 << "\" fill=\"#1d4ed8\">EXPECTED STRONG</text>";
    out << "<text x=\"" << L+15 << "\" y=\"" << T+PH-15 << "\" fill=\"#64748b\">EXPECTED WEAK</text>";
    out << "<text x=\"" << L+PW/2+15 << "\" y=\"" << T+PH-15 << "\" fill=\"#7e22ce\">STRUCTURAL OVERESTIMATE</text>";
    out << "<text x=\"" << L+PW/2-80 << "\" y=\"" << H-28 << "\">Structural percentile</text>";
    out << "<text transform=\"translate(22 " << T+PH/2+65 << ") rotate(-90)\">Causal percentile</text>";
    for (int i = 0; i < r.entries.size(); ++i)
    {
        const CausalSurpriseEntry& e = r.entries.at(i);
        const double x = L + e.structuralPercentile * PW;
        const double y = T + (1.0 - e.causalPercentile) * PH;
        QString color = QStringLiteral("#94a3b8");
        if (e.gatePassed)
        {
            if (e.quadrant == QStringLiteral("CAUSAL SURPRISE")) color = QStringLiteral("#d97706");
            else if (e.quadrant == QStringLiteral("EXPECTED STRONG")) color = QStringLiteral("#2563eb");
            else if (e.quadrant == QStringLiteral("STRUCTURAL OVERESTIMATE")) color = QStringLiteral("#9333ea");
            else color = QStringLiteral("#64748b");
        }
        out << "<circle cx=\"" << QString::number(x,'f',2) << "\" cy=\"" << QString::number(y,'f',2)
            << "\" r=\"" << (e.gatePassed ? 6 : 4) << "\" fill=\"" << color << "\"><title>"
            << esc(e.type) << " [" << static_cast<qulonglong>(e.rootId) << "] graph " << QString::number(e.graphScore,'f',4)
            << " causal " << QString::number(e.validationDeltaHz,'f',4) << " Hz surprise " << QString::number(e.surpriseScore,'f',4)
            << "</title></circle>";
    }
    out << "</svg></div>";

    out << "<h2>Ranked candidates</h2><table><tr><th>Neuron</th><th>Root ID</th><th>Surprise rank</th><th>Graph rank</th><th>Depth</th><th>Graph score</th><th>Paths</th><th>Synaptic strength</th><th>Validated &Delta; Hz</th><th>CI95 low</th><th>CI95 high</th><th>Sign %</th><th>Structural %ile</th><th>Causal %ile</th><th>Surprise</th><th>Quadrant</th></tr>";
    for (int i = 0; i < r.entries.size(); ++i)
    {
        const CausalSurpriseEntry& e = r.entries.at(i);
        out << "<tr" << (e.quadrant == QStringLiteral("CAUSAL SURPRISE") && e.gatePassed ? " class=\"surprise\"" : (!e.gatePassed ? " class=\"fail\"" : "")) << "><td>"
            << esc(e.type) << "</td><td>" << static_cast<qulonglong>(e.rootId) << "</td><td>" << (e.surpriseRank > 0 ? QString::number(e.surpriseRank) : QStringLiteral("-"))
            << "</td><td>" << e.graphRank << "</td><td>" << e.depth << "</td><td>" << QString::number(e.graphScore,'f',6)
            << "</td><td>" << static_cast<qulonglong>(e.pathCount) << "</td><td>" << static_cast<qulonglong>(e.synapseStrength)
            << "</td><td>" << QString::number(e.validationDeltaHz,'f',6) << "</td><td>" << QString::number(e.ci95LowHz,'f',6)
            << "</td><td>" << QString::number(e.ci95HighHz,'f',6) << "</td><td>" << QString::number(100.0*e.signConsistency,'f',1)
            << "</td><td>" << QString::number(100.0*e.structuralPercentile,'f',1) << "</td><td>" << QString::number(100.0*e.causalPercentile,'f',1)
            << "</td><td>" << QString::number(e.surpriseScore,'f',4) << "</td><td>" << esc(e.gatePassed ? e.quadrant : QStringLiteral("NOT GATE-PASSED")) << "</td></tr>";
    }
    out << "</table></body></html>";
    out.flush();
    if (!file.commit())
    {
        if (error) *error = QStringLiteral("Cannot commit Causal Surprise report: %1").arg(file.errorString());
        return false;
    }
    return true;
}

}
