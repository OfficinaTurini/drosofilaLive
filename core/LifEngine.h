#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <functional>

class Connectome;

class LifEngine
{
public:
    enum EdgeMode
    {
        NativeEdges = 0,
        MinimumStrengthEdges,
        RandomRetentionEdges,
        ShuffledWeightsEdges,
        ShuffledWeightsMinimumStrengthEdges
    };

    struct Parameters
    {
        double dtMs = 0.10;
        double durationMs = 1000.0;

        double vRestMv = -52.0;
        double vThresholdMv = -45.0;
        double tauMembraneMs = 20.0;
        double tauSynapseMs = 5.0;
        double refractoryMs = 2.2;
        double delayMs = 1.8;
        double weightPerSynapseMv = 0.275;

        // Shiu et al.: PoissonInput weight = w_syn * f_poi, f_poi=250.
        double poissonForceScale = 250.0;

        // Edge control. Existing v0.4 behaviour is MinimumStrengthEdges.
        EdgeMode edgeMode = MinimumStrengthEdges;
        int minimumConnectivity = 1;
        double randomRetentionProbability = 1.0;
        quint32 edgeControlSeed = 424242u;

        double activityBinMs = 1.0;

        int trials = 1;
        quint32 randomSeed = 12345u;

        bool monitorEnabled = false;
        quint32 monitorNeuronIndex = 0;

        quint64 maxRecordedSpikes = 500000;
        quint64 runawayAbortSpikesPerTrial = 10000000;

        // v0.6 circuit tracing. Trace data are recorded for trial 1 only.
        bool traceEnabled = false;

        // v0.7 causal ablation: electrically silenced neurons. Incoming
        // events are discarded, the neuron never spikes, and therefore it
        // produces no outgoing events. Stimulus injection is ignored too.
        QVector<quint32> ablatedNeurons;

        // v0.10 temporal causal monitoring. Counts are aggregated across all
        // trials in fixed-width time bins for the selected neurons only.
        QVector<quint32> temporalMonitorNeurons;
        double temporalBinMs = 10.0;

        // Optional low-rate progress telemetry. The callback is observational
        // only: it does not alter the numerical path/RNG sequence. Values are
        // expressed as completed/total integration steps across all trials.
        // It may be invoked from a worker thread.
        int progressIntervalSteps = 250;
        std::function<void(quint64 completedSteps, quint64 totalSteps)> progress;
    };

    // Deterministic forced spikes retained for diagnostics/regression tests.
    struct Stimulus
    {
        quint32 neuronIndex = 0;
        double startMs = 1.0;
        double intervalMs = 5.0;
        int count = 1;
    };

    struct PoissonStimulus
    {
        QString name;
        QVector<quint32> neuronIndices;
        double rateHz = 100.0;
        double startMs = 0.0;
        double endMs = 1000.0;
    };

    struct Spike
    {
        float timeMs = 0.0f;
        quint32 neuronIndex = 0;
    };

    struct Result
    {
        // Raster/activity are recorded for trial 1 only. Aggregate counters span all trials.
        QVector<Spike> spikes;
        QVector<quint32> activityBins;
        QVector<quint32> monitorSpikesPerTrial;

        double activityBinMs = 1.0;
        double requestedDurationMs = 0.0;
        double simulatedDurationMs = 0.0;  // per trial
        double dtMs = 0.0;

        int trialsRequested = 0;
        int trialsCompleted = 0;

        quint64 totalSpikes = 0;
        quint64 rasterTrialSpikes = 0;
        quint64 poissonEvents = 0;
        quint64 poissonDrivenSpikes = 0;
        quint64 forcedSpikeEvents = 0;
        quint64 networkGeneratedSpikes = 0;
        quint64 scheduledEdgeEvents = 0;
        quint32 peakSpikesPerStep = 0;
        qint64 wallTimeMs = 0;

        double monitorMeanHz = 0.0;
        double monitorStdHz = 0.0;
        double monitorActivationProbability = 0.0;
        double monitorMeanLatencyMs = -1.0;
        QVector<float> monitorFirstSpikeMsPerTrial;

        // v0.10 temporal causal arrays. temporalSpikeCounts is neuron-major:
        // slot * temporalBinCount + bin, aggregated across all completed trials.
        // temporalFirstSpikeMsPerTrial is trial-major:
        // trial * temporalNeuronCount + slot.
        QVector<quint32> temporalNeuronIndices;
        QVector<quint32> temporalSpikeCounts;
        QVector<quint32> temporalSpikeCountsPerTrial;
        QVector<float> temporalFirstSpikeMsPerTrial;
        double temporalBinMs = 0.0;
        int temporalBinCount = 0;

        // v0.6 trace arrays (trial 1 only). Empty when traceEnabled=false.
        QVector<quint32> traceSpikeCount;
        QVector<quint32> traceNetworkSpikeCount;
        QVector<float> traceFirstSpikeMs;
        QVector<quint32> traceIncomingEdgeEvents;
        QVector<quint32> traceOutgoingEdgeEvents;
        QVector<qint64> traceSignedInputConnectivity;
        QVector<quint64> traceAbsInputConnectivity;
        QVector<quint32> traceEdgeEventCount;

        bool recordingTruncated = false;
        bool runawayAborted = false;
        QString error;
    };

    explicit LifEngine(const Connectome *connectome = nullptr);

    void setConnectome(const Connectome *connectome);

    Result run(const Parameters &parameters,
               const QVector<Stimulus> &forcedStimuli,
               const QVector<PoissonStimulus> &poissonStimuli) const;

    // Helpers for controlled experiments. These perform one full edge scan.
    quint64 totalEdgeCount() const;
    quint64 countEdgesMinimumConnectivity(int minimumConnectivity) const;
    quint64 countEdgesRandomRetention(double probability, quint32 seed) const;

private:
    const Connectome *m_connectome;
};
