#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <cstdint>

namespace DrosofilaLive
{
struct ExperimentParameters
{
    double durationMs = 1000.0;
    double sugarRateHz = 100.0;
    int trials = 1;
    quint32 randomSeed = 12345u;
    double temporalBinMs = 10.0;
    QString stimulusPath;
    QVector<std::uint64_t> watchedRootIds;
    QVector<std::uint64_t> ablatedRootIds;
};

struct ExperimentNeuronSummary
{
    std::uint64_t rootId = 0;
    QString type;
    double meanHz = 0.0;
    double meanFirstSpikeMs = -1.0;
    double activationProbability = 0.0;
};

struct ExperimentFrameNeuron
{
    std::uint64_t rootId = 0;
    double rateHz = 0.0;
    bool spiked = false;
};

struct ExperimentFrame
{
    double startMs = 0.0;
    double endMs = 0.0;
    QVector<ExperimentFrameNeuron> neurons;
};

struct LifModelSnapshot
{
    double dtMs = 0.10;
    double vRestMv = -52.0;
    double vThresholdMv = -45.0;
    double tauMembraneMs = 20.0;
    double tauSynapseMs = 5.0;
    double refractoryMs = 2.2;
    double delayMs = 1.8;
    double weightPerSynapseMv = 0.275;
    double poissonForceScale = 250.0;
    int edgeMode = 1;
    int minimumConnectivity = 1;
    double randomRetentionProbability = 1.0;
    quint32 edgeControlSeed = 424242u;
    double activityBinMs = 1.0;
    quint64 maxRecordedSpikes = 0;
    quint64 runawayAbortSpikesPerTrial = 0;
    bool monitorEnabled = false;
    bool traceEnabled = false;
    double temporalBinMs = 10.0;
};

struct ExperimentResult
{
    bool valid = false;
    QString error;
    QString stimulusName;
    QString stimulusPath;
    QString stimulusSource;
    QString stimulusSha256;
    QVector<std::uint64_t> stimulusRootIds;
    double sugarRateHz = 0.0;
    double stimulusStartMs = 0.0;
    double stimulusEndMs = 0.0;
    LifModelSnapshot lifModel;
    double durationMs = 0.0;
    double temporalBinMs = 0.0;
    int trialsRequested = 0;
    int trialsCompleted = 0;
    quint32 randomSeed = 0;
    qint64 wallTimeMs = 0;
    quint64 totalSpikes = 0;
    quint64 poissonEvents = 0;
    double targetMeanHz = 0.0;
    double targetStdHz = 0.0;
    double targetMeanLatencyMs = -1.0;
    QVector<std::uint64_t> watchedRootIds;
    QVector<std::uint64_t> ablatedRootIds;
    QVector<ExperimentNeuronSummary> neurons;
    QVector<ExperimentFrame> frames;
};

struct ComparisonResult
{
    bool valid = false;
    QString error;
    ExperimentResult reference;
    ExperimentResult perturbed;
    QVector<std::uint64_t> perturbedRootIds;
    double targetDeltaHz = 0.0;
    double targetRatio = 0.0;
    double sustainedOnsetMs = -1.0;
    double sustainedThresholdHz = 20.0;
    int sustainedBins = 5;
};

struct EpistasisResult
{
    bool valid = false;
    QString error;
    std::uint64_t rootA = 0;
    std::uint64_t rootB = 0;
    QString typeA;
    QString typeB;
    ExperimentResult wildType;
    ExperimentResult aOff;
    ExperimentResult bOff;
    ExperimentResult doubleOff;
    double aDeltaHz = 0.0;
    double bDeltaHz = 0.0;
    double doubleDeltaHz = 0.0;
    double expectedAdditiveHz = 0.0;
    double interactionHz = 0.0;
};


struct StatisticalSummary
{
    int n = 0;
    double mean = 0.0;
    double stddev = 0.0;
    double sem = 0.0;
    double ci95Low = 0.0;
    double ci95High = 0.0;
    double sumSquaredDeviation = 0.0;
    int positiveCount = 0;
    int negativeCount = 0;
    int zeroCount = 0;
    double signConsistency = 0.0;
    bool uncertaintyAvailable = false;
};

struct StatisticalEpistasisReplicate
{
    quint32 seed = 0;
    double wildTypeHz = 0.0;
    double aOffHz = 0.0;
    double bOffHz = 0.0;
    double doubleOffHz = 0.0;
    double expectedAdditiveHz = 0.0;
    double interactionHz = 0.0;
    double wildTypeLatencyMs = -1.0;
    double aOffLatencyMs = -1.0;
    double bOffLatencyMs = -1.0;
    double doubleOffLatencyMs = -1.0;
};

struct StatisticalEpistasisResult
{
    bool valid = false;
    QString error;
    std::uint64_t rootA = 0;
    std::uint64_t rootB = 0;
    QString typeA;
    QString typeB;
    int trialsPerReplicate = 1;
    int replicatesRequested = 0;
    int replicatesCompleted = 0;
    quint32 baseSeed = 0;
    QVector<StatisticalEpistasisReplicate> replicates;
    StatisticalSummary wildType;
    StatisticalSummary aOff;
    StatisticalSummary bOff;
    StatisticalSummary doubleOff;
    StatisticalSummary interaction;
    EpistasisResult lastEpistasis;
};

struct ConnectomeSummary
{
    bool loaded = false;
    quint32 neuronCount = 0;
    quint64 edgeCount = 0;
    quint64 synapseCount = 0;
    double mappedMiB = 0.0;
};

struct ConnectomeNeuronInfo
{
    bool present = false;
    quint32 index = 0;
    QString primaryType;
    qint8 sign = 0;
    quint32 fanIn = 0;
    quint32 fanOut = 0;
    quint32 synapsesIn = 0;
    quint32 synapsesOut = 0;
};

struct CausalAnchorValidation
{
    int idsFound = 0;
    int labelsMatched = 0;
    int expected = 0;
    QString details;

    bool complete() const { return expected > 0 && idsFound == expected; }
};
}
