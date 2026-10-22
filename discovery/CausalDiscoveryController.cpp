#include "CausalDiscoveryController.h"

#include "../experiment/ExperimentController.h"
#include "../statistics/StatisticsController.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <limits>

namespace DrosofilaLive
{
namespace
{
const std::uint64_t kTargetRootId = 720575940660219265ULL;
const char* kTargetType = "CB0701";

bool containsRoot(const QVector<std::uint64_t>& roots, std::uint64_t rootId)
{
    for (int i = 0; i < roots.size(); ++i)
        if (roots.at(i) == rootId)
            return true;
    return false;
}
}

CausalDiscoveryController::CausalDiscoveryController(ExperimentController* experiment, QObject* parent)
    : QObject(parent),
      experiment_(experiment),
      candidateIndex_(0),
      screenReplicateIndex_(0),
      screenReplicatesRequested_(1),
      validationCandidateIndex_(0),
      validationReplicateIndex_(0),
      validationReplicatesRequested_(0),
      epistasisPairIndex_(0),
      epistasisReplicateIndex_(0),
      epistasisReplicatesRequested_(0),
      running_(false),
      stopRequested_(false),
      operation_(CausalDiscoveryNone),
      lastOperation_(CausalDiscoveryNone)
{
    if (experiment_)
    {
        connect(experiment_, SIGNAL(comparisonProgress(int,QString)),
                this, SLOT(comparisonProgress(int,QString)));
        connect(experiment_, SIGNAL(comparisonFinished()),
                this, SLOT(comparisonFinished()));
        connect(experiment_, SIGNAL(epistasisProgress(int,QString)),
                this, SLOT(epistasisProgress(int,QString)));
        connect(experiment_, SIGNAL(epistasisFinished()),
                this, SLOT(epistasisFinished()));
    }
}

bool CausalDiscoveryController::isRunning() const
{
    return running_;
}

CausalDiscoveryOperation CausalDiscoveryController::operation() const
{
    return operation_;
}

CausalDiscoveryOperation CausalDiscoveryController::lastOperation() const
{
    return lastOperation_;
}

void CausalDiscoveryController::requestStop()
{
    if (running_)
        stopRequested_ = true;
}

void CausalDiscoveryController::clearResult()
{
    if (running_) return;
    result_ = CausalDiscoveryResult();
    candidates_.clear();
    validationEntryIndices_.clear();
    epistasisPairs_.clear();
    lastOperation_ = CausalDiscoveryNone;
}

bool CausalDiscoveryController::restoreResult(const CausalDiscoveryResult& result)
{
    if (running_) return false;
    result_ = result;
    candidates_.clear();
    validationEntryIndices_.clear();
    epistasisPairs_.clear();
    candidateIndex_ = 0;
    screenReplicateIndex_ = 0;
    validationCandidateIndex_ = 0;
    validationReplicateIndex_ = 0;
    epistasisPairIndex_ = 0;
    epistasisReplicateIndex_ = 0;
    stopRequested_ = false;
    operation_ = CausalDiscoveryNone;
    if (!result_.epistasisEntries.isEmpty()) lastOperation_ = CausalDiscoveryEpistasis;
    else if (result_.validationCandidatesRequested > 0) lastOperation_ = CausalDiscoveryValidation;
    else if (!result_.entries.isEmpty()) lastOperation_ = CausalDiscoveryScreen;
    else lastOperation_ = CausalDiscoveryNone;
    return true;
}

void CausalDiscoveryController::setProvenance(const CausalDiscoveryProvenance& provenance)
{
    result_.provenance = provenance;
}

bool CausalDiscoveryController::runScreen(const ExperimentParameters& parameters,
                                          const QVector<CausalDiscoveryCandidate>& candidates,
                                          int replicates)
{
    if (!experiment_ || running_ || experiment_->isRunning() || !experiment_->isConnectomeLoaded() || replicates <= 0)
        return false;

    const quint64 lastSeed = static_cast<quint64>(parameters.randomSeed) + static_cast<quint64>(replicates - 1);
    if (lastSeed > static_cast<quint64>(std::numeric_limits<quint32>::max()))
        return false;

    baseParameters_ = parameters;
    baseParameters_.ablatedRootIds.clear();
    if (!containsRoot(baseParameters_.watchedRootIds, kTargetRootId))
        baseParameters_.watchedRootIds.append(kTargetRootId);

    candidates_.clear();
    QVector<std::uint64_t> seen;
    for (int i = 0; i < candidates.size(); ++i)
    {
        const CausalDiscoveryCandidate& candidate = candidates.at(i);
        if (candidate.rootId == 0 || candidate.rootId == kTargetRootId || containsRoot(seen, candidate.rootId))
            continue;
        candidates_.append(candidate);
        seen.append(candidate.rootId);
    }

    if (candidates_.isEmpty())
        return false;

    result_ = CausalDiscoveryResult();
    result_.targetRootId = kTargetRootId;
    result_.targetType = QString::fromLatin1(kTargetType);
    result_.candidatesRequested = candidates_.size();
    result_.screenReplicatesRequested = replicates;
    result_.parameters = baseParameters_;

    candidateIndex_ = 0;
    screenReplicateIndex_ = 0;
    screenReplicatesRequested_ = replicates;
    screenReferenceHz_.clear();
    screenPerturbedHz_.clear();
    screenDeltaHz_.clear();
    stopRequested_ = false;
    running_ = true;
    operation_ = CausalDiscoveryScreen;
    lastOperation_ = CausalDiscoveryScreen;

    emit progress(0, QStringLiteral("Preparing candidate 1/%1, screen replicate 1/%2").arg(candidates_.size()).arg(replicates));
    if (!startCurrentScreenReplicate())
    {
        result_.valid = false;
        result_.error = QStringLiteral("Unable to start the first causal comparison.");
        running_ = false;
        operation_ = CausalDiscoveryNone;
        return false;
    }
    return true;
}

bool CausalDiscoveryController::runValidation(const ExperimentParameters& parameters,
                                              const QVector<std::uint64_t>& rootIds,
                                              int replicates)
{
    if (!experiment_ || running_ || experiment_->isRunning() || !experiment_->isConnectomeLoaded() ||
        rootIds.isEmpty() || replicates <= 0 || result_.entries.isEmpty())
        return false;

    const quint64 lastSeed = static_cast<quint64>(parameters.randomSeed) + static_cast<quint64>(replicates - 1);
    if (lastSeed > static_cast<quint64>(std::numeric_limits<quint32>::max()))
        return false;

    baseParameters_ = parameters;
    baseParameters_.ablatedRootIds.clear();
    if (!containsRoot(baseParameters_.watchedRootIds, kTargetRootId))
        baseParameters_.watchedRootIds.append(kTargetRootId);

    validationEntryIndices_.clear();
    QVector<std::uint64_t> seen;
    for (int i = 0; i < rootIds.size(); ++i)
    {
        const std::uint64_t rootId = rootIds.at(i);
        if (rootId == 0 || rootId == kTargetRootId || containsRoot(seen, rootId))
            continue;
        const int entryIndex = entryIndexForRoot(rootId);
        if (entryIndex < 0 || !result_.entries.at(entryIndex).valid)
            continue;
        validationEntryIndices_.append(entryIndex);
        seen.append(rootId);
    }
    if (validationEntryIndices_.isEmpty())
        return false;

    for (int i = 0; i < validationEntryIndices_.size(); ++i)
    {
        CausalDiscoveryEntry& entry = result_.entries[validationEntryIndices_.at(i)];
        entry.validationAttempted = true;
        entry.validationValid = false;
        entry.validationError.clear();
        entry.validationReplicatesRequested = replicates;
        entry.validationReplicatesCompleted = 0;
        entry.validationReference = StatisticalSummary();
        entry.validationPerturbed = StatisticalSummary();
        entry.validationDelta = StatisticalSummary();
    }

    result_.stopped = false;
    result_.error.clear();
    result_.epistasisEntries.clear();
    result_.epistasisPairsRequested = 0;
    result_.epistasisPairsCompleted = 0;
    result_.epistasisReplicatesRequested = 0;
    result_.epistasisRootIds.clear();
    epistasisPairs_.clear();
    result_.validationCandidatesRequested = validationEntryIndices_.size();
    result_.validationCandidatesCompleted = 0;
    result_.validationReplicatesRequested = replicates;
    result_.validationRootIds.clear();
    for (int i = 0; i < validationEntryIndices_.size(); ++i)
        result_.validationRootIds.append(result_.entries.at(validationEntryIndices_.at(i)).candidate.rootId);
    result_.parameters = baseParameters_;

    validationCandidateIndex_ = 0;
    validationReplicateIndex_ = 0;
    validationReplicatesRequested_ = replicates;
    validationReferenceHz_.clear();
    validationPerturbedHz_.clear();
    validationDeltaHz_.clear();
    stopRequested_ = false;
    running_ = true;
    operation_ = CausalDiscoveryValidation;
    lastOperation_ = CausalDiscoveryValidation;

    emit progress(0, QStringLiteral("Validating candidate 1/%1, replicate 1/%2")
                      .arg(validationEntryIndices_.size()).arg(replicates));
    if (!startCurrentValidationReplicate())
    {
        running_ = false;
        operation_ = CausalDiscoveryNone;
        result_.error = QStringLiteral("Unable to start the first validation replicate.");
        return false;
    }
    return true;
}

bool CausalDiscoveryController::runEpistasis(const ExperimentParameters& parameters,
                                             const QVector<std::uint64_t>& rootIds,
                                             int replicates)
{
    if (!experiment_ || running_ || experiment_->isRunning() || !experiment_->isConnectomeLoaded() ||
        rootIds.size() < 2 || replicates <= 0 || result_.entries.isEmpty())
        return false;

    const quint64 lastSeed = static_cast<quint64>(parameters.randomSeed) + static_cast<quint64>(replicates - 1);
    if (lastSeed > static_cast<quint64>(std::numeric_limits<quint32>::max()))
        return false;

    baseParameters_ = parameters;
    baseParameters_.ablatedRootIds.clear();
    if (!containsRoot(baseParameters_.watchedRootIds, kTargetRootId))
        baseParameters_.watchedRootIds.append(kTargetRootId);

    QVector<int> entryIndices;
    QVector<std::uint64_t> seen;
    for (int i = 0; i < rootIds.size(); ++i)
    {
        const std::uint64_t rootId = rootIds.at(i);
        if (rootId == 0 || rootId == kTargetRootId || containsRoot(seen, rootId))
            continue;
        const int entryIndex = entryIndexForRoot(rootId);
        if (entryIndex < 0 || !containsRoot(result_.validationRootIds, rootId) ||
            !result_.entries.at(entryIndex).validationValid)
            continue;
        entryIndices.append(entryIndex);
        seen.append(rootId);
    }
    if (entryIndices.size() < 2)
        return false;

    epistasisPairs_.clear();
    for (int a = 0; a < entryIndices.size(); ++a)
    {
        const CausalDiscoveryEntry& ea = result_.entries.at(entryIndices.at(a));
        for (int b = a + 1; b < entryIndices.size(); ++b)
        {
            const CausalDiscoveryEntry& eb = result_.entries.at(entryIndices.at(b));
            CausalDiscoveryEpistasisEntry pair;
            pair.rootA = ea.candidate.rootId;
            pair.rootB = eb.candidate.rootId;
            pair.typeA = ea.candidate.type;
            pair.typeB = eb.candidate.type;
            pair.causalRankA = ea.causalRank;
            pair.causalRankB = eb.causalRank;
            pair.validatedDeltaAHz = ea.validationDelta.mean;
            pair.validatedDeltaBHz = eb.validationDelta.mean;
            pair.replicatesRequested = replicates;
            epistasisPairs_.append(pair);
        }
    }
    if (epistasisPairs_.isEmpty())
        return false;

    result_.stopped = false;
    result_.error.clear();
    result_.epistasisEntries.clear();
    result_.epistasisPairsRequested = epistasisPairs_.size();
    result_.epistasisPairsCompleted = 0;
    result_.epistasisReplicatesRequested = replicates;
    result_.epistasisRootIds = seen;
    result_.parameters = baseParameters_;

    epistasisPairIndex_ = 0;
    epistasisReplicateIndex_ = 0;
    epistasisReplicatesRequested_ = replicates;
    epistasisWildTypeHz_.clear();
    epistasisAOffHz_.clear();
    epistasisBOffHz_.clear();
    epistasisDoubleOffHz_.clear();
    epistasisExpectedHz_.clear();
    epistasisInteractionHz_.clear();
    stopRequested_ = false;
    running_ = true;
    operation_ = CausalDiscoveryEpistasis;
    lastOperation_ = CausalDiscoveryEpistasis;

    emit progress(0, QStringLiteral("Epistasis pair 1/%1, replicate 1/%2")
                      .arg(epistasisPairs_.size()).arg(replicates));
    if (!startCurrentEpistasisReplicate())
    {
        running_ = false;
        operation_ = CausalDiscoveryNone;
        result_.error = QStringLiteral("Unable to start the first discovery epistasis replicate.");
        return false;
    }
    return true;
}

CausalDiscoveryResult CausalDiscoveryController::result() const
{
    return result_;
}

int CausalDiscoveryController::entryIndexForRoot(std::uint64_t rootId) const
{
    for (int i = 0; i < result_.entries.size(); ++i)
        if (result_.entries.at(i).candidate.rootId == rootId)
            return i;
    return -1;
}

bool CausalDiscoveryController::startCurrentScreenReplicate()
{
    if (!running_ || operation_ != CausalDiscoveryScreen || !experiment_ ||
        candidateIndex_ < 0 || candidateIndex_ >= candidates_.size() ||
        screenReplicateIndex_ < 0 || screenReplicateIndex_ >= screenReplicatesRequested_)
        return false;

    const CausalDiscoveryCandidate& candidate = candidates_.at(candidateIndex_);
    QVector<std::uint64_t> perturbed;
    perturbed.append(candidate.rootId);

    ExperimentParameters parameters = baseParameters_;
    parameters.randomSeed = static_cast<quint32>(static_cast<quint64>(baseParameters_.randomSeed) +
                                                 static_cast<quint64>(screenReplicateIndex_));
    if (!containsRoot(parameters.watchedRootIds, candidate.rootId))
        parameters.watchedRootIds.append(candidate.rootId);

    return experiment_->runComparison(parameters, perturbed);
}

void CausalDiscoveryController::finalizeScreenCandidate(bool complete)
{
    if (candidateIndex_ < 0 || candidateIndex_ >= candidates_.size())
        return;

    CausalDiscoveryEntry entry;
    entry.candidate = candidates_.at(candidateIndex_);
    entry.comparison = experiment_ ? experiment_->comparisonResult() : ComparisonResult();
    entry.screenReplicatesRequested = screenReplicatesRequested_;
    entry.screenReplicatesCompleted = screenDeltaHz_.size();
    entry.screenReference = StatisticsController::summarize(screenReferenceHz_);
    entry.screenPerturbed = StatisticsController::summarize(screenPerturbedHz_);
    entry.screenDelta = StatisticsController::summarize(screenDeltaHz_);
    entry.valid = complete && entry.screenReplicatesCompleted == screenReplicatesRequested_ &&
                  entry.screenReplicatesCompleted > 0;
    if (!entry.valid)
    {
        if (entry.comparison.valid == false && !entry.comparison.error.isEmpty())
            entry.error = entry.comparison.error;
        else
            entry.error = QStringLiteral("Screen incomplete: %1/%2 replicate(s).")
                .arg(entry.screenReplicatesCompleted).arg(screenReplicatesRequested_);
    }
    result_.entries.append(entry);
    result_.candidatesCompleted = result_.entries.size();
    emit candidateFinished(entry, result_.candidatesCompleted, candidates_.size());
}

void CausalDiscoveryController::advanceScreenCandidate()
{
    ++candidateIndex_;
    if (candidateIndex_ >= candidates_.size())
    {
        finalizeScreen();
        return;
    }

    screenReplicateIndex_ = 0;
    screenReferenceHz_.clear();
    screenPerturbedHz_.clear();
    screenDeltaHz_.clear();

    if (!startCurrentScreenReplicate())
    {
        finalizeScreenCandidate(false);
        advanceScreenCandidate();
    }
}

bool CausalDiscoveryController::startCurrentValidationReplicate()
{
    if (!running_ || operation_ != CausalDiscoveryValidation || !experiment_ ||
        validationCandidateIndex_ < 0 || validationCandidateIndex_ >= validationEntryIndices_.size() ||
        validationReplicateIndex_ < 0 || validationReplicateIndex_ >= validationReplicatesRequested_)
        return false;

    const int entryIndex = validationEntryIndices_.at(validationCandidateIndex_);
    if (entryIndex < 0 || entryIndex >= result_.entries.size())
        return false;
    const CausalDiscoveryEntry& entry = result_.entries.at(entryIndex);

    ExperimentParameters parameters = baseParameters_;
    parameters.randomSeed = static_cast<quint32>(static_cast<quint64>(baseParameters_.randomSeed) +
                                                 static_cast<quint64>(validationReplicateIndex_));
    if (!containsRoot(parameters.watchedRootIds, entry.candidate.rootId))
        parameters.watchedRootIds.append(entry.candidate.rootId);

    QVector<std::uint64_t> perturbed;
    perturbed.append(entry.candidate.rootId);
    return experiment_->runComparison(parameters, perturbed);
}

bool CausalDiscoveryController::startCurrentEpistasisReplicate()
{
    if (!running_ || operation_ != CausalDiscoveryEpistasis || !experiment_ ||
        epistasisPairIndex_ < 0 || epistasisPairIndex_ >= epistasisPairs_.size() ||
        epistasisReplicateIndex_ < 0 || epistasisReplicateIndex_ >= epistasisReplicatesRequested_)
        return false;

    const CausalDiscoveryEpistasisEntry& pair = epistasisPairs_.at(epistasisPairIndex_);
    ExperimentParameters parameters = baseParameters_;
    parameters.randomSeed = static_cast<quint32>(static_cast<quint64>(baseParameters_.randomSeed) +
                                                 static_cast<quint64>(epistasisReplicateIndex_));
    if (!containsRoot(parameters.watchedRootIds, pair.rootA))
        parameters.watchedRootIds.append(pair.rootA);
    if (!containsRoot(parameters.watchedRootIds, pair.rootB))
        parameters.watchedRootIds.append(pair.rootB);
    return experiment_->runEpistasis(parameters, pair.rootA, pair.rootB);
}

void CausalDiscoveryController::comparisonProgress(int percent, const QString& phase)
{
    if (!running_)
        return;

    if (operation_ == CausalDiscoveryScreen)
    {
        if (candidates_.isEmpty() || screenReplicatesRequested_ <= 0) return;
        const int totalComparisons = candidates_.size() * screenReplicatesRequested_;
        const int completedBefore = candidateIndex_ * screenReplicatesRequested_ + screenReplicateIndex_;
        const int overall = std::max(0, std::min(99,
            static_cast<int>(std::floor(100.0 * (completedBefore + percent / 100.0) / totalComparisons))));
        const CausalDiscoveryCandidate& candidate = candidates_.at(candidateIndex_);
        emit progress(overall,
                      QStringLiteral("Screen %1/%2 %3 [%4] | replicate %5/%6 - %7")
                          .arg(candidateIndex_ + 1)
                          .arg(candidates_.size())
                          .arg(candidate.type.isEmpty() ? QStringLiteral("unannotated") : candidate.type)
                          .arg(static_cast<qulonglong>(candidate.rootId))
                          .arg(screenReplicateIndex_ + 1)
                          .arg(screenReplicatesRequested_)
                          .arg(phase));
        return;
    }

    if (operation_ == CausalDiscoveryValidation && !validationEntryIndices_.isEmpty())
    {
        const int totalComparisons = validationEntryIndices_.size() * validationReplicatesRequested_;
        const int completedBefore = validationCandidateIndex_ * validationReplicatesRequested_ + validationReplicateIndex_;
        const int overall = std::max(0, std::min(99,
            static_cast<int>(std::floor(100.0 * (completedBefore + percent / 100.0) / totalComparisons))));
        const CausalDiscoveryEntry& entry = result_.entries.at(validationEntryIndices_.at(validationCandidateIndex_));
        emit progress(overall,
                      QStringLiteral("Validate %1/%2 %3 [%4] | replicate %5/%6 - %7")
                          .arg(validationCandidateIndex_ + 1)
                          .arg(validationEntryIndices_.size())
                          .arg(entry.candidate.type.isEmpty() ? QStringLiteral("unannotated") : entry.candidate.type)
                          .arg(static_cast<qulonglong>(entry.candidate.rootId))
                          .arg(validationReplicateIndex_ + 1)
                          .arg(validationReplicatesRequested_)
                          .arg(phase));
    }
}

void CausalDiscoveryController::epistasisProgress(int percent, const QString& phase)
{
    if (!running_ || operation_ != CausalDiscoveryEpistasis || epistasisPairs_.isEmpty() ||
        epistasisReplicatesRequested_ <= 0)
        return;

    const int p = std::max(0, std::min(100, percent));
    const int totalRuns = epistasisPairs_.size() * epistasisReplicatesRequested_;
    const int completedBefore = epistasisPairIndex_ * epistasisReplicatesRequested_ + epistasisReplicateIndex_;
    const int overall = std::max(0, std::min(99,
        static_cast<int>(std::floor(100.0 * (completedBefore + p / 100.0) / totalRuns))));
    const CausalDiscoveryEpistasisEntry& pair = epistasisPairs_.at(epistasisPairIndex_);
    emit progress(overall,
                  QStringLiteral("Epistasis %1/%2 %3 [%4] x %5 [%6] | replicate %7/%8 - %9")
                      .arg(epistasisPairIndex_ + 1)
                      .arg(epistasisPairs_.size())
                      .arg(pair.typeA.isEmpty() ? QStringLiteral("unannotated") : pair.typeA)
                      .arg(static_cast<qulonglong>(pair.rootA))
                      .arg(pair.typeB.isEmpty() ? QStringLiteral("unannotated") : pair.typeB)
                      .arg(static_cast<qulonglong>(pair.rootB))
                      .arg(epistasisReplicateIndex_ + 1)
                      .arg(epistasisReplicatesRequested_)
                      .arg(phase));
}

void CausalDiscoveryController::comparisonFinished()
{
    if (!running_ || !experiment_)
        return;

    if (operation_ == CausalDiscoveryScreen)
    {
        if (candidateIndex_ < 0 || candidateIndex_ >= candidates_.size()) return;

        const ComparisonResult comparison = experiment_->comparisonResult();
        if (!comparison.valid)
        {
            finalizeScreenCandidate(false);
            if (stopRequested_)
            {
                result_.stopped = true;
                finalizeScreen();
            }
            else
            {
                advanceScreenCandidate();
            }
            return;
        }

        screenReferenceHz_.append(comparison.reference.targetMeanHz);
        screenPerturbedHz_.append(comparison.perturbed.targetMeanHz);
        screenDeltaHz_.append(comparison.targetDeltaHz);
        ++screenReplicateIndex_;

        if (stopRequested_)
        {
            result_.stopped = true;
            finalizeScreenCandidate(false);
            finalizeScreen();
            return;
        }

        if (screenReplicateIndex_ >= screenReplicatesRequested_)
        {
            finalizeScreenCandidate(true);
            advanceScreenCandidate();
            return;
        }

        if (!startCurrentScreenReplicate())
        {
            finalizeScreenCandidate(false);
            advanceScreenCandidate();
        }
        return;
    }

    if (operation_ == CausalDiscoveryValidation)
    {
        if (validationCandidateIndex_ < 0 || validationCandidateIndex_ >= validationEntryIndices_.size()) return;
        const int entryIndex = validationEntryIndices_.at(validationCandidateIndex_);
        CausalDiscoveryEntry& entry = result_.entries[entryIndex];
        const ComparisonResult comparison = experiment_->comparisonResult();

        if (!comparison.valid)
        {
            entry.validationError = QStringLiteral("Replicate %1 failed: %2")
                .arg(validationReplicateIndex_ + 1).arg(comparison.error);
            finalizeValidationCandidate(false);
            advanceValidationCandidate();
            return;
        }

        validationReferenceHz_.append(comparison.reference.targetMeanHz);
        validationPerturbedHz_.append(comparison.perturbed.targetMeanHz);
        validationDeltaHz_.append(comparison.targetDeltaHz);
        entry.validationReplicatesCompleted = validationDeltaHz_.size();

        ++validationReplicateIndex_;
        if (stopRequested_)
        {
            result_.stopped = true;
            entry.validationError = QStringLiteral("Stopped after %1/%2 replicate(s).")
                .arg(entry.validationReplicatesCompleted).arg(validationReplicatesRequested_);
            finalizeValidationCandidate(false);
            finalizeValidation();
            return;
        }
        if (validationReplicateIndex_ >= validationReplicatesRequested_)
        {
            finalizeValidationCandidate(true);
            advanceValidationCandidate();
            return;
        }
        if (!startCurrentValidationReplicate())
        {
            entry.validationError = QStringLiteral("Unable to start replicate %1/%2.")
                .arg(validationReplicateIndex_ + 1).arg(validationReplicatesRequested_);
            finalizeValidationCandidate(false);
            advanceValidationCandidate();
        }
    }
}

void CausalDiscoveryController::epistasisFinished()
{
    if (!running_ || operation_ != CausalDiscoveryEpistasis || !experiment_ ||
        epistasisPairIndex_ < 0 || epistasisPairIndex_ >= epistasisPairs_.size())
        return;

    CausalDiscoveryEpistasisEntry& pair = epistasisPairs_[epistasisPairIndex_];
    const EpistasisResult e = experiment_->epistasisResult();
    pair.lastEpistasis = e;

    if (!e.valid)
    {
        pair.error = QStringLiteral("Replicate %1 failed: %2")
            .arg(epistasisReplicateIndex_ + 1).arg(e.error);
        finalizeEpistasisPair(false);
        advanceEpistasisPair();
        return;
    }

    epistasisWildTypeHz_.append(e.wildType.targetMeanHz);
    epistasisAOffHz_.append(e.aOff.targetMeanHz);
    epistasisBOffHz_.append(e.bOff.targetMeanHz);
    epistasisDoubleOffHz_.append(e.doubleOff.targetMeanHz);
    epistasisExpectedHz_.append(e.expectedAdditiveHz);
    epistasisInteractionHz_.append(e.interactionHz);
    pair.replicatesCompleted = epistasisInteractionHz_.size();

    ++epistasisReplicateIndex_;
    if (stopRequested_)
    {
        result_.stopped = true;
        pair.error = QStringLiteral("Stopped after %1/%2 replicate(s).")
            .arg(pair.replicatesCompleted).arg(epistasisReplicatesRequested_);
        finalizeEpistasisPair(false);
        finalizeEpistasis();
        return;
    }
    if (epistasisReplicateIndex_ >= epistasisReplicatesRequested_)
    {
        finalizeEpistasisPair(true);
        advanceEpistasisPair();
        return;
    }
    if (!startCurrentEpistasisReplicate())
    {
        pair.error = QStringLiteral("Unable to start replicate %1/%2.")
            .arg(epistasisReplicateIndex_ + 1).arg(epistasisReplicatesRequested_);
        finalizeEpistasisPair(false);
        advanceEpistasisPair();
    }
}

void CausalDiscoveryController::finalizeScreen()
{
    if (!running_) return;
    if (result_.provenance.valid)
        result_.provenance.updatedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    std::stable_sort(result_.entries.begin(), result_.entries.end(),
        [](const CausalDiscoveryEntry& a, const CausalDiscoveryEntry& b)
        {
            if (a.valid != b.valid) return a.valid;
            if (!a.valid) return a.candidate.graphRank < b.candidate.graphRank;
            const double aa = std::fabs(a.screenDelta.n > 0 ? a.screenDelta.mean : a.comparison.targetDeltaHz);
            const double bb = std::fabs(b.screenDelta.n > 0 ? b.screenDelta.mean : b.comparison.targetDeltaHz);
            if (std::fabs(aa - bb) > 1e-12) return aa > bb;
            return a.candidate.graphRank < b.candidate.graphRank;
        });

    int rank = 1;
    int validCount = 0;
    for (int i = 0; i < result_.entries.size(); ++i)
    {
        if (result_.entries[i].valid)
        {
            result_.entries[i].causalRank = rank++;
            ++validCount;
        }
    }

    result_.valid = validCount > 0;
    if (!result_.valid && result_.error.isEmpty())
        result_.error = QStringLiteral("No candidate comparison completed successfully.");

    running_ = false;
    operation_ = CausalDiscoveryNone;
    emit progress(100, result_.stopped ? QStringLiteral("Screen stopped") : QStringLiteral("Screen complete"));
    emit finished();
}

void CausalDiscoveryController::finalizeValidationCandidate(bool complete)
{
    if (validationCandidateIndex_ < 0 || validationCandidateIndex_ >= validationEntryIndices_.size()) return;
    const int entryIndex = validationEntryIndices_.at(validationCandidateIndex_);
    CausalDiscoveryEntry& entry = result_.entries[entryIndex];

    entry.validationReference = StatisticsController::summarize(validationReferenceHz_);
    entry.validationPerturbed = StatisticsController::summarize(validationPerturbedHz_);
    entry.validationDelta = StatisticsController::summarize(validationDeltaHz_);
    entry.validationReplicatesCompleted = validationDeltaHz_.size();
    entry.validationValid = complete && entry.validationReplicatesCompleted == validationReplicatesRequested_ &&
                            entry.validationReplicatesCompleted > 0;
    if (!complete && entry.validationError.isEmpty())
        entry.validationError = QStringLiteral("Validation incomplete: %1/%2 replicate(s).")
            .arg(entry.validationReplicatesCompleted).arg(validationReplicatesRequested_);

    ++result_.validationCandidatesCompleted;
    emit validationCandidateFinished(entry,
                                     result_.validationCandidatesCompleted,
                                     result_.validationCandidatesRequested);
}

void CausalDiscoveryController::advanceValidationCandidate()
{
    ++validationCandidateIndex_;
    if (validationCandidateIndex_ >= validationEntryIndices_.size())
    {
        finalizeValidation();
        return;
    }

    validationReplicateIndex_ = 0;
    validationReferenceHz_.clear();
    validationPerturbedHz_.clear();
    validationDeltaHz_.clear();

    if (!startCurrentValidationReplicate())
    {
        const int entryIndex = validationEntryIndices_.at(validationCandidateIndex_);
        CausalDiscoveryEntry& entry = result_.entries[entryIndex];
        entry.validationError = QStringLiteral("Unable to start first validation replicate.");
        finalizeValidationCandidate(false);
        advanceValidationCandidate();
    }
}

void CausalDiscoveryController::finalizeValidation()
{
    if (!running_) return;
    if (result_.provenance.valid)
        result_.provenance.updatedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    int completedValid = 0;
    for (int i = 0; i < validationEntryIndices_.size(); ++i)
        if (result_.entries.at(validationEntryIndices_.at(i)).validationValid)
            ++completedValid;

    if (completedValid == 0 && result_.error.isEmpty() && !result_.stopped)
        result_.error = QStringLiteral("No candidate completed statistical validation successfully.");

    running_ = false;
    operation_ = CausalDiscoveryNone;
    emit progress(100, result_.stopped ? QStringLiteral("Validation stopped") : QStringLiteral("Validation complete"));
    emit finished();
}

void CausalDiscoveryController::finalizeEpistasisPair(bool complete)
{
    if (epistasisPairIndex_ < 0 || epistasisPairIndex_ >= epistasisPairs_.size())
        return;

    CausalDiscoveryEpistasisEntry& pair = epistasisPairs_[epistasisPairIndex_];
    pair.wildType = StatisticsController::summarize(epistasisWildTypeHz_);
    pair.aOff = StatisticsController::summarize(epistasisAOffHz_);
    pair.bOff = StatisticsController::summarize(epistasisBOffHz_);
    pair.doubleOff = StatisticsController::summarize(epistasisDoubleOffHz_);
    pair.expectedAdditive = StatisticsController::summarize(epistasisExpectedHz_);
    pair.interaction = StatisticsController::summarize(epistasisInteractionHz_);
    pair.replicatesCompleted = epistasisInteractionHz_.size();
    pair.valid = complete && pair.replicatesCompleted == epistasisReplicatesRequested_ &&
                 pair.replicatesCompleted > 0;
    if (!complete && pair.error.isEmpty())
        pair.error = QStringLiteral("Epistasis incomplete: %1/%2 replicate(s).")
            .arg(pair.replicatesCompleted).arg(epistasisReplicatesRequested_);

    result_.epistasisEntries.append(pair);
    result_.epistasisPairsCompleted = result_.epistasisEntries.size();
    emit epistasisPairFinished(pair, result_.epistasisPairsCompleted, result_.epistasisPairsRequested);
}

void CausalDiscoveryController::advanceEpistasisPair()
{
    ++epistasisPairIndex_;
    if (epistasisPairIndex_ >= epistasisPairs_.size())
    {
        finalizeEpistasis();
        return;
    }

    epistasisReplicateIndex_ = 0;
    epistasisWildTypeHz_.clear();
    epistasisAOffHz_.clear();
    epistasisBOffHz_.clear();
    epistasisDoubleOffHz_.clear();
    epistasisExpectedHz_.clear();
    epistasisInteractionHz_.clear();

    if (!startCurrentEpistasisReplicate())
    {
        CausalDiscoveryEpistasisEntry& pair = epistasisPairs_[epistasisPairIndex_];
        pair.error = QStringLiteral("Unable to start first epistasis replicate.");
        finalizeEpistasisPair(false);
        advanceEpistasisPair();
    }
}

void CausalDiscoveryController::finalizeEpistasis()
{
    if (!running_) return;
    if (result_.provenance.valid)
        result_.provenance.updatedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    std::stable_sort(result_.epistasisEntries.begin(), result_.epistasisEntries.end(),
        [](const CausalDiscoveryEpistasisEntry& a, const CausalDiscoveryEpistasisEntry& b)
        {
            if (a.valid != b.valid) return a.valid;
            if (a.valid && b.valid)
            {
                const double aa = std::fabs(a.interaction.mean);
                const double bb = std::fabs(b.interaction.mean);
                if (std::fabs(aa - bb) > 1e-12) return aa > bb;
            }
            if (a.causalRankA != b.causalRankA) return a.causalRankA < b.causalRankA;
            return a.causalRankB < b.causalRankB;
        });

    int rank = 1;
    int validCount = 0;
    for (int i = 0; i < result_.epistasisEntries.size(); ++i)
    {
        if (result_.epistasisEntries[i].valid)
        {
            result_.epistasisEntries[i].interactionRank = rank++;
            ++validCount;
        }
    }

    if (validCount == 0 && result_.error.isEmpty() && !result_.stopped)
        result_.error = QStringLiteral("No discovery epistasis pair completed statistical validation successfully.");

    running_ = false;
    operation_ = CausalDiscoveryNone;
    emit progress(100, result_.stopped ? QStringLiteral("Discovery epistasis stopped")
                                       : QStringLiteral("Discovery epistasis complete"));
    emit finished();
}

void CausalDiscoveryController::fail(const QString& error)
{
    result_.error = error;
    result_.candidatesCompleted = result_.entries.size();
    running_ = false;
    operation_ = CausalDiscoveryNone;
    emit finished();
}
}
