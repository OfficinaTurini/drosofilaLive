#pragma once

#include "../experiment/ExperimentData.h"
#include "../explorer/CircuitExplorer.h"

#include <QObject>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <cstdint>

namespace DrosofilaLive
{
class ExperimentController;

enum CausalDiscoveryOperation
{
    CausalDiscoveryNone = 0,
    CausalDiscoveryScreen,
    CausalDiscoveryValidation,
    CausalDiscoveryEpistasis
};

struct CausalDiscoveryCandidate
{
    std::uint64_t rootId = 0;
    QString type;
    int graphRank = 0;
    int depth = 0;
    quint64 synapseStrength = 0;
    double graphScore = 0.0;
};

struct CausalDiscoveryEntry
{
    bool valid = false;
    QString error;
    int causalRank = 0;
    CausalDiscoveryCandidate candidate;
    ComparisonResult comparison;

    // Coarse-screen statistics. With one replicate these reduce to the historical
    // single-shot comparison; with >1 replicate the causal ranking is based on
    // the mean TARGET delta across independent seeds.
    int screenReplicatesRequested = 0;
    int screenReplicatesCompleted = 0;
    StatisticalSummary screenReference;
    StatisticalSummary screenPerturbed;
    StatisticalSummary screenDelta;

    bool validationAttempted = false;
    bool validationValid = false;
    QString validationError;
    int validationReplicatesRequested = 0;
    int validationReplicatesCompleted = 0;
    StatisticalSummary validationReference;
    StatisticalSummary validationPerturbed;
    StatisticalSummary validationDelta;
};

struct CausalDiscoveryEpistasisEntry
{
    bool valid = false;
    QString error;
    int interactionRank = 0;
    std::uint64_t rootA = 0;
    std::uint64_t rootB = 0;
    QString typeA;
    QString typeB;
    int causalRankA = 0;
    int causalRankB = 0;
    double validatedDeltaAHz = 0.0;
    double validatedDeltaBHz = 0.0;
    int replicatesRequested = 0;
    int replicatesCompleted = 0;
    StatisticalSummary wildType;
    StatisticalSummary aOff;
    StatisticalSummary bOff;
    StatisticalSummary doubleOff;
    StatisticalSummary expectedAdditive;
    StatisticalSummary interaction;
    EpistasisResult lastEpistasis;
};

struct CausalDiscoveryProvenance
{
    bool valid = false;
    QString origin;                 // GUI, cvBasic or ARCHIVE
    CircuitExploreParameters explorerParameters;
    QString selectionMode;          // GLOBAL or LAYERED
    int selectionTopN = 0;
    int selectedCandidates = 0;
    bool screenParametersValid = false;
    ExperimentParameters screenParameters;
    int validationTopN = 0;
    bool validationParametersValid = false;
    ExperimentParameters validationParameters;
    int epistasisTopN = 0;
    bool epistasisParametersValid = false;
    ExperimentParameters epistasisParameters;
    QString epistasisGateMode;      // NONE, CI95, SIGN or BOTH
    double epistasisMinimumSignConsistency = 0.0;
    QString startedUtc;
    QString updatedUtc;
};

struct CausalDiscoveryResult
{
    bool valid = false;
    bool stopped = false;
    QString error;
    std::uint64_t targetRootId = 0;
    QString targetType;
    int candidatesRequested = 0;
    int candidatesCompleted = 0;
    int screenReplicatesRequested = 1;
    int validationCandidatesRequested = 0;
    int validationCandidatesCompleted = 0;
    int validationReplicatesRequested = 0;
    QVector<std::uint64_t> validationRootIds;
    int epistasisPairsRequested = 0;
    int epistasisPairsCompleted = 0;
    int epistasisReplicatesRequested = 0;
    QVector<std::uint64_t> epistasisRootIds;
    ExperimentParameters parameters;
    CausalDiscoveryProvenance provenance;
    QVector<CausalDiscoveryEntry> entries;
    QVector<CausalDiscoveryEpistasisEntry> epistasisEntries;
};

class CausalDiscoveryController : public QObject
{
    Q_OBJECT

public:
    explicit CausalDiscoveryController(ExperimentController* experiment, QObject* parent = 0);

