#include "SimulationController.h"

#include "../core/Connectome.h"
#include "../core/LifEngine.h"
#include "../core/StimulusSet.h"
#include "../model/CausalCircuit.h"

#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>

namespace DrosofilaLive
{
namespace
{
const std::uint64_t kTargetRootId = 720575940660219265ULL;

QString typeForRoot(std::uint64_t rootId)
{
    const std::vector<CausalNeuronSpec>& specs = validatedCausalCircuit();
    for (std::vector<CausalNeuronSpec>::const_iterator it = specs.begin(); it != specs.end(); ++it)
    {
        if (it->rootId == rootId)
            return QString::fromLatin1(it->type);
    }
    return QStringLiteral("?");
}

bool containsRoot(const QVector<std::uint64_t>& roots, std::uint64_t rootId)
{
    for (int i = 0; i < roots.size(); ++i)
        if (roots.at(i) == rootId)
            return true;
    return false;
}
}

SimulationController::SimulationController(QObject* parent)
    : QThread(parent), connectome_(0)
{
}

SimulationController::~SimulationController()
{
    if (isRunning())
        wait();
}

void SimulationController::setConnectome(const Connectome* connectome)
{
    if (isRunning())
        return;
    connectome_ = connectome;
}

bool SimulationController::startExperiment(const ExperimentParameters& parameters)
{
    if (isRunning() || !connectome_ || !connectome_->isLoaded())
        return false;

    {
        QMutexLocker lock(&mutex_);
        parameters_ = parameters;
        result_ = ExperimentResult();
    }
    start();
    return true;
}

ExperimentResult SimulationController::result() const
{
    QMutexLocker lock(&mutex_);
    return result_;
}

LifModelSnapshot SimulationController::validatedLifModelSnapshot()
{
    LifModelSnapshot m;
    m.dtMs = 0.10;
    m.vRestMv = -52.0;
    m.vThresholdMv = -45.0;
    m.tauMembraneMs = 20.0;
    m.tauSynapseMs = 5.0;
    m.refractoryMs = 2.2;
    m.delayMs = 1.8;
    m.weightPerSynapseMv = 0.275;
    m.poissonForceScale = 250.0;
    m.edgeMode = static_cast<int>(LifEngine::MinimumStrengthEdges);
    m.minimumConnectivity = 1;
    m.randomRetentionProbability = 1.0;
    m.edgeControlSeed = 424242u;
    m.activityBinMs = 1.0;
    m.maxRecordedSpikes = 1;
    m.runawayAbortSpikesPerTrial = 10000000;
    m.monitorEnabled = true;
    m.traceEnabled = false;
    m.temporalBinMs = 10.0;
    return m;
}

void SimulationController::run()
{
    ExperimentParameters parameters;
    {
        QMutexLocker lock(&mutex_);
        parameters = parameters_;
    }

    const ExperimentResult r = execute(parameters);
    {
        QMutexLocker lock(&mutex_);
        result_ = r;
    }
}

ExperimentResult SimulationController::execute(const ExperimentParameters& request)
{
    ExperimentResult out;
    out.durationMs = request.durationMs;
    out.temporalBinMs = request.temporalBinMs;
    out.trialsRequested = request.trials;
    out.randomSeed = request.randomSeed;
    out.watchedRootIds = request.watchedRootIds;
    out.ablatedRootIds = request.ablatedRootIds;

    if (!connectome_ || !connectome_->isLoaded())
    {
        out.error = QStringLiteral("Connectome not loaded.");
        return out;
    }
    if (request.durationMs <= 0.0 || request.sugarRateHz < 0.0 ||
        request.trials <= 0 || request.temporalBinMs <= 0.0)
    {
        out.error = QStringLiteral("Invalid experiment parameters.");
        return out;
    }
    if (request.watchedRootIds.isEmpty())
    {
        out.error = QStringLiteral("At least one watched neuron is required.");
        return out;
    }

    StimulusSet sugarSet;
    QString stimulusMessage;
    if (!sugarSet.load(request.stimulusPath, *connectome_, &stimulusMessage))
    {
        out.error = stimulusMessage;
        return out;
    }
    if (!sugarSet.missingRootIds().isEmpty())
    {
        out.error = QStringLiteral("Sugar stimulus is incomplete for the loaded connectome: %1 root IDs missing.")
                        .arg(sugarSet.missingRootIds().size());
        return out;
    }
    out.stimulusName = sugarSet.name();
    out.stimulusPath = request.stimulusPath;
    out.stimulusSource = sugarSet.source();
    for (int i = 0; i < sugarSet.rootIds().size(); ++i)
        out.stimulusRootIds.append(static_cast<std::uint64_t>(sugarSet.rootIds().at(i)));
    QFile stimulusFile(request.stimulusPath);
    if (stimulusFile.open(QIODevice::ReadOnly))
        out.stimulusSha256 = QString::fromLatin1(
            QCryptographicHash::hash(stimulusFile.readAll(), QCryptographicHash::Sha256).toHex());
    out.sugarRateHz = request.sugarRateHz;
    out.stimulusStartMs = 0.0;
    out.stimulusEndMs = request.durationMs;

    // Validate the complete scientific anchor set and resolve TARGET/MN9.
    const std::vector<CausalNeuronSpec>& specs = validatedCausalCircuit();
    quint32 targetIndex = 0;
    bool targetFound = false;
    for (std::vector<CausalNeuronSpec>::const_iterator it = specs.begin(); it != specs.end(); ++it)
    {
        quint32 index = 0;
        if (!connectome_->indexFromRootId(static_cast<quint64>(it->rootId), &index))
        {
            out.error = QStringLiteral("Validated neuron %1 [%2] is missing from the loaded connectome.")
                            .arg(QString::fromLatin1(it->type))
                            .arg(static_cast<qulonglong>(it->rootId));
            return out;
        }
        if (it->rootId == kTargetRootId)
        {
            targetIndex = index;
            targetFound = true;
        }
    }
    if (!targetFound)
    {
        out.error = QStringLiteral("TARGET/MN9 root ID not found in validated circuit.");
        return out;
    }

    QVector<quint32> trackedIndices;
    QVector<std::uint64_t> trackedRootIds;
    trackedIndices.reserve(request.watchedRootIds.size());
    trackedRootIds.reserve(request.watchedRootIds.size());
    for (int i = 0; i < request.watchedRootIds.size(); ++i)
    {
        const std::uint64_t rootId = request.watchedRootIds.at(i);
        quint32 index = 0;
        if (!connectome_->indexFromRootId(static_cast<quint64>(rootId), &index))
        {
            out.error = QStringLiteral("Watched root ID %1 is missing from the loaded connectome.")
                            .arg(static_cast<qulonglong>(rootId));
            return out;
        }
        trackedIndices.append(index);
        trackedRootIds.append(rootId);
    }

    QVector<quint32> ablatedIndices;
    ablatedIndices.reserve(request.ablatedRootIds.size());
    for (int i = 0; i < request.ablatedRootIds.size(); ++i)
    {
        const std::uint64_t rootId = request.ablatedRootIds.at(i);
        quint32 index = 0;
        if (!connectome_->indexFromRootId(static_cast<quint64>(rootId), &index))
        {
            out.error = QStringLiteral("Ablated root ID %1 is missing from the loaded connectome.")
                            .arg(static_cast<qulonglong>(rootId));
            return out;
        }
        ablatedIndices.append(index);
    }

    LifEngine engine(connectome_);
    LifEngine::Parameters p;
    p.durationMs = request.durationMs;
    p.dtMs = 0.1;
    p.trials = request.trials;
    p.randomSeed = request.randomSeed;
    p.edgeMode = LifEngine::MinimumStrengthEdges;
    p.minimumConnectivity = 1;
    p.temporalBinMs = request.temporalBinMs;
    p.temporalMonitorNeurons = trackedIndices;
    p.monitorEnabled = true;
    p.monitorNeuronIndex = targetIndex;
    p.traceEnabled = false;
    p.maxRecordedSpikes = 1;
    p.runawayAbortSpikesPerTrial = 10000000;
    p.ablatedNeurons = ablatedIndices;

    // Validated Shiu-derived LIF preset used by drosofila v0.15.
    p.vRestMv = -52.0;
    p.vThresholdMv = -45.0;
    p.tauMembraneMs = 20.0;
    p.tauSynapseMs = 5.0;
    p.refractoryMs = 2.2;
    p.delayMs = 1.8;
    p.weightPerSynapseMv = 0.275;
    p.poissonForceScale = 250.0;

    // Persist the exact effective LIF configuration as observational metadata.
    // The validated preset is also exposed to the REPRODUCE preflight.
    out.lifModel = validatedLifModelSnapshot();
    out.lifModel.temporalBinMs = p.temporalBinMs;


    p.progressIntervalSteps = std::max(50, static_cast<int>(request.durationMs / p.dtMs) / 100);
    const quint64 stepsPerTrial = static_cast<quint64>(
        std::max(1, static_cast<int>(std::ceil(request.durationMs / p.dtMs))));
    const int requestedTrials = request.trials;
    p.progress = [this, stepsPerTrial, requestedTrials](quint64 completed, quint64 total)
    {
        const quint64 done = total > 0 ? std::min(completed, total) : completed;
        const int percent = total > 0
            ? static_cast<int>(std::min<quint64>(100, (done * 100) / total))
            : 0;

        int trial = 1;
        int trialPercent = 0;
        if (stepsPerTrial > 0 && requestedTrials > 0)
        {
            const quint64 zeroBasedStep = done > 0 ? done - 1 : 0;
            trial = std::min(requestedTrials,
                             static_cast<int>(zeroBasedStep / stepsPerTrial) + 1);
            const quint64 trialStart = static_cast<quint64>(trial - 1) * stepsPerTrial;
            const quint64 doneInTrial = done > trialStart
                ? std::min(stepsPerTrial, done - trialStart)
                : 0;
            trialPercent = static_cast<int>(
                std::min<quint64>(100, (doneInTrial * 100) / stepsPerTrial));
        }

        emit simulationProgress(percent,
            QStringLiteral("LIF trial %1/%2 (%3%)")
                .arg(trial).arg(requestedTrials).arg(trialPercent));
    };

    LifEngine::PoissonStimulus sugar;
    sugar.name = sugarSet.name();
    sugar.neuronIndices = sugarSet.indices();
    sugar.rateHz = request.sugarRateHz;
    sugar.startMs = 0.0;
    sugar.endMs = request.durationMs;

    QVector<LifEngine::PoissonStimulus> poisson;
    poisson.append(sugar);

    QElapsedTimer timer;
    timer.start();
    const LifEngine::Result r = engine.run(p, QVector<LifEngine::Stimulus>(), poisson);
    out.wallTimeMs = timer.elapsed();

    if (!r.error.isEmpty())
    {
        out.error = r.error;
        return out;
    }
    if (r.runawayAborted)
    {
        out.error = QStringLiteral("Experiment aborted by runaway-spike protection.");
        return out;
    }
    if (r.temporalNeuronIndices.size() != trackedIndices.size() || r.temporalBinCount <= 0)
    {
        out.error = QStringLiteral("Temporal monitor result is inconsistent.");
        return out;
    }

    out.trialsCompleted = r.trialsCompleted;
    out.totalSpikes = r.totalSpikes;
    out.poissonEvents = r.poissonEvents;
    out.targetMeanHz = r.monitorMeanHz;
    out.targetStdHz = r.monitorStdHz;
    out.targetMeanLatencyMs = r.monitorMeanLatencyMs;

    const int neuronCount = trackedIndices.size();
    const int binCount = r.temporalBinCount;
    const double durationSeconds = request.durationMs / 1000.0;
    const double binSeconds = r.temporalBinMs / 1000.0;
    const int completedTrials = std::max(1, r.trialsCompleted);

    out.neurons.reserve(neuronCount);
    for (int neuron = 0; neuron < neuronCount; ++neuron)
    {
        quint64 totalCount = 0;
        for (int bin = 0; bin < binCount; ++bin)
            totalCount += r.temporalSpikeCounts.at(neuron * binCount + bin);

        double firstSum = 0.0;
        int firstCount = 0;
        for (int trial = 0; trial < r.trialsCompleted; ++trial)
        {
            const int pos = trial * neuronCount + neuron;
            if (pos < r.temporalFirstSpikeMsPerTrial.size())
            {
                const float first = r.temporalFirstSpikeMsPerTrial.at(pos);
                if (first >= 0.0f)
                {
                    firstSum += first;
                    ++firstCount;
                }
            }
        }

        ExperimentNeuronSummary summary;
        summary.rootId = trackedRootIds.at(neuron);
        summary.type = typeForRoot(summary.rootId);
        summary.meanHz = durationSeconds > 0.0
            ? static_cast<double>(totalCount) / (completedTrials * durationSeconds)
            : 0.0;
        summary.meanFirstSpikeMs = firstCount > 0 ? firstSum / firstCount : -1.0;
        summary.activationProbability = r.trialsCompleted > 0
            ? static_cast<double>(firstCount) / r.trialsCompleted
            : 0.0;
        out.neurons.append(summary);
    }

    out.frames.reserve(binCount);
    for (int bin = 0; bin < binCount; ++bin)
    {
        ExperimentFrame frame;
        frame.startMs = bin * r.temporalBinMs;
        frame.endMs = std::min(request.durationMs, frame.startMs + r.temporalBinMs);
        frame.neurons.reserve(neuronCount);

        for (int neuron = 0; neuron < neuronCount; ++neuron)
        {
            const quint32 count = r.temporalSpikeCounts.at(neuron * binCount + bin);
            ExperimentFrameNeuron state;
            state.rootId = trackedRootIds.at(neuron);
            state.rateHz = binSeconds > 0.0
                ? static_cast<double>(count) / (completedTrials * binSeconds)
                : 0.0;
            state.spiked = count > 0;
            frame.neurons.append(state);
        }
        out.frames.append(frame);
    }

    // A watched ablated neuron is expected to remain silent; keep the root IDs
    // in the result so playback can render the experimental configuration.
    for (int i = 0; i < out.ablatedRootIds.size(); ++i)
    {
        const std::uint64_t rootId = out.ablatedRootIds.at(i);
        if (!containsRoot(out.watchedRootIds, rootId))
        {
            // No temporal trace was requested for this neuron. This is valid:
            // it will still be displayed as ablated in the 3D scene.
        }
    }

    out.valid = true;
    return out;
}
}
