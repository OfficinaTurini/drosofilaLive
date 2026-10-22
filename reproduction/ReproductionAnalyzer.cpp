#include "ReproductionAnalyzer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace DrosofilaLive
{
namespace
{
QString html(const QString& in)
{
    QString s = in;
    s.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    s.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    s.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    s.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return s;
}

void addMetric(QVector<ReproductionMetric>* out,
               const QString& phase,
               const QString& subject,
               const QString& metric,
               double archived,
               double reproduced,
               double tolerance)
{
    if (!out) return;
    ReproductionMetric m;
    m.phase = phase;
    m.subject = subject;
    m.metric = metric;
    m.archived = archived;
    m.reproduced = reproduced;
    m.delta = reproduced - archived;
    m.tolerance = tolerance;
    m.match = std::fabs(m.delta) <= tolerance;
    out->append(m);
}

const CircuitExploreCandidate* explorerCandidate(const CircuitExploreResult& r, std::uint64_t rootId)
{
    for (int i = 0; i < r.candidates.size(); ++i)
        if (r.candidates.at(i).rootId == rootId)
            return &r.candidates.at(i);
    return 0;
}

const CausalDiscoveryEntry* discoveryEntry(const CausalDiscoveryResult& r, std::uint64_t rootId)
{
    for (int i = 0; i < r.entries.size(); ++i)
        if (r.entries.at(i).candidate.rootId == rootId)
            return &r.entries.at(i);
    return 0;
}

const CausalDiscoveryEpistasisEntry* epistasisEntry(const CausalDiscoveryResult& r,
                                                     std::uint64_t rootA,
                                                     std::uint64_t rootB)
{
    for (int i = 0; i < r.epistasisEntries.size(); ++i)
    {
        const CausalDiscoveryEpistasisEntry& e = r.epistasisEntries.at(i);
        if ((e.rootA == rootA && e.rootB == rootB) || (e.rootA == rootB && e.rootB == rootA))
            return &e;
    }
    return 0;
}

QString rootSubject(const QString& type, std::uint64_t rootId)
{
    return QStringLiteral("%1 [%2]")
        .arg(type.isEmpty() ? QStringLiteral("unannotated") : type)
        .arg(static_cast<qulonglong>(rootId));
}
}

int ReproductionAnalysis::mismatchCount() const
{
    int n = 0;
    for (int i = 0; i < metrics.size(); ++i)
        if (!metrics.at(i).match) ++n;
    return n;
}

int ReproductionAnalysis::requiredPreflightMismatchCount() const
{
    int n = 0;
    for (int i = 0; i < preflight.size(); ++i)
        if (preflight.at(i).required && !preflight.at(i).match) ++n;
    return n;
}

QVector<ReproductionMetric> ReproductionAnalyzer::compareExplorer(const CircuitExploreResult& archived,
                                                                   const CircuitExploreResult& reproduced,
                                                                   double tolerance)
{
    QVector<ReproductionMetric> out;
    addMetric(&out, QStringLiteral("Explorer"), QStringLiteral("query"), QStringLiteral("candidate count"),
              archived.candidates.size(), reproduced.candidates.size(), 0.0);
    addMetric(&out, QStringLiteral("Explorer"), QStringLiteral("query"), QStringLiteral("displayed edge count"),
              archived.edges.size(), reproduced.edges.size(), 0.0);

    for (int i = 0; i < archived.candidates.size(); ++i)
    {
        const CircuitExploreCandidate& a = archived.candidates.at(i);
        const CircuitExploreCandidate* b = explorerCandidate(reproduced, a.rootId);
        const QString subject = rootSubject(a.primaryType, a.rootId);
        addMetric(&out, QStringLiteral("Explorer"), subject, QStringLiteral("present"), 1.0, b ? 1.0 : 0.0, 0.0);
        if (!b) continue;
        addMetric(&out, QStringLiteral("Explorer"), subject, QStringLiteral("depth"), a.depth, b->depth, 0.0);
        addMetric(&out, QStringLiteral("Explorer"), subject, QStringLiteral("synapse strength"),
                  static_cast<double>(a.synapseStrength), static_cast<double>(b->synapseStrength), 0.0);
        addMetric(&out, QStringLiteral("Explorer"), subject, QStringLiteral("path count"),
                  static_cast<double>(a.pathCount), static_cast<double>(b->pathCount), 0.0);
        addMetric(&out, QStringLiteral("Explorer"), subject, QStringLiteral("graph score"), a.score, b->score, tolerance);
    }
    return out;
}

QVector<ReproductionMetric> ReproductionAnalyzer::compareDiscovery(const CausalDiscoveryResult& archived,
                                                                    const CausalDiscoveryResult& reproduced,
                                                                    double tolerance)
{
    QVector<ReproductionMetric> out;
    addMetric(&out, QStringLiteral("Screen"), QStringLiteral("protocol"), QStringLiteral("candidate count"),
              archived.entries.size(), reproduced.entries.size(), 0.0);

    for (int i = 0; i < archived.entries.size(); ++i)
    {
        const CausalDiscoveryEntry& a = archived.entries.at(i);
        const CausalDiscoveryEntry* b = discoveryEntry(reproduced, a.candidate.rootId);
        const QString subject = rootSubject(a.candidate.type, a.candidate.rootId);
        addMetric(&out, QStringLiteral("Screen"), subject, QStringLiteral("present"), 1.0, b ? 1.0 : 0.0, 0.0);
        if (!b) continue;
        addMetric(&out, QStringLiteral("Screen"), subject, QStringLiteral("causal rank"), a.causalRank, b->causalRank, 0.0);
        addMetric(&out, QStringLiteral("Screen"), subject, QStringLiteral("mean delta Hz"), a.screenDelta.mean, b->screenDelta.mean, tolerance);
        addMetric(&out, QStringLiteral("Screen"), subject, QStringLiteral("std delta Hz"), a.screenDelta.stddev, b->screenDelta.stddev, tolerance);
        addMetric(&out, QStringLiteral("Screen"), subject, QStringLiteral("sign consistency"), a.screenDelta.signConsistency, b->screenDelta.signConsistency, tolerance);

        if (a.validationAttempted || a.validationDelta.n > 0)
        {
            addMetric(&out, QStringLiteral("Validation"), subject, QStringLiteral("attempted"), 1.0,
                      (b->validationAttempted || b->validationDelta.n > 0) ? 1.0 : 0.0, 0.0);
            addMetric(&out, QStringLiteral("Validation"), subject, QStringLiteral("mean delta Hz"), a.validationDelta.mean, b->validationDelta.mean, tolerance);
            addMetric(&out, QStringLiteral("Validation"), subject, QStringLiteral("std delta Hz"), a.validationDelta.stddev, b->validationDelta.stddev, tolerance);
            addMetric(&out, QStringLiteral("Validation"), subject, QStringLiteral("CI95 low"), a.validationDelta.ci95Low, b->validationDelta.ci95Low, tolerance);
            addMetric(&out, QStringLiteral("Validation"), subject, QStringLiteral("CI95 high"), a.validationDelta.ci95High, b->validationDelta.ci95High, tolerance);
            addMetric(&out, QStringLiteral("Validation"), subject, QStringLiteral("sign consistency"), a.validationDelta.signConsistency, b->validationDelta.signConsistency, tolerance);
        }
    }

    addMetric(&out, QStringLiteral("Epistasis"), QStringLiteral("protocol"), QStringLiteral("pair count"),
              archived.epistasisEntries.size(), reproduced.epistasisEntries.size(), 0.0);
    for (int i = 0; i < archived.epistasisEntries.size(); ++i)
    {
        const CausalDiscoveryEpistasisEntry& a = archived.epistasisEntries.at(i);
        const CausalDiscoveryEpistasisEntry* b = epistasisEntry(reproduced, a.rootA, a.rootB);
        const QString subject = QStringLiteral("%1 x %2")
            .arg(rootSubject(a.typeA, a.rootA), rootSubject(a.typeB, a.rootB));
        addMetric(&out, QStringLiteral("Epistasis"), subject, QStringLiteral("present"), 1.0, b ? 1.0 : 0.0, 0.0);
        if (!b) continue;
        addMetric(&out, QStringLiteral("Epistasis"), subject, QStringLiteral("interaction rank"), a.interactionRank, b->interactionRank, 0.0);
        addMetric(&out, QStringLiteral("Epistasis"), subject, QStringLiteral("mean interaction Hz"), a.interaction.mean, b->interaction.mean, tolerance);
        addMetric(&out, QStringLiteral("Epistasis"), subject, QStringLiteral("std interaction Hz"), a.interaction.stddev, b->interaction.stddev, tolerance);
        addMetric(&out, QStringLiteral("Epistasis"), subject, QStringLiteral("CI95 low"), a.interaction.ci95Low, b->interaction.ci95Low, tolerance);
        addMetric(&out, QStringLiteral("Epistasis"), subject, QStringLiteral("CI95 high"), a.interaction.ci95High, b->interaction.ci95High, tolerance);
        addMetric(&out, QStringLiteral("Epistasis"), subject, QStringLiteral("sign consistency"), a.interaction.signConsistency, b->interaction.signConsistency, tolerance);
    }
    return out;
}

bool ReproductionAnalyzer::writeHtmlReport(const QString& filePath,
                                           const ExperimentArchiveBundle& archivedBundle,
                                           const ReproductionAnalysis& analysis,
                                           QString* error)
{
    QFileInfo info(filePath);
    QDir().mkpath(info.absolutePath());
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error) *error = QStringLiteral("Cannot write reproduction report: %1").arg(filePath);
        return false;
    }

    QTextStream s(&f);
    s.setCodec("UTF-8");
    s.setGenerateByteOrderMark(true);
    const QString status = !analysis.valid
        ? QStringLiteral("INCOMPLETE")
        : (analysis.numericalPass && analysis.environmentExact
            ? QStringLiteral("PASS")
            : (analysis.numericalPass ? QStringLiteral("NUMERIC PASS / ENVIRONMENT DIFFERENT") : QStringLiteral("FAIL")));
    const QString badge = !analysis.valid ? QStringLiteral("warn")
        : (analysis.numericalPass && analysis.environmentExact ? QStringLiteral("ok")
            : (analysis.numericalPass ? QStringLiteral("warn") : QStringLiteral("bad")));

    s << "<!doctype html><html><head><meta charset='utf-8'><title>Drosofila Live Reproduction Report</title>"
         "<style>body{font-family:Segoe UI,Arial,sans-serif;background:#f5f7fa;color:#20242a;margin:32px}"
         "h1,h2{color:#26384a}.card{background:white;border:1px solid #d9e0e7;border-radius:10px;padding:18px;margin:14px 0}"
         "table{border-collapse:collapse;width:100%;font-size:13px}th,td{border-bottom:1px solid #e5e9ee;padding:7px;text-align:left}"
         "th{background:#eef3f7}.ok{color:#176b3a;font-weight:700}.warn{color:#9a6610;font-weight:700}.bad{color:#a32929;font-weight:700}"
         ".mono{font-family:Consolas,monospace}</style></head><body>";
    s << "<h1>Drosofila Live &mdash; Reproduction Report</h1>";
    s << "<div class='card'><b>Status:</b> <span class='" << badge << "'>" << html(status) << "</span><br>"
      << "<b>Archive:</b> " << html(archivedBundle.sourcePath) << "<br>"
      << "<b>Archive application:</b> " << html(archivedBundle.applicationVersion) << "<br>"
      << "<b>Connectome:</b> " << html(archivedBundle.connectomeVersion) << "<br>"
      << "<b>Started UTC:</b> " << html(analysis.startedUtc) << "<br>"
      << "<b>Finished UTC:</b> " << html(analysis.finishedUtc) << "<br>"
      << "<b>Required preflight mismatches:</b> " << analysis.requiredPreflightMismatchCount() << "<br>"
      << "<b>Numeric mismatches:</b> " << analysis.mismatchCount();
    if (!analysis.error.isEmpty()) s << "<br><b>Run note:</b> " << html(analysis.error);
    s << "</div>";

    s << "<h2>Preflight</h2><div class='card'><table><tr><th>Category</th><th>Item</th><th>Archived</th><th>Current</th><th>Result</th></tr>";
    for (int i = 0; i < analysis.preflight.size(); ++i)
    {
        const ReproductionCheck& c = analysis.preflight.at(i);
        s << "<tr><td>" << html(c.category) << "</td><td>" << html(c.item) << "</td><td class='mono'>" << html(c.archived)
          << "</td><td class='mono'>" << html(c.current) << "</td><td class='" << (c.match ? "ok" : (c.required ? "bad" : "warn")) << "'>"
          << (c.match ? "MATCH" : (c.required ? "MISMATCH" : "INFO")) << "</td></tr>";
    }
    s << "</table></div>";

    s << "<h2>Archived vs reproduced</h2><div class='card'><table><tr><th>Phase</th><th>Subject</th><th>Metric</th><th>Archived</th><th>Reproduced</th><th>Delta</th><th>Tolerance</th><th>Result</th></tr>";
    for (int i = 0; i < analysis.metrics.size(); ++i)
    {
        const ReproductionMetric& m = analysis.metrics.at(i);
        s << "<tr><td>" << html(m.phase) << "</td><td>" << html(m.subject) << "</td><td>" << html(m.metric)
          << "</td><td>" << QString::number(m.archived, 'g', 15) << "</td><td>" << QString::number(m.reproduced, 'g', 15)
          << "</td><td>" << QString::number(m.delta, 'g', 15) << "</td><td>" << QString::number(m.tolerance, 'g', 6)
          << "</td><td class='" << (m.match ? "ok" : "bad") << "'>" << (m.match ? "PASS" : "FAIL") << "</td></tr>";
    }
    s << "</table></div>";
    s << "<div class='card'><b>Interpretation:</b> PASS means that the stored scientific protocol reproduced the archived Explorer/Discovery metrics within the stated tolerances. "
         "A numeric PASS with a non-exact environment is reported separately and must not be described as an exact reproduction.<br><br>"
         "<b>Scope of exact preflight:</b> .dro v3 records the declared connectome version and validates causal anchor IDs/labels, but does not cryptographically hash every byte of the complete connectome dataset.</div>";
    s << "</body></html>";
    return true;
}
}
