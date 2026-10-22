#include "LifEngine.h"
#include "Connectome.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace
{
int positiveRoundedTicks(double valueMs, double dtMs)
{
    if (valueMs <= 0.0)
        return 0;
    return std::max(1, static_cast<int>(std::llround(valueMs / dtMs)));
}

quint32 mixedSeed(quint32 base, int trial)
{
    // Deterministic trial separation without depending on global RNG state.
    return base ^ (0x9E3779B9u + static_cast<quint32>(trial) * 0x85EBCA6Bu);
}

quint64 splitMix64(quint64 x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

bool randomEdgeKept(quint64 edgeIndex, double probability, quint32 seed)
{
    if (probability <= 0.0)
        return false;
    if (probability >= 1.0)
        return true;

    const quint64 h = splitMix64(edgeIndex ^
        (static_cast<quint64>(seed) * 0xD6E8FEB86659FD93ull));

    // Top 53 bits -> [0,1), deterministic across trials/runs.
    const double u = static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
    return u < probability;
}
}

LifEngine::LifEngine(const Connectome *connectome)
    : m_connectome(connectome)
{
}

void LifEngine::setConnectome(const Connectome *connectome)
{
    m_connectome = connectome;
}

quint64 LifEngine::totalEdgeCount() const
{
    return (m_connectome && m_connectome->isLoaded()) ? m_connectome->edgeCount() : 0;
}

quint64 LifEngine::countEdgesMinimumConnectivity(int minimumConnectivity) const
{
    if (!m_connectome || !m_connectome->isLoaded())
        return 0;

    const int threshold = std::max(1, minimumConnectivity);
    quint64 count = 0;
    const quint64 eCount = m_connectome->edgeCount();
    for (quint64 e = 0; e < eCount; ++e)
    {
        if (std::abs(static_cast<int>(m_connectome->edgeWeight(e))) >= threshold)
            ++count;
    }
    return count;
}

quint64 LifEngine::countEdgesRandomRetention(double probability, quint32 seed) const
{
    if (!m_connectome || !m_connectome->isLoaded())
        return 0;

    const quint64 eCount = m_connectome->edgeCount();
    if (probability <= 0.0)
        return 0;
    if (probability >= 1.0)
        return eCount;

    quint64 count = 0;
    for (quint64 e = 0; e < eCount; ++e)
    {
        if (randomEdgeKept(e, probability, seed))
            ++count;
    }
    return count;
}

LifEngine::Result LifEngine::run(const Parameters &p,
                                 const QVector<Stimulus> &forcedStimuli,
                                 const QVector<PoissonStimulus> &poissonStimuli) const
{
    Result result;
    result.activityBinMs = p.activityBinMs;
    result.requestedDurationMs = p.durationMs;
    result.dtMs = p.dtMs;
    result.trialsRequested = p.trials;

    if (!m_connectome || !m_connectome->isLoaded())
    {
        result.error = QStringLiteral("Connectoma non caricato.");
        return result;
    }

    if (p.dtMs <= 0.0 || p.durationMs <= 0.0 ||
        p.tauMembraneMs <= 0.0 || p.tauSynapseMs <= 0.0 ||
        p.activityBinMs <= 0.0 || p.weightPerSynapseMv <= 0.0 ||
        p.poissonForceScale <= 0.0 || p.trials <= 0)
    {
        result.error = QStringLiteral("Parametri LIF non validi.");
        return result;
    }

    if (p.edgeMode == RandomRetentionEdges &&
        (p.randomRetentionProbability < 0.0 || p.randomRetentionProbability > 1.0))
    {
        result.error = QStringLiteral("Random retention probability deve essere in [0,1].");
        return result;
    }

    if (std::abs(p.tauMembraneMs - p.tauSynapseMs) < 1e-9)
    {
        result.error = QStringLiteral("tauMembrane e tauSynapse non possono coincidere in questa implementazione.");
        return result;
    }

    const quint32 n = m_connectome->neuronCount();
    if (p.monitorEnabled && p.monitorNeuronIndex >= n)
    {
        result.error = QStringLiteral("Indice neurone monitor non valido.");
        return result;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(p.durationMs / p.dtMs)));
    const int progressIntervalSteps = std::max(1, p.progressIntervalSteps);
    const quint64 progressTotalSteps =
        static_cast<quint64>(steps) * static_cast<quint64>(p.trials);
    const int delayTicks = std::max(1, positiveRoundedTicks(p.delayMs, p.dtMs));
    const int refractoryTicks = positiveRoundedTicks(p.refractoryMs, p.dtMs);
    const int ringSlots = delayTicks + 1;

    const int activityBinCount = std::max(1, static_cast<int>(std::ceil(p.durationMs / p.activityBinMs)));
    result.activityBins.fill(0, activityBinCount);

    // v0.10 selected-neuron temporal monitor. Keep a dense index->slot lookup
    // (~0.55 MiB for the current connectome) so recording adds O(spikes), not
    // O(selected neurons), work to the hot integration loop.
    QVector<int> temporalSlotByNeuron;
    int temporalBinCount = 0;
    if (!p.temporalMonitorNeurons.isEmpty())
    {
        if (p.temporalBinMs <= 0.0)
        {
            result.error = QStringLiteral("temporalBinMs deve essere > 0.");
            return result;
        }

        temporalSlotByNeuron.fill(-1, static_cast<int>(n));
        for (quint32 idx : p.temporalMonitorNeurons)
        {
            if (idx >= n)
                continue;
            if (temporalSlotByNeuron[static_cast<int>(idx)] >= 0)
                continue;
            const int slot = result.temporalNeuronIndices.size();
            temporalSlotByNeuron[static_cast<int>(idx)] = slot;
            result.temporalNeuronIndices.append(idx);
        }

        temporalBinCount = std::max(1, static_cast<int>(std::ceil(p.durationMs / p.temporalBinMs)));
        result.temporalBinMs = p.temporalBinMs;
        result.temporalBinCount = temporalBinCount;

        const quint64 temporalCells =
            static_cast<quint64>(result.temporalNeuronIndices.size()) *
            static_cast<quint64>(temporalBinCount);
        if (temporalCells > static_cast<quint64>(std::numeric_limits<int>::max()))
        {
            result.error = QStringLiteral("Temporal monitor troppo grande.");
            return result;
        }
        result.temporalSpikeCounts.fill(0u, static_cast<int>(temporalCells));
        const quint64 temporalTrialCells = temporalCells * static_cast<quint64>(p.trials);
        if (temporalTrialCells > static_cast<quint64>(std::numeric_limits<int>::max()))
        {
            result.error = QStringLiteral("Temporal per-trial monitor troppo grande.");
            return result;
        }
        result.temporalSpikeCountsPerTrial.fill(0u, static_cast<int>(temporalTrialCells));
        result.temporalFirstSpikeMsPerTrial.reserve(
            result.temporalNeuronIndices.size() * p.trials);
    }

    const quint64 pendingCount = static_cast<quint64>(ringSlots) * static_cast<quint64>(n);
    const quint64 pendingBytes = pendingCount * sizeof(float);
    const quint64 maxDelayRingBytes = 512ull * 1024ull * 1024ull;
    if (pendingCount > static_cast<quint64>(std::numeric_limits<int>::max()) ||
        pendingBytes > maxDelayRingBytes)
    {
        result.error = QStringLiteral(
            "Delay ring troppo grande (>512 MiB): aumentare dt o ridurre il delay.");
        return result;
    }

    // Exact sub-step solution for non-refractory intervals:
    //   dg/dt = -g/tauSyn
    //   dV/dt = (Vrest - V + g)/tauMem
    const double aMem = std::exp(-p.dtMs / p.tauMembraneMs);
    const double aSyn = std::exp(-p.dtMs / p.tauSynapseMs);
    const double gToV = p.tauSynapseMs /
                        (p.tauMembraneMs - p.tauSynapseMs) *
                        (aMem - aSyn);

    // Brian2 PoissonInput with N=1 is effectively a Bernoulli event each dt.
    for (const PoissonStimulus &group : poissonStimuli)
    {
        if (group.rateHz < 0.0)
        {
            result.error = QStringLiteral("Rate Poisson negativa.");
            return result;
        }
        const double probability = group.rateHz * p.dtMs / 1000.0;
        if (probability > 1.0)
        {
            result.error = QStringLiteral("rate * dt > 1: ridurre rate o dt.");
            return result;
        }
    }

    QVector<QVector<quint32>> forcedByStep(steps);
    for (const Stimulus &stim : forcedStimuli)
    {
        if (stim.neuronIndex >= n || stim.count <= 0)
            continue;

        for (int k = 0; k < stim.count; ++k)
        {
            const double tMs = stim.startMs + static_cast<double>(k) * stim.intervalMs;
            const int step = static_cast<int>(std::llround(tMs / p.dtMs));
            if (step >= 0 && step < steps)
                forcedByStep[step].append(stim.neuronIndex);
        }
    }

    QVector<quint8> zeroRefractory(static_cast<int>(n), 0u);
    for (const PoissonStimulus &group : poissonStimuli)
    {
        for (quint32 idx : group.neuronIndices)
        {
            if (idx < n)
                zeroRefractory[static_cast<int>(idx)] = 1u;
        }
    }

    QVector<quint8> ablated(static_cast<int>(n), 0u);
    for (quint32 idx : p.ablatedNeurons)
    {
        if (idx < n)
            ablated[static_cast<int>(idx)] = 1u;
    }

    // Causal control D: topology and sign stay fixed, while absolute edge
    // strengths are permuted independently inside + and - classes.
    QVector<qint16> shuffledWeights;
    if (p.edgeMode == ShuffledWeightsEdges ||
        p.edgeMode == ShuffledWeightsMinimumStrengthEdges)
    {
        const quint64 eCount64 = m_connectome->edgeCount();
        if (eCount64 > static_cast<quint64>(std::numeric_limits<int>::max()))
        {
            result.error = QStringLiteral("Troppi archi per lo shuffle pesi QVector.");
            return result;
        }

        const int eCount = static_cast<int>(eCount64);
        QVector<quint16> positive;
        QVector<quint16> negative;
        positive.reserve((eCount * 3) / 5);
        negative.reserve((eCount * 2) / 5);

        for (int ei = 0; ei < eCount; ++ei)
        {
            const qint16 w = m_connectome->edgeWeight(static_cast<quint64>(ei));
            const quint16 magnitude = static_cast<quint16>(std::abs(static_cast<int>(w)));
            if (w >= 0)
                positive.append(magnitude);
            else
                negative.append(magnitude);
        }

        std::mt19937 shuffleRng(p.edgeControlSeed);
        std::shuffle(positive.begin(), positive.end(), shuffleRng);
        std::shuffle(negative.begin(), negative.end(), shuffleRng);

        shuffledWeights.resize(eCount);
        int pos = 0;
        int neg = 0;
        for (int ei = 0; ei < eCount; ++ei)
        {
            const qint16 original = m_connectome->edgeWeight(static_cast<quint64>(ei));
            if (original >= 0)
                shuffledWeights[ei] = static_cast<qint16>(positive[pos++]);
            else
                shuffledWeights[ei] = static_cast<qint16>(-static_cast<qint32>(negative[neg++]));
        }
    }

    const double poissonJumpMv = p.weightPerSynapseMv * p.poissonForceScale;

    if (p.traceEnabled)
    {
        const quint64 edgeCount64 = m_connectome->edgeCount();
        if (edgeCount64 > static_cast<quint64>(std::numeric_limits<int>::max()))
        {
            result.error = QStringLiteral("Troppi archi per il trace v0.6.");
            return result;
        }

        result.traceSpikeCount.fill(0u, static_cast<int>(n));
        result.traceNetworkSpikeCount.fill(0u, static_cast<int>(n));
        result.traceFirstSpikeMs.fill(-1.0f, static_cast<int>(n));
        result.traceIncomingEdgeEvents.fill(0u, static_cast<int>(n));
        result.traceOutgoingEdgeEvents.fill(0u, static_cast<int>(n));
        result.traceSignedInputConnectivity.fill(0, static_cast<int>(n));
        result.traceAbsInputConnectivity.fill(0u, static_cast<int>(n));
        result.traceEdgeEventCount.fill(0u, static_cast<int>(edgeCount64));
    }

    for (int trial = 0; trial < p.trials; ++trial)
    {
        QVector<float> voltage(static_cast<int>(n), static_cast<float>(p.vRestMv));
        QVector<float> conductance(static_cast<int>(n), 0.0f);
        QVector<quint32> refractoryUntil(static_cast<int>(n), 0u);
        QVector<float> pending(static_cast<int>(pendingCount), 0.0f);

        QVector<quint32> currentSpikes;
        currentSpikes.reserve(4096);

        QVector<quint32> spikeStamp(static_cast<int>(n), std::numeric_limits<quint32>::max());

        std::mt19937 rng(mixedSeed(p.randomSeed, trial));
        std::uniform_real_distribution<double> uniform01(0.0, 1.0);

        quint64 trialSpikes = 0;
        quint32 monitorCount = 0;
        float monitorFirstSpikeMs = -1.0f;
        QVector<float> temporalFirstThisTrial(
            result.temporalNeuronIndices.size(), -1.0f);
        int ringIndex = 0;

        for (int step = 0; step < steps; ++step)
        {
            currentSpikes.clear();
            float *incomingSlot = pending.data() + static_cast<quint64>(ringIndex) * n;

            enum SpikeSource { NaturalSpike = 0, ForcedSpike = 1, PoissonSpike = 2 };

            auto emitSpike = [&](quint32 idx, SpikeSource source)
            {
                const int ii = static_cast<int>(idx);
                if (ablated[ii])
                    return;
                if (spikeStamp[ii] == static_cast<quint32>(step))
                    return;

                spikeStamp[ii] = static_cast<quint32>(step);
                voltage[ii] = static_cast<float>(p.vRestMv);
                conductance[ii] = 0.0f; // Shiu/Brian reset: g = 0 mV

                if (zeroRefractory[ii])
                    refractoryUntil[ii] = static_cast<quint32>(step);
                else
                    refractoryUntil[ii] = static_cast<quint32>(step + refractoryTicks);

                currentSpikes.append(idx);
                if (source == ForcedSpike)
                    ++result.forcedSpikeEvents;
                else if (source == PoissonSpike)
                    ++result.poissonDrivenSpikes;
                else
                    ++result.networkGeneratedSpikes;

                if (p.traceEnabled && trial == 0)
                {
                    ++result.traceSpikeCount[ii];
                    if (source == NaturalSpike)
                        ++result.traceNetworkSpikeCount[ii];
                    if (result.traceFirstSpikeMs[ii] < 0.0f)
                        result.traceFirstSpikeMs[ii] = static_cast<float>(
                            static_cast<double>(step) * p.dtMs);
                }
            };

            for (quint32 idx : forcedByStep[step])
            {
                if (idx < n)
                    emitSpike(idx, ForcedSpike);
            }

            // In the Shiu model both dv/dt and dg/dt are "unless refractory".
            for (quint32 i = 0; i < n; ++i)
            {
                const int ii = static_cast<int>(i);
                const double incoming = static_cast<double>(incomingSlot[ii]);
                incomingSlot[ii] = 0.0f;

                if (ablated[ii])
                {
                    voltage[ii] = static_cast<float>(p.vRestMv);
                    conductance[ii] = 0.0f;
                    continue;
                }

                if (spikeStamp[ii] == static_cast<quint32>(step))
                    continue;

                if (static_cast<quint32>(step) < refractoryUntil[ii])
                {
                    conductance[ii] += static_cast<float>(incoming);
                    voltage[ii] = static_cast<float>(p.vRestMv);
                    continue;
                }

                const double g0 = static_cast<double>(conductance[ii]) + incoming;
                const double g1 = g0 * aSyn;
                conductance[ii] = static_cast<float>(g1);

                const double u0 = static_cast<double>(voltage[ii]) - p.vRestMv;
                const double u1 = u0 * aMem + g0 * gToV;
                const double v1 = p.vRestMv + u1;
                voltage[ii] = static_cast<float>(v1);

                if (v1 > p.vThresholdMv)
                    emitSpike(i, NaturalSpike);
            }

            const double timeMs = static_cast<double>(step) * p.dtMs;
            for (const PoissonStimulus &group : poissonStimuli)
            {
                if (group.rateHz <= 0.0 || timeMs < group.startMs || timeMs >= group.endMs)
                    continue;

                const double probability = group.rateHz * p.dtMs / 1000.0;
                for (quint32 idx : group.neuronIndices)
                {
                    if (idx >= n || ablated[static_cast<int>(idx)])
                        continue;

                    if (uniform01(rng) < probability)
                    {
                        ++result.poissonEvents;
                        const int ii = static_cast<int>(idx);

                        if (spikeStamp[ii] == static_cast<quint32>(step))
                            continue;

                        voltage[ii] += static_cast<float>(poissonJumpMv);
                        if (static_cast<double>(voltage[ii]) > p.vThresholdMv)
                            emitSpike(idx, PoissonSpike);
                    }
                }
            }

            const quint32 spikeCountThisStep = static_cast<quint32>(currentSpikes.size());
            result.peakSpikesPerStep = std::max(result.peakSpikesPerStep, spikeCountThisStep);
            result.totalSpikes += spikeCountThisStep;
            trialSpikes += spikeCountThisStep;

            if (p.monitorEnabled)
            {
                for (quint32 idx : currentSpikes)
                {
                    if (idx == p.monitorNeuronIndex)
                    {
                        ++monitorCount;
                        if (monitorFirstSpikeMs < 0.0f)
                            monitorFirstSpikeMs = static_cast<float>(timeMs);
                    }
                }
            }

            if (!temporalSlotByNeuron.isEmpty())
            {
                const int temporalBin = std::min(temporalBinCount - 1,
                    std::max(0, static_cast<int>(timeMs / p.temporalBinMs)));
                for (quint32 idx : currentSpikes)
                {
                    const int slot = temporalSlotByNeuron[static_cast<int>(idx)];
                    if (slot < 0)
                        continue;
                    ++result.temporalSpikeCounts[slot * temporalBinCount + temporalBin];
                    const int perTrialPos =
                        (trial * result.temporalNeuronIndices.size() + slot) * temporalBinCount + temporalBin;
                    ++result.temporalSpikeCountsPerTrial[perTrialPos];
                    if (temporalFirstThisTrial[slot] < 0.0f)
                        temporalFirstThisTrial[slot] = static_cast<float>(timeMs);
                }
            }

            if (trial == 0)
            {
                result.rasterTrialSpikes += spikeCountThisStep;

                const int bin = std::min(activityBinCount - 1,
                                         std::max(0, static_cast<int>(timeMs / p.activityBinMs)));
                result.activityBins[bin] += spikeCountThisStep;

                for (quint32 idx : currentSpikes)
                {
                    if (result.spikes.size() < static_cast<int>(std::min<quint64>(
                            p.maxRecordedSpikes,
                            static_cast<quint64>(std::numeric_limits<int>::max()))))
                    {
                        Spike s;
                        s.timeMs = static_cast<float>(timeMs);
                        s.neuronIndex = idx;
                        result.spikes.append(s);
                    }
                    else
                    {
                        result.recordingTruncated = true;
                    }
                }
            }

            if (trialSpikes >= p.runawayAbortSpikesPerTrial)
            {
                result.runawayAborted = true;
                result.simulatedDurationMs = timeMs;
                break;
            }

            const int dueSlot = (ringIndex + delayTicks) % ringSlots;
            float *due = pending.data() + static_cast<quint64>(dueSlot) * n;

            for (quint32 source : currentSpikes)
            {
                const quint64 begin = m_connectome->edgeBegin(source);
                const quint64 end = m_connectome->edgeEnd(source);

                for (quint64 e = begin; e < end; ++e)
                {
                    qint16 effectiveWeight = m_connectome->edgeWeight(e);

                    if (p.edgeMode == MinimumStrengthEdges)
                    {
                        if (std::abs(static_cast<int>(effectiveWeight)) < p.minimumConnectivity)
                            continue;
                    }
                    else if (p.edgeMode == RandomRetentionEdges)
                    {
                        if (!randomEdgeKept(e, p.randomRetentionProbability, p.edgeControlSeed))
                            continue;
                    }
                    else if (p.edgeMode == ShuffledWeightsEdges)
                    {
                        effectiveWeight = shuffledWeights[static_cast<int>(e)];
                    }
                    else if (p.edgeMode == ShuffledWeightsMinimumStrengthEdges)
                    {
                        effectiveWeight = shuffledWeights[static_cast<int>(e)];
                        if (std::abs(static_cast<int>(effectiveWeight)) < p.minimumConnectivity)
                            continue;
                    }
                    // NativeEdges: no change/no filtering.

                    const quint32 target = m_connectome->edgeTarget(e);
                    if (ablated[static_cast<int>(target)])
                        continue;

                    due[target] += static_cast<float>(
                        static_cast<double>(effectiveWeight) * p.weightPerSynapseMv);
                    ++result.scheduledEdgeEvents;

                    if (p.traceEnabled && trial == 0)
                    {
                        const int ei = static_cast<int>(e);
                        ++result.traceEdgeEventCount[ei];
                        ++result.traceOutgoingEdgeEvents[static_cast<int>(source)];
                        ++result.traceIncomingEdgeEvents[static_cast<int>(target)];
                        result.traceSignedInputConnectivity[static_cast<int>(target)] +=
                            static_cast<qint64>(effectiveWeight);
                        result.traceAbsInputConnectivity[static_cast<int>(target)] +=
                            static_cast<quint64>(std::abs(static_cast<int>(effectiveWeight)));
                    }
                }
            }

            ringIndex = (ringIndex + 1) % ringSlots;
            result.simulatedDurationMs = std::min(
                p.durationMs,
                (static_cast<double>(step) + 1.0) * p.dtMs);

            if (p.progress &&
                (((step + 1) % progressIntervalSteps) == 0 || (step + 1) == steps))
            {
                const quint64 completed =
                    static_cast<quint64>(trial) * static_cast<quint64>(steps) +
                    static_cast<quint64>(step + 1);
                p.progress(completed, progressTotalSteps);
            }
        }

        if (p.monitorEnabled)
        {
            result.monitorSpikesPerTrial.append(monitorCount);
            result.monitorFirstSpikeMsPerTrial.append(monitorFirstSpikeMs);
        }

        for (float firstMs : temporalFirstThisTrial)
            result.temporalFirstSpikeMsPerTrial.append(firstMs);

        ++result.trialsCompleted;

        if (result.runawayAborted)
            break;
    }

    if (p.monitorEnabled && !result.monitorSpikesPerTrial.isEmpty())
    {
        const double seconds = p.durationMs / 1000.0;
        double sumHz = 0.0;
        for (quint32 count : result.monitorSpikesPerTrial)
            sumHz += static_cast<double>(count) / seconds;

        result.monitorMeanHz = sumHz / result.monitorSpikesPerTrial.size();

        double variance = 0.0;
        for (quint32 count : result.monitorSpikesPerTrial)
        {
            const double hz = static_cast<double>(count) / seconds;
            const double d = hz - result.monitorMeanHz;
            variance += d * d;
        }
        variance /= result.monitorSpikesPerTrial.size();
        result.monitorStdHz = std::sqrt(variance);

        int activeTrials = 0;
        double latencySum = 0.0;
        for (float latency : result.monitorFirstSpikeMsPerTrial)
        {
            if (latency >= 0.0f)
            {
                ++activeTrials;
                latencySum += latency;
            }
        }
        result.monitorActivationProbability =
            static_cast<double>(activeTrials) / result.monitorSpikesPerTrial.size();
        if (activeTrials > 0)
            result.monitorMeanLatencyMs = latencySum / activeTrials;
    }

    return result;
}
