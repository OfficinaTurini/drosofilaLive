#include "ExperimentController.h"

#include "../core/Connectome.h"
#include "../model/CausalCircuit.h"
#include "../simulation/SimulationController.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace DrosofilaLive
{
namespace
{
const std::uint64_t kTargetRootId = 720575940660219265ULL;

bool frameRateForRoot(const ExperimentFrame& frame, std::uint64_t rootId, double* rate)
{
    for (int i = 0; i < frame.neurons.size(); ++i)
    {
        if (frame.neurons.at(i).rootId == rootId)
        {
            if (rate) *rate = frame.neurons.at(i).rateHz;
            return true;
        }
    }
    return false;
}

QString typeForRoot(std::uint64_t rootId)
{
    const std::vector<CausalNeuronSpec>& specs = validatedCausalCircuit();
    for (std::vector<CausalNeuronSpec>::const_iterator it = specs.begin(); it != specs.end(); ++it)
        if (it->rootId == rootId)
            return QString::fromLatin1(it->type);
    return QString::number(static_cast<qulonglong>(rootId));
}

}

ExperimentController::ExperimentController(QObject* parent)
    : QObject(parent),
      connectome_(new Connectome()),
      simulation_(new SimulationController(this)),
      runMode_(RunIdle)
{
    connect(simulation_, SIGNAL(simulationProgress(int,QString)),
            this, SLOT(forwardSimulationProgress(int,QString)));
    connect(simulation_, SIGNAL(finished()),
            this, SLOT(simulationFinished()));
}

ExperimentController::~ExperimentController()
{
    if (simulation_ && simulation_->isRunning())
        simulation_->wait();
    delete connectome_;
}

bool ExperimentController::loadConnectome(const QString& directory, QString* message)
{
    if (!connectome_ || isRunning())
    {
        if (message) *message = QStringLiteral("Experiment is currently running.");
        return false;
    }

    if (!connectome_->load(directory, message))
        return false;

    simulation_->setConnectome(connectome_);
    return true;
}

bool ExperimentController::isConnectomeLoaded() const
{
    return connectome_ && connectome_->isLoaded();
}

const Connectome* ExperimentController::readOnlyConnectome() const
{
    return connectome_;
}

bool ExperimentController::isRunning() const
{
    return simulation_ && simulation_->isRunning();
}

ConnectomeSummary ExperimentController::connectomeSummary() const
{
    ConnectomeSummary summary;
    if (!isConnectomeLoaded())
        return summary;

    summary.loaded = true;
    summary.neuronCount = connectome_->neuronCount();
    summary.edgeCount = connectome_->edgeCount();
    summary.synapseCount = connectome_->synapseCount();
    summary.mappedMiB = connectome_->mappedMiB();
    return summary;
}

ConnectomeNeuronInfo ExperimentController::neuronInfo(std::uint64_t rootId) const
{
    ConnectomeNeuronInfo info;
    if (!isConnectomeLoaded())
        return info;

    quint32 index = 0;
    if (!connectome_->indexFromRootId(static_cast<quint64>(rootId), &index))
        return info;

    const Connectome::Annotation annotation = connectome_->annotation(index);
    info.present = true;
    info.index = index;
    info.primaryType = annotation.primaryType;
    info.sign = connectome_->sign(index);
    info.fanIn = connectome_->fanIn(index);
    info.fanOut = connectome_->fanOut(index);
    info.synapsesIn = connectome_->synapsesIn(index);
    info.synapsesOut = connectome_->synapsesOut(index);
    return info;
}

CausalAnchorValidation ExperimentController::validateCausalCircuit() const
{
    CausalAnchorValidation out;
    const std::vector<CausalNeuronSpec>& specs = validatedCausalCircuit();
    out.expected = static_cast<int>(specs.size());

    if (!isConnectomeLoaded())
    {
        out.details = QStringLiteral("Connectome not loaded");
        return out;
    }

    QStringList lines;
    for (std::vector<CausalNeuronSpec>::const_iterator it = specs.begin(); it != specs.end(); ++it)
    {
        const ConnectomeNeuronInfo info = neuronInfo(it->rootId);
        if (!info.present)
        {
            lines << QStringLiteral("MISSING %1 [%2]")
                        .arg(QString::fromLatin1(it->type))
                        .arg(static_cast<qulonglong>(it->rootId));
            continue;
        }

        ++out.idsFound;
        const bool typeOk = info.primaryType.compare(QString::fromLatin1(it->type), Qt::CaseInsensitive) == 0;
        if (typeOk)
            ++out.labelsMatched;

        lines << QStringLiteral("%1 [%2] -> index %3, type=%4, sign=%5 %6")
                    .arg(QString::fromLatin1(it->type))
                    .arg(static_cast<qulonglong>(it->rootId))
                    .arg(info.index)
                    .arg(info.primaryType.isEmpty() ? QStringLiteral("-") : info.primaryType)
                    .arg(static_cast<int>(info.sign))
                    .arg(typeOk ? QStringLiteral("OK") : QStringLiteral("TYPE-MISMATCH"));
    }

    out.details = lines.join(QLatin1Char('\n'));
    return out;
}

bool ExperimentController::run(const ExperimentParameters& parameters)
{
    if (!simulation_ || !isConnectomeLoaded() || isRunning() || runMode_ != RunIdle)
        return false;

    runMode_ = RunSingle;
    if (!simulation_->startExperiment(parameters))
    {
        runMode_ = RunIdle;
        return false;
    }
    return true;
}

ExperimentResult ExperimentController::result() const
{
    return simulation_ ? simulation_->result() : ExperimentResult();
}

bool ExperimentController::runComparison(const ExperimentParameters& commonParameters,
                                         const QVector<std::uint64_t>& perturbedRootIds)
{
    if (!simulation_ || !isConnectomeLoaded() || isRunning() || runMode_ != RunIdle || perturbedRootIds.isEmpty())
        return false;

    comparisonResult_ = ComparisonResult();
    comparisonReferenceResult_ = ExperimentResult();

    comparisonReferenceParameters_ = commonParameters;
    comparisonReferenceParameters_.ablatedRootIds.clear();

    comparisonPerturbedParameters_ = commonParameters;
    comparisonPerturbedParameters_.ablatedRootIds = perturbedRootIds;

    comparisonResult_.perturbedRootIds = perturbedRootIds;
    runMode_ = RunComparisonReference;
    emit comparisonProgress(0, QStringLiteral("Reference"));

    if (!simulation_->startExperiment(comparisonReferenceParameters_))
    {
        runMode_ = RunIdle;
        return false;
    }
    return true;
}

ComparisonResult ExperimentController::comparisonResult() const
{
    return comparisonResult_;
}

bool ExperimentController::runEpistasis(const ExperimentParameters& commonParameters,
                                        std::uint64_t rootA,
                                        std::uint64_t rootB)
{
    if (!simulation_ || !isConnectomeLoaded() || isRunning() || runMode_ != RunIdle ||
        rootA == 0 || rootB == 0 || rootA == rootB)
        return false;

    quint32 indexA = 0;
    quint32 indexB = 0;
    if (!connectome_ ||
        !connectome_->indexFromRootId(static_cast<quint64>(rootA), &indexA) ||
        !connectome_->indexFromRootId(static_cast<quint64>(rootB), &indexB))
        return false;

    epistasisResult_ = EpistasisResult();
    epistasisWildTypeResult_ = ExperimentResult();
    epistasisAOffResult_ = ExperimentResult();
    epistasisBOffResult_ = ExperimentResult();

    epistasisResult_.rootA = rootA;
    epistasisResult_.rootB = rootB;
    epistasisResult_.typeA = typeForRoot(rootA);
    epistasisResult_.typeB = typeForRoot(rootB);

    epistasisWildTypeParameters_ = commonParameters;
    epistasisWildTypeParameters_.ablatedRootIds.clear();

    epistasisAOffParameters_ = commonParameters;
    epistasisAOffParameters_.ablatedRootIds.clear();
    epistasisAOffParameters_.ablatedRootIds.append(rootA);

    epistasisBOffParameters_ = commonParameters;
    epistasisBOffParameters_.ablatedRootIds.clear();
    epistasisBOffParameters_.ablatedRootIds.append(rootB);

    epistasisDoubleOffParameters_ = commonParameters;
    epistasisDoubleOffParameters_.ablatedRootIds.clear();
    epistasisDoubleOffParameters_.ablatedRootIds.append(rootA);
    epistasisDoubleOffParameters_.ablatedRootIds.append(rootB);

    runMode_ = RunEpistasisWildType;
    emit epistasisProgress(0, QStringLiteral("WT"));
    if (!simulation_->startExperiment(epistasisWildTypeParameters_))
    {
        runMode_ = RunIdle;
        return false;
    }
    return true;
}

EpistasisResult ExperimentController::epistasisResult() const
{
    return epistasisResult_;
}

void ExperimentController::forwardSimulationProgress(int percent, const QString& phase)
{
    const int p = std::max(0, std::min(100, percent));
    if (runMode_ == RunComparisonReference)
    {
        emit comparisonProgress(std::max(0, std::min(50, p / 2)),
                                QStringLiteral("Reference: %1").arg(phase));
        return;
    }
    if (runMode_ == RunComparisonPerturbed)
    {
        emit comparisonProgress(50 + std::max(0, std::min(50, p / 2)),
                                QStringLiteral("Perturbed: %1").arg(phase));
        return;
    }
    if (runMode_ == RunEpistasisWildType)
    {
        emit epistasisProgress(p / 4, QStringLiteral("WT: %1").arg(phase));
        return;
    }
    if (runMode_ == RunEpistasisAOff)
    {
        emit epistasisProgress(25 + p / 4, QStringLiteral("A OFF: %1").arg(phase));
        return;
    }
    if (runMode_ == RunEpistasisBOff)
    {
        emit epistasisProgress(50 + p / 4, QStringLiteral("B OFF: %1").arg(phase));
        return;
    }
    if (runMode_ == RunEpistasisDoubleOff)
    {
        emit epistasisProgress(75 + p / 4, QStringLiteral("A+B OFF: %1").arg(phase));
        return;
    }
    emit experimentProgress(p, phase);
}

void ExperimentController::simulationFinished()
{
    if (runMode_ == RunSingle)
    {
        runMode_ = RunIdle;
        emit experimentFinished();
        return;
    }

    if (runMode_ == RunComparisonReference)
    {
        comparisonReferenceResult_ = simulation_->result();
        if (!comparisonReferenceResult_.valid)
        {
            finishComparisonWithError(QStringLiteral("Reference failed: %1").arg(comparisonReferenceResult_.error));
            return;
        }

        runMode_ = RunComparisonPerturbed;
        emit comparisonProgress(50, QStringLiteral("Reference complete; starting perturbed run"));
        if (!simulation_->startExperiment(comparisonPerturbedParameters_))
        {
            finishComparisonWithError(QStringLiteral("Unable to start perturbed run."));
            return;
        }
        return;
    }

    if (runMode_ == RunComparisonPerturbed)
    {
        comparisonResult_.reference = comparisonReferenceResult_;
        comparisonResult_.perturbed = simulation_->result();
        if (!comparisonResult_.perturbed.valid)
        {
            finishComparisonWithError(QStringLiteral("Perturbed run failed: %1").arg(comparisonResult_.perturbed.error));
            return;
        }

        buildComparisonResult();
        runMode_ = RunIdle;
        emit comparisonProgress(100, QStringLiteral("Comparison complete"));
        emit comparisonFinished();
        return;
    }

    if (runMode_ == RunEpistasisWildType)
    {
        epistasisWildTypeResult_ = simulation_->result();
        if (!epistasisWildTypeResult_.valid)
        {
            finishEpistasisWithError(QStringLiteral("WT failed: %1").arg(epistasisWildTypeResult_.error));
            return;
        }
        if (!startEpistasisStage(RunEpistasisAOff))
            finishEpistasisWithError(QStringLiteral("Unable to start A OFF run."));
        return;
    }

    if (runMode_ == RunEpistasisAOff)
    {
        epistasisAOffResult_ = simulation_->result();
        if (!epistasisAOffResult_.valid)
        {
            finishEpistasisWithError(QStringLiteral("A OFF failed: %1").arg(epistasisAOffResult_.error));
            return;
        }
        if (!startEpistasisStage(RunEpistasisBOff))
            finishEpistasisWithError(QStringLiteral("Unable to start B OFF run."));
        return;
    }

    if (runMode_ == RunEpistasisBOff)
    {
        epistasisBOffResult_ = simulation_->result();
        if (!epistasisBOffResult_.valid)
        {
            finishEpistasisWithError(QStringLiteral("B OFF failed: %1").arg(epistasisBOffResult_.error));
            return;
        }
        if (!startEpistasisStage(RunEpistasisDoubleOff))
            finishEpistasisWithError(QStringLiteral("Unable to start A+B OFF run."));
        return;
    }

    if (runMode_ == RunEpistasisDoubleOff)
    {
        epistasisResult_.wildType = epistasisWildTypeResult_;
        epistasisResult_.aOff = epistasisAOffResult_;
        epistasisResult_.bOff = epistasisBOffResult_;
        epistasisResult_.doubleOff = simulation_->result();
        if (!epistasisResult_.doubleOff.valid)
        {
            finishEpistasisWithError(QStringLiteral("A+B OFF failed: %1").arg(epistasisResult_.doubleOff.error));
            return;
        }

        buildEpistasisResult();
        runMode_ = RunIdle;
        emit epistasisProgress(100, QStringLiteral("Epistasis complete"));
        emit epistasisFinished();
        return;
    }

    runMode_ = RunIdle;
}

void ExperimentController::finishComparisonWithError(const QString& error)
{
    comparisonResult_.valid = false;
    comparisonResult_.error = error;
    runMode_ = RunIdle;
    emit comparisonFinished();
}

void ExperimentController::buildComparisonResult()
{
    ComparisonResult& out = comparisonResult_;
    out.valid = false;
    out.error.clear();
    out.targetDeltaHz = out.perturbed.targetMeanHz - out.reference.targetMeanHz;
    out.targetRatio = std::fabs(out.reference.targetMeanHz) > 1e-12
        ? out.perturbed.targetMeanHz / out.reference.targetMeanHz
        : 0.0;

    const int commonFrames = std::min(out.reference.frames.size(), out.perturbed.frames.size());
    if (commonFrames <= 0)
    {
        out.error = QStringLiteral("Comparison has no temporal frames.");
        return;
    }

    int sustained = 0;
    for (int i = 0; i < commonFrames; ++i)
    {
        double referenceRate = 0.0;
        double perturbedRate = 0.0;
        const bool haveReference = frameRateForRoot(out.reference.frames.at(i), kTargetRootId, &referenceRate);
        const bool havePerturbed = frameRateForRoot(out.perturbed.frames.at(i), kTargetRootId, &perturbedRate);
        if (!haveReference || !havePerturbed)
        {
            out.error = QStringLiteral("TARGET/MN9 temporal trace is missing from comparison.");
            return;
        }

        const double delta = perturbedRate - referenceRate;
        if (std::fabs(delta) >= out.sustainedThresholdHz)
            ++sustained;
        else
            sustained = 0;

        if (sustained >= out.sustainedBins)
        {
            const int first = i - out.sustainedBins + 1;
            out.sustainedOnsetMs = out.reference.frames.at(first).startMs;
            break;
        }
    }

    out.valid = true;
}

bool ExperimentController::startEpistasisStage(RunMode mode)
{
    const ExperimentParameters* parameters = 0;
    QString phase;
    switch (mode)
    {
    case RunEpistasisAOff:
        parameters = &epistasisAOffParameters_;
        phase = QStringLiteral("WT complete; starting A OFF");
        break;
    case RunEpistasisBOff:
        parameters = &epistasisBOffParameters_;
        phase = QStringLiteral("A OFF complete; starting B OFF");
        break;
    case RunEpistasisDoubleOff:
        parameters = &epistasisDoubleOffParameters_;
        phase = QStringLiteral("B OFF complete; starting A+B OFF");
        break;
    default:
        return false;
    }

    runMode_ = mode;
    const int base = mode == RunEpistasisAOff ? 25 : (mode == RunEpistasisBOff ? 50 : 75);
    emit epistasisProgress(base, phase);
    return simulation_->startExperiment(*parameters);
}

void ExperimentController::finishEpistasisWithError(const QString& error)
{
    epistasisResult_.valid = false;
    epistasisResult_.error = error;
    runMode_ = RunIdle;
    emit epistasisFinished();
}

void ExperimentController::buildEpistasisResult()
{
    EpistasisResult& out = epistasisResult_;
    out.valid = false;
    out.error.clear();

    const double wt = out.wildType.targetMeanHz;
    const double a = out.aOff.targetMeanHz;
    const double b = out.bOff.targetMeanHz;
    const double ab = out.doubleOff.targetMeanHz;

    out.aDeltaHz = a - wt;
    out.bDeltaHz = b - wt;
    out.doubleDeltaHz = ab - wt;

    // Additive null model in output-rate space:
    // expected(A+B OFF) = WT + (A OFF - WT) + (B OFF - WT)
    //                   = A OFF + B OFF - WT.
    out.expectedAdditiveHz = a + b - wt;
    out.interactionHz = ab - out.expectedAdditiveHz;
    out.valid = true;
}
}
