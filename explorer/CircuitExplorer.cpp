#include "CircuitExplorer.h"

#include "../core/Connectome.h"

#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace DrosofilaLive
{
namespace
{
quint64 saturatingAdd(quint64 a, quint64 b)
{
    const quint64 maxValue = std::numeric_limits<quint64>::max();
    if (maxValue - a < b)
        return maxValue;
    return a + b;
}

quint64 edgeKey(quint32 source, quint32 target)
{
    return (static_cast<quint64>(source) << 32) | static_cast<quint64>(target);
}

struct CandidateAccumulator
{
    int depth = std::numeric_limits<int>::max();
    bool upstream = false;
    bool downstream = false;
    quint64 pathCount = 0;
    quint64 strength = 0;
    quint32 directSynapses = 0;
    double rootFraction = 0.0;
};
}

CircuitExplorer::CircuitExplorer(const Connectome* connectome, QObject* parent)
    : QObject(parent)
    , connectome_(connectome)
    , reverseReady_(false)
    , typeIndexReady_(false)
{
}

void CircuitExplorer::invalidateCache()
{
    reverseOffsets_.clear();
    reverseSources_.clear();
    reverseWeights_.clear();
    primaryTypeIndex_.clear();
    reverseReady_ = false;
    typeIndexReady_ = false;
}

void CircuitExplorer::buildTypeIndex()
{
    if (typeIndexReady_ || !connectome_ || !connectome_->isLoaded())
        return;

    primaryTypeIndex_.clear();
    const quint32 n = connectome_->neuronCount();
    for (quint32 i = 0; i < n; ++i)
    {
        const Connectome::Annotation a = connectome_->annotation(i);
        const QString key = a.primaryType.trimmed().toLower();
        if (!key.isEmpty())
            primaryTypeIndex_[key].append(i);
    }
    typeIndexReady_ = true;
}

bool CircuitExplorer::resolveRoot(const QString& text,
                                  quint32* index,
                                  std::uint64_t* rootId,
                                  QString* type,
                                  QString* error)
{
    if (!connectome_ || !connectome_->isLoaded())
    {
        if (error) *error = QStringLiteral("Connectome not loaded.");
        return false;
    }

    const QString query = text.trimmed();
    if (query.isEmpty())
    {
        if (error) *error = QStringLiteral("Root query is empty.");
        return false;
    }

    bool numericOk = false;
    const qulonglong numericRoot = query.toULongLong(&numericOk);
    if (numericOk)
    {
        quint32 idx = 0;
        if (!connectome_->indexFromRootId(static_cast<quint64>(numericRoot), &idx))
        {
            if (error) *error = QStringLiteral("Root ID %1 is not present in the loaded connectome.").arg(query);
            return false;
        }
        if (index) *index = idx;
        if (rootId) *rootId = static_cast<std::uint64_t>(numericRoot);
        if (type)
        {
            const QString primary = connectome_->annotation(idx).primaryType.trimmed();
            *type = primary.isEmpty() ? QStringLiteral("unannotated") : primary;
        }
        return true;
    }

    buildTypeIndex();
    const QVector<quint32> matches = primaryTypeIndex_.value(query.toLower());
    if (matches.isEmpty())
    {
        if (error) *error = QStringLiteral("No primaryType exactly matches '%1'. Use an exact FlyWire root ID if needed.").arg(query);
        return false;
    }
    if (matches.size() != 1)
    {
        QStringList ids;
        const int shown = std::min(12, matches.size());
        for (int i = 0; i < shown; ++i)
            ids << QString::number(static_cast<qulonglong>(connectome_->rootId(matches.at(i))));
        QString suffix;
        if (matches.size() > shown)
            suffix = QStringLiteral(" ... (+%1 more)").arg(matches.size() - shown);
        if (error)
            *error = QStringLiteral("primaryType '%1' is ambiguous (%2 neurons). Use an exact root ID. Candidates: %3%4")
                .arg(query).arg(matches.size()).arg(ids.join(QStringLiteral(", "))).arg(suffix);
        return false;
    }

    const quint32 idx = matches.first();
    if (index) *index = idx;
    if (rootId) *rootId = connectome_->rootId(idx);
    if (type) *type = connectome_->annotation(idx).primaryType.trimmed();
    return true;
}

bool CircuitExplorer::buildReverseIndex(bool* builtNow, QString* error)
{
    if (builtNow) *builtNow = false;
    if (reverseReady_)
        return true;
    if (!connectome_ || !connectome_->isLoaded())
    {
        if (error) *error = QStringLiteral("Connectome not loaded.");
        return false;
    }

    const quint32 n = connectome_->neuronCount();
    const quint64 eCount = connectome_->edgeCount();
    if (eCount > static_cast<quint64>(std::numeric_limits<int>::max()))
    {
        if (error) *error = QStringLiteral("Reverse index exceeds QVector addressable size in this build.");
        return false;
    }

    emit progress(2, QStringLiteral("Building upstream index: counting incoming edges"));
    reverseOffsets_.fill(0, static_cast<int>(n + 1));
    const quint64 stride = std::max<quint64>(1, eCount / 20);
    for (quint64 e = 0; e < eCount; ++e)
    {
        const quint32 target = connectome_->edgeTarget(e);
        if (target < n)
            ++reverseOffsets_[static_cast<int>(target + 1)];
        if ((e % stride) == 0)
            emit progress(2 + static_cast<int>((e * 28) / std::max<quint64>(1, eCount)),
                          QStringLiteral("Building upstream index: count %1/%2 edges")
                              .arg(e).arg(eCount));
    }

    emit progress(31, QStringLiteral("Building upstream index: prefix sum"));
    for (quint32 i = 1; i <= n; ++i)
        reverseOffsets_[static_cast<int>(i)] += reverseOffsets_.at(static_cast<int>(i - 1));

    reverseSources_.resize(static_cast<int>(eCount));
    reverseWeights_.resize(static_cast<int>(eCount));
    QVector<quint32> cursor = reverseOffsets_;

    emit progress(35, QStringLiteral("Building upstream index: filling source lists"));
    quint64 processed = 0;
    for (quint32 source = 0; source < n; ++source)
    {
        const quint64 begin = connectome_->edgeBegin(source);
        const quint64 end = connectome_->edgeEnd(source);
        for (quint64 e = begin; e < end; ++e)
        {
            const quint32 target = connectome_->edgeTarget(e);
            if (target >= n)
                continue;
            const quint32 pos = cursor[static_cast<int>(target)]++;
            reverseSources_[static_cast<int>(pos)] = source;
            reverseWeights_[static_cast<int>(pos)] = connectome_->edgeWeight(e);
            ++processed;
            if ((processed % stride) == 0)
                emit progress(35 + static_cast<int>((processed * 50) / std::max<quint64>(1, eCount)),
                              QStringLiteral("Building upstream index: fill %1/%2 edges")
                                  .arg(processed).arg(eCount));
        }
    }

    reverseReady_ = true;
    if (builtNow) *builtNow = true;
    emit progress(86, QStringLiteral("Upstream index ready"));
    return true;
}

void CircuitExplorer::explore(const CircuitExploreParameters& requested)
{
    CircuitExploreResult result;
    result.rootQuery = requested.rootText;
    result.direction = requested.direction;
    result.maxDepth = std::max(1, std::min(6, requested.maxDepth));
    result.minimumSynapses = std::max(1, requested.minimumSynapses);
    result.maximumNeurons = std::max(1, std::min(1000, requested.maximumNeurons));

    if (!connectome_ || !connectome_->isLoaded())
    {
        result.error = QStringLiteral("Connectome not loaded.");
        emit finished(result);
        return;
    }

    emit progress(0, QStringLiteral("Resolving root neuron"));
    QString resolveError;
    if (!resolveRoot(requested.rootText, &result.rootIndex, &result.rootId, &result.rootType, &resolveError))
    {
        result.error = resolveError;
        emit finished(result);
        return;
    }

    if (result.direction == ExploreUpstream || result.direction == ExploreBoth)
    {
        bool builtNow = false;
        QString reverseError;
        if (!buildReverseIndex(&builtNow, &reverseError))
        {
            result.error = reverseError;
            emit finished(result);
            return;
        }
        result.reverseIndexBuiltNow = builtNow;
    }

    const quint32 n = connectome_->neuronCount();
    const int hardCap = std::min(20000, std::max(1000, result.maximumNeurons * 50));
    QHash<quint32, CandidateAccumulator> merged;
    QHash<quint64, CircuitExploreEdge> mergedEdges;

    const auto traverse = [&](bool upstream)
    {
        QVector<int> distance(static_cast<int>(n), -1);
        QVector<quint64> paths(static_cast<int>(n), 0);
        QVector<quint32> frontier;
        frontier.append(result.rootIndex);
        distance[static_cast<int>(result.rootIndex)] = 0;
        paths[static_cast<int>(result.rootIndex)] = 1;

        for (int depth = 1; depth <= result.maxDepth && !frontier.isEmpty(); ++depth)
        {
            QVector<quint32> next;
            QSet<quint32> nextSet;
            for (int fi = 0; fi < frontier.size(); ++fi)
            {
                const quint32 current = frontier.at(fi);
                const quint64 parentPaths = paths.at(static_cast<int>(current));
                if (upstream)
                {
                    const quint32 begin = reverseOffsets_.at(static_cast<int>(current));
                    const quint32 end = reverseOffsets_.at(static_cast<int>(current + 1));
                    for (quint32 pos = begin; pos < end; ++pos)
                    {
                        ++result.exploredEdges;
                        const qint16 weight = reverseWeights_.at(static_cast<int>(pos));
                        const quint32 magnitude = static_cast<quint32>(std::abs(static_cast<int>(weight)));
                        if (magnitude < static_cast<quint32>(result.minimumSynapses))
                            continue;
                        const quint32 candidate = reverseSources_.at(static_cast<int>(pos));
                        if (candidate == result.rootIndex)
                            continue;

                        int& d = distance[static_cast<int>(candidate)];
                        if (d < 0)
                        {
                            d = depth;
                            paths[static_cast<int>(candidate)] = parentPaths;
                            if (!nextSet.contains(candidate))
                            {
                                nextSet.insert(candidate);
                                next.append(candidate);
                            }
                        }
                        else if (d == depth)
                        {
                            paths[static_cast<int>(candidate)] = saturatingAdd(paths.at(static_cast<int>(candidate)), parentPaths);
                        }
                        else
                        {
                            continue;
                        }

                        CandidateAccumulator& a = merged[candidate];
                        a.depth = std::min(a.depth, depth);
                        a.upstream = true;
                        a.pathCount = saturatingAdd(a.pathCount, parentPaths);
                        a.strength = saturatingAdd(a.strength, magnitude);
                        if (depth == 1)
                        {
                            a.directSynapses = std::max(a.directSynapses, magnitude);
                            const quint32 denom = std::max<quint32>(1, connectome_->synapsesIn(result.rootIndex));
                            a.rootFraction = std::max(a.rootFraction, static_cast<double>(magnitude) / static_cast<double>(denom));
                        }

                        CircuitExploreEdge edge;
                        edge.sourceRootId = connectome_->rootId(candidate);
                        edge.targetRootId = connectome_->rootId(current);
                        edge.weight = weight;
                        mergedEdges.insert(edgeKey(candidate, current), edge);
                    }
                }
                else
                {
                    const quint64 begin = connectome_->edgeBegin(current);
                    const quint64 end = connectome_->edgeEnd(current);
                    for (quint64 e = begin; e < end; ++e)
                    {
                        ++result.exploredEdges;
                        const qint16 weight = connectome_->edgeWeight(e);
                        const quint32 magnitude = static_cast<quint32>(std::abs(static_cast<int>(weight)));
                        if (magnitude < static_cast<quint32>(result.minimumSynapses))
                            continue;
                        const quint32 candidate = connectome_->edgeTarget(e);
                        if (candidate == result.rootIndex || candidate >= n)
                            continue;

                        int& d = distance[static_cast<int>(candidate)];
                        if (d < 0)
                        {
                            d = depth;
                            paths[static_cast<int>(candidate)] = parentPaths;
                            if (!nextSet.contains(candidate))
                            {
                                nextSet.insert(candidate);
                                next.append(candidate);
                            }
                        }
                        else if (d == depth)
                        {
                            paths[static_cast<int>(candidate)] = saturatingAdd(paths.at(static_cast<int>(candidate)), parentPaths);
                        }
                        else
                        {
                            continue;
                        }

                        CandidateAccumulator& a = merged[candidate];
                        a.depth = std::min(a.depth, depth);
                        a.downstream = true;
                        a.pathCount = saturatingAdd(a.pathCount, parentPaths);
                        a.strength = saturatingAdd(a.strength, magnitude);
                        if (depth == 1)
                        {
                            a.directSynapses = std::max(a.directSynapses, magnitude);
                            const quint32 denom = std::max<quint32>(1, connectome_->synapsesOut(result.rootIndex));
                            a.rootFraction = std::max(a.rootFraction, static_cast<double>(magnitude) / static_cast<double>(denom));
                        }

                        CircuitExploreEdge edge;
                        edge.sourceRootId = connectome_->rootId(current);
                        edge.targetRootId = connectome_->rootId(candidate);
                        edge.weight = weight;
                        mergedEdges.insert(edgeKey(current, candidate), edge);
                    }
                }

                if (merged.size() >= hardCap)
                {
                    result.truncated = true;
                    break;
                }
            }
            frontier = next;
            const int base = result.direction == ExploreBoth ? (upstream ? 87 : 93) : 87;
            const int span = result.direction == ExploreBoth ? 5 : 10;
            emit progress(std::min(98, base + (span * depth) / std::max(1, result.maxDepth)),
                          QStringLiteral("Exploring %1 depth %2/%3: %4 candidates")
                              .arg(upstream ? QStringLiteral("upstream") : QStringLiteral("downstream"))
                              .arg(depth).arg(result.maxDepth).arg(merged.size()));
            if (result.truncated)
                break;
        }
    };

    if (result.direction == ExploreUpstream)
        traverse(true);
    else if (result.direction == ExploreDownstream)
        traverse(false);
    else
    {
        traverse(true);
        if (!result.truncated)
            traverse(false);
    }

    result.candidates.reserve(merged.size());
    double maxRawScore = 0.0;
    QHash<quint32, double> rawScores;
    for (QHash<quint32, CandidateAccumulator>::const_iterator it = merged.constBegin(); it != merged.constEnd(); ++it)
    {
        const CandidateAccumulator& a = it.value();
        const int depth = std::max(1, a.depth);
        const double pathFactor = 1.0 + std::log1p(static_cast<double>(std::min<quint64>(a.pathCount, 1000000ULL)));
        const double raw = (static_cast<double>(a.strength) * pathFactor) / static_cast<double>(depth);
        rawScores.insert(it.key(), raw);
        maxRawScore = std::max(maxRawScore, raw);
    }

    for (QHash<quint32, CandidateAccumulator>::const_iterator it = merged.constBegin(); it != merged.constEnd(); ++it)
    {
        const quint32 idx = it.key();
        const CandidateAccumulator& a = it.value();
        CircuitExploreCandidate c;
        c.index = idx;
        c.rootId = connectome_->rootId(idx);
        const Connectome::Annotation annotation = connectome_->annotation(idx);
        c.primaryType = annotation.primaryType.trimmed();
        c.additionalTypes = annotation.additionalTypes.trimmed();
        c.sign = connectome_->sign(idx);
        c.depth = a.depth;
        c.upstream = a.upstream;
        c.downstream = a.downstream;
        c.pathCount = a.pathCount;
        c.synapseStrength = a.strength;
        c.directSynapses = a.directSynapses;
        c.rootFraction = a.rootFraction;
        c.score = maxRawScore > 0.0 ? rawScores.value(idx) / maxRawScore : 0.0;
        result.candidates.append(c);
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const CircuitExploreCandidate& a, const CircuitExploreCandidate& b)
    {
        if (a.score != b.score) return a.score > b.score;
        if (a.depth != b.depth) return a.depth < b.depth;
        if (a.directSynapses != b.directSynapses) return a.directSynapses > b.directSynapses;
        return a.rootId < b.rootId;
    });

    if (result.candidates.size() > result.maximumNeurons)
        result.candidates.resize(result.maximumNeurons);

    QSet<quint64> visibleRoots;
    visibleRoots.insert(result.rootId);
    for (int i = 0; i < result.candidates.size(); ++i)
        visibleRoots.insert(result.candidates.at(i).rootId);

    for (QHash<quint64, CircuitExploreEdge>::const_iterator it = mergedEdges.constBegin(); it != mergedEdges.constEnd(); ++it)
    {
        const CircuitExploreEdge& edge = it.value();
        if (visibleRoots.contains(edge.sourceRootId) && visibleRoots.contains(edge.targetRootId))
            result.edges.append(edge);
    }

    result.valid = true;
    emit progress(100, QStringLiteral("Circuit exploration complete"));
    emit finished(result);
}

}
