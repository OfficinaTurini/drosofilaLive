#pragma once

#include <QObject>
#include <QHash>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <cstdint>

class Connectome;

namespace DrosofilaLive
{

enum CircuitExploreDirection
{
    ExploreUpstream = 0,
    ExploreDownstream = 1,
    ExploreBoth = 2
};

struct CircuitExploreParameters
{
    QString rootText;
    int direction = ExploreUpstream;
    int maxDepth = 2;
    int minimumSynapses = 10;
    int maximumNeurons = 100;
};

struct CircuitExploreCandidate
{
    std::uint64_t rootId = 0;
    quint32 index = 0;
    QString primaryType;
    QString additionalTypes;
    qint8 sign = 0;
    int depth = 0;
    bool upstream = false;
    bool downstream = false;
    quint64 pathCount = 0;
    quint64 synapseStrength = 0;
    quint32 directSynapses = 0;
    double rootFraction = 0.0;
    double score = 0.0;
};

struct CircuitExploreEdge
{
    std::uint64_t sourceRootId = 0;
    std::uint64_t targetRootId = 0;
    qint16 weight = 0;
};

struct CircuitExploreResult
{
    bool valid = false;
    QString error;
    QString rootQuery;
    std::uint64_t rootId = 0;
    quint32 rootIndex = 0;
    QString rootType;
    int direction = ExploreUpstream;
    int maxDepth = 0;
    int minimumSynapses = 0;
    int maximumNeurons = 0;
    bool reverseIndexBuiltNow = false;
    bool truncated = false;
    quint64 exploredEdges = 0;
    QVector<CircuitExploreCandidate> candidates;
    QVector<CircuitExploreEdge> edges;
};

class CircuitExplorer : public QObject
{
    Q_OBJECT

public:
    explicit CircuitExplorer(const Connectome* connectome, QObject* parent = 0);

public slots:
    void invalidateCache();
    void explore(const DrosofilaLive::CircuitExploreParameters& parameters);

signals:
    void progress(int percent, const QString& phase);
    void finished(const DrosofilaLive::CircuitExploreResult& result);

private:
    bool buildReverseIndex(bool* builtNow, QString* error);
    bool resolveRoot(const QString& text,
                     quint32* index,
                     std::uint64_t* rootId,
                     QString* type,
                     QString* error);
    void buildTypeIndex();

    const Connectome* connectome_;
    QVector<quint32> reverseOffsets_;
    QVector<quint32> reverseSources_;
    QVector<qint16> reverseWeights_;
    QHash<QString, QVector<quint32> > primaryTypeIndex_;
    bool reverseReady_;
    bool typeIndexReady_;
};

}

Q_DECLARE_METATYPE(DrosofilaLive::CircuitExploreParameters)
Q_DECLARE_METATYPE(DrosofilaLive::CircuitExploreResult)