    bool isRunning() const;
    CausalDiscoveryOperation operation() const;
    CausalDiscoveryOperation lastOperation() const;
    void requestStop();
    void clearResult();
    bool restoreResult(const CausalDiscoveryResult& result);
    void setProvenance(const CausalDiscoveryProvenance& provenance);

    bool runScreen(const ExperimentParameters& parameters,
                   const QVector<CausalDiscoveryCandidate>& candidates,
                   int replicates = 1);
    bool runValidation(const ExperimentParameters& parameters,
                       const QVector<std::uint64_t>& rootIds,
                       int replicates);
    bool runEpistasis(const ExperimentParameters& parameters,
                      const QVector<std::uint64_t>& rootIds,
                      int replicates);
    CausalDiscoveryResult result() const;

signals:
    void progress(int percent, const QString& phase);
    void candidateFinished(const DrosofilaLive::CausalDiscoveryEntry& entry, int completed, int total);
    void validationCandidateFinished(const DrosofilaLive::CausalDiscoveryEntry& entry, int completed, int total);
    void epistasisPairFinished(const DrosofilaLive::CausalDiscoveryEpistasisEntry& entry, int completed, int total);
    void finished();

private slots:
    void comparisonProgress(int percent, const QString& phase);
    void comparisonFinished();
    void epistasisProgress(int percent, const QString& phase);
    void epistasisFinished();

private:
    bool startCurrentScreenReplicate();
    void finalizeScreenCandidate(bool complete);
    void advanceScreenCandidate();
    bool startCurrentValidationReplicate();
    bool startCurrentEpistasisReplicate();
    void finalizeScreen();
    void finalizeValidationCandidate(bool complete);
    void advanceValidationCandidate();
    void finalizeValidation();
    void finalizeEpistasisPair(bool complete);
    void advanceEpistasisPair();
    void finalizeEpistasis();
    void fail(const QString& error);
    int entryIndexForRoot(std::uint64_t rootId) const;

    ExperimentController* experiment_;
    ExperimentParameters baseParameters_;
    QVector<CausalDiscoveryCandidate> candidates_;
    QVector<int> validationEntryIndices_;
    QVector<double> validationReferenceHz_;
    QVector<double> validationPerturbedHz_;
    QVector<double> validationDeltaHz_;
    int candidateIndex_;
    int screenReplicateIndex_;
    int screenReplicatesRequested_;
    QVector<double> screenReferenceHz_;
    QVector<double> screenPerturbedHz_;
    QVector<double> screenDeltaHz_;
    int validationCandidateIndex_;
    int validationReplicateIndex_;
    int validationReplicatesRequested_;

    QVector<CausalDiscoveryEpistasisEntry> epistasisPairs_;
    QVector<double> epistasisWildTypeHz_;
    QVector<double> epistasisAOffHz_;
    QVector<double> epistasisBOffHz_;
    QVector<double> epistasisDoubleOffHz_;
    QVector<double> epistasisExpectedHz_;
    QVector<double> epistasisInteractionHz_;
    int epistasisPairIndex_;
    int epistasisReplicateIndex_;
    int epistasisReplicatesRequested_;

    bool running_;
    bool stopRequested_;
    CausalDiscoveryOperation operation_;
    CausalDiscoveryOperation lastOperation_;
    CausalDiscoveryResult result_;
};
}

Q_DECLARE_METATYPE(DrosofilaLive::CausalDiscoveryProvenance)
Q_DECLARE_METATYPE(DrosofilaLive::CausalDiscoveryEntry)
Q_DECLARE_METATYPE(DrosofilaLive::CausalDiscoveryEpistasisEntry)
