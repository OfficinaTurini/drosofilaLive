#include "StatisticsController.h"

#include "../experiment/ExperimentController.h"
#include "../model/CausalCircuit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace DrosofilaLive
{
namespace
{
QString typeForRoot(std::uint64_t rootId)
{
    const std::vector<CausalNeuronSpec>& specs = validatedCausalCircuit();
    for (std::vector<CausalNeuronSpec>::const_iterator it = specs.begin(); it != specs.end(); ++it)
        if (it->rootId == rootId)
            return QString::fromLatin1(it->type);
    return QStringLiteral("?");
}

double tCritical95(int df)
{
    // Two-sided 95% Student-t critical values. Exact table for df 1..30;
    // conservative/asymptotic anchors above 30.
    static const double t1to30[] = {
        0.0,
        12.706, 4.303, 3.182, 2.776, 2.571,
        2.447, 2.365, 2.306, 2.262, 2.228,
        2.201, 2.179, 2.160, 2.145, 2.131,
        2.120, 2.110, 2.101, 2.093, 2.086,
        2.080, 2.074, 2.069, 2.064, 2.060,
        2.056, 2.052, 2.048, 2.045, 2.042
    };
    if (df <= 0) return 0.0;
    if (df <= 30) return t1to30[df];
    if (df <= 40) return 2.021;
    if (df <= 60) return 2.000;
    if (df <= 120) return 1.980;
    return 1.960;
}
}

StatisticsController::StatisticsController(ExperimentController* experiment, QObject* parent)
    : QObject(parent),
      experiment_(experiment),
      rootA_(0),
      rootB_(0),
      replicatesRequested_(0),
      replicateIndex_(0),
      running_(false),
      stopRequested_(false)
{
    if (experiment_)
    {
        connect(experiment_, SIGNAL(epistasisProgress(int,QString)),
                this, SLOT(epistasisProgress(int,QString)));
        connect(experiment_, SIGNAL(epistasisFinished()),
                this, SLOT(epistasisFinished()));
    }
}

bool StatisticsController::isRunning() const
{
    return running_;
}

void StatisticsController::requestStop()
{
    if (running_) stopRequested_ = true;
}

bool StatisticsController::runEpistasisValidation(const ExperimentParameters& parameters,
                                                   std::uint64_t rootA,
                                                   std::uint64_t rootB,
                                                   int replicates)
{
    if (!experiment_ || running_ || experiment_->isRunning() || !experiment_->isConnectomeLoaded() ||
        rootA == 0 || rootB == 0 || rootA == rootB || replicates <= 0)
        return false;

    const quint64 lastSeed = static_cast<quint64>(parameters.randomSeed) + static_cast<quint64>(replicates - 1);
    if (lastSeed > static_cast<quint64>(std::numeric_limits<quint32>::max()))
        return false;

    baseParameters_ = parameters;
    rootA_ = rootA;
    rootB_ = rootB;
    replicatesRequested_ = replicates;
    replicateIndex_ = 0;
    running_ = true;
    stopRequested_ = false;
    result_ = StatisticalEpistasisResult();
    result_.rootA = rootA;
    result_.rootB = rootB;
    result_.typeA = typeForRoot(rootA);
    result_.typeB = typeForRoot(rootB);
    if (result_.typeA == QStringLiteral("?") && experiment_)
    {
        const ConnectomeNeuronInfo info = experiment_->neuronInfo(rootA);
        if (info.present && !info.primaryType.trimmed().isEmpty()) result_.typeA = info.primaryType.trimmed();
    }
    if (result_.typeB == QStringLiteral("?") && experiment_)
    {
        const ConnectomeNeuronInfo info = experiment_->neuronInfo(rootB);
        if (info.present && !info.primaryType.trimmed().isEmpty()) result_.typeB = info.primaryType.trimmed();
    }
    result_.baseSeed = parameters.randomSeed;
    result_.trialsPerReplicate = parameters.trials;
    result_.replicatesRequested = replicates;

    emit progress(0, QStringLiteral("Starting replicate 1/%1").arg(replicatesRequested_));
    if (!startCurrentReplicate())
    {
        running_ = false;
        result_.error = QStringLiteral("Unable to start first statistical replicate.");
        return false;
    }
    return true;
}

StatisticalEpistasisResult StatisticsController::result() const
{
    return result_;
}

bool StatisticsController::startCurrentReplicate()
{
    if (!running_ || !experiment_ || replicateIndex_ < 0 || replicateIndex_ >= replicatesRequested_)
        return false;

    ExperimentParameters p = baseParameters_;
    p.randomSeed = static_cast<quint32>(static_cast<quint64>(baseParameters_.randomSeed) +
                                       static_cast<quint64>(replicateIndex_));
    return experiment_->runEpistasis(p, rootA_, rootB_);
}

void StatisticsController::epistasisProgress(int percent, const QString& phase)
{
    if (!running_ || replicatesRequested_ <= 0)
        return;

    const int p = std::max(0, std::min(100, percent));
    const int overall = std::max(0, std::min(100,
        static_cast<int>((replicateIndex_ * 100 + p) / replicatesRequested_)));
    emit progress(overall,
                  QStringLiteral("Replicate %1/%2 | %3")
                      .arg(replicateIndex_ + 1)
                      .arg(replicatesRequested_)
                      .arg(phase));
}

void StatisticsController::epistasisFinished()
{
    if (!running_ || !experiment_)
        return;

    const EpistasisResult e = experiment_->epistasisResult();
    if (!e.valid)
    {
        completeWithError(QStringLiteral("Replicate %1 failed: %2")
                              .arg(replicateIndex_ + 1)
                              .arg(e.error));
        return;
    }

    StatisticalEpistasisReplicate r;
    r.seed = static_cast<quint32>(static_cast<quint64>(baseParameters_.randomSeed) +
                                 static_cast<quint64>(replicateIndex_));
    r.wildTypeHz = e.wildType.targetMeanHz;
    r.aOffHz = e.aOff.targetMeanHz;
    r.bOffHz = e.bOff.targetMeanHz;
    r.doubleOffHz = e.doubleOff.targetMeanHz;
    r.expectedAdditiveHz = e.expectedAdditiveHz;
    r.interactionHz = e.interactionHz;
    r.wildTypeLatencyMs = e.wildType.targetMeanLatencyMs;
    r.aOffLatencyMs = e.aOff.targetMeanLatencyMs;
    r.bOffLatencyMs = e.bOff.targetMeanLatencyMs;
    r.doubleOffLatencyMs = e.doubleOff.targetMeanLatencyMs;
    result_.replicates.append(r);
    result_.lastEpistasis = e;
    result_.replicatesCompleted = result_.replicates.size();

    ++replicateIndex_;
    if (stopRequested_)
    {
        completeWithError(QStringLiteral("Statistical validation stopped after %1/%2 completed replicates.")
                              .arg(result_.replicatesCompleted).arg(replicatesRequested_));
        return;
    }
    if (replicateIndex_ >= replicatesRequested_)
    {
        finalize();
        return;
    }

    emit progress(static_cast<int>((replicateIndex_ * 100) / replicatesRequested_),
                  QStringLiteral("Starting replicate %1/%2")
                      .arg(replicateIndex_ + 1)
                      .arg(replicatesRequested_));
    if (!startCurrentReplicate())
        completeWithError(QStringLiteral("Unable to start replicate %1.").arg(replicateIndex_ + 1));
}

void StatisticsController::completeWithError(const QString& error)
{
    result_.valid = false;
    result_.error = error;
    running_ = false;
    emit finished();
}

StatisticalSummary StatisticsController::summarize(const QVector<double>& values)
{
    StatisticalSummary s;
    s.n = values.size();
    if (values.isEmpty())
        return s;

    double sum = 0.0;
    for (int i = 0; i < values.size(); ++i)
        sum += values.at(i);
    s.mean = sum / values.size();

    for (int i = 0; i < values.size(); ++i)
    {
        const double v = values.at(i);
        const double d = v - s.mean;
        s.sumSquaredDeviation += d * d;
        if (v > 1e-12) ++s.positiveCount;
        else if (v < -1e-12) ++s.negativeCount;
        else ++s.zeroCount;
    }

    if (values.size() > 1)
    {
        s.uncertaintyAvailable = true;
        s.stddev = std::sqrt(s.sumSquaredDeviation / (values.size() - 1));
        s.sem = s.stddev / std::sqrt(static_cast<double>(values.size()));
        const double t = tCritical95(values.size() - 1);
        s.ci95Low = s.mean - t * s.sem;
        s.ci95High = s.mean + t * s.sem;
    }
    else
    {
        s.stddev = 0.0;
        s.sem = 0.0;
        s.ci95Low = s.mean;
        s.ci95High = s.mean;
    }

    int matching = 0;
    if (s.mean > 1e-12) matching = s.positiveCount;
    else if (s.mean < -1e-12) matching = s.negativeCount;
    else matching = std::max(s.positiveCount, s.negativeCount);
    s.signConsistency = values.isEmpty() ? 0.0 : static_cast<double>(matching) / values.size();
    return s;
}

void StatisticsController::finalize()
{
    QVector<double> wt;
    QVector<double> a;
    QVector<double> b;
    QVector<double> ab;
    QVector<double> interaction;
    wt.reserve(result_.replicates.size());
    a.reserve(result_.replicates.size());
    b.reserve(result_.replicates.size());
    ab.reserve(result_.replicates.size());
    interaction.reserve(result_.replicates.size());

    for (int i = 0; i < result_.replicates.size(); ++i)
    {
        const StatisticalEpistasisReplicate& r = result_.replicates.at(i);
        wt.append(r.wildTypeHz);
        a.append(r.aOffHz);
        b.append(r.bOffHz);
        ab.append(r.doubleOffHz);
        interaction.append(r.interactionHz);
    }

    result_.wildType = summarize(wt);
    result_.aOff = summarize(a);
    result_.bOff = summarize(b);
    result_.doubleOff = summarize(ab);
    result_.interaction = summarize(interaction);
    result_.valid = result_.replicatesCompleted == result_.replicatesRequested && result_.replicatesCompleted > 0;
    if (!result_.valid && result_.error.isEmpty())
        result_.error = QStringLiteral("Statistical validation completed only %1/%2 replicates.")
                            .arg(result_.replicatesCompleted).arg(result_.replicatesRequested);

    running_ = false;
    emit progress(100, QStringLiteral("Statistical validation complete"));
    emit finished();
}
}
