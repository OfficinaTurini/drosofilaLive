#pragma once

#include "ExperimentData.h"

#include <QObject>
#include <QString>

#include <cstdint>

class Connectome;

namespace DrosofilaLive
{
class SimulationController;

class ExperimentController : public QObject
{
    Q_OBJECT

public:
    explicit ExperimentController(QObject* parent = 0);
    ~ExperimentController() override;

    bool loadConnectome(const QString& directory, QString* message = 0);
    bool isConnectomeLoaded() const;
    bool isRunning() const;
    const Connectome* readOnlyConnectome() const;

    ConnectomeSummary connectomeSummary() const;
    ConnectomeNeuronInfo neuronInfo(std::uint64_t rootId) const;
    CausalAnchorValidation validateCausalCircuit() const;

    bool run(const ExperimentParameters& parameters);
    ExperimentResult result() const;

    bool runComparison(const ExperimentParameters& commonParameters,
                       const QVector<std::uint64_t>& perturbedRootIds);
    ComparisonResult comparisonResult() const;

    bool runEpistasis(const ExperimentParameters& commonParameters,
                      std::uint64_t rootA,
                      std::uint64_t rootB);
    EpistasisResult epistasisResult() const;

signals:
    void experimentProgress(int percent, const QString& phase);
    void experimentFinished();
    void comparisonProgress(int percent, const QString& phase);
    void comparisonFinished();
    void epistasisProgress(int percent, const QString& phase);
    void epistasisFinished();

private slots:
    void forwardSimulationProgress(int percent, const QString& phase);
    void simulationFinished();

private:
    enum RunMode
    {
        RunIdle = 0,
        RunSingle,
        RunComparisonReference,
        RunComparisonPerturbed,
        RunEpistasisWildType,
        RunEpistasisAOff,
        RunEpistasisBOff,
        RunEpistasisDoubleOff
    };

    void finishComparisonWithError(const QString& error);
    void buildComparisonResult();
    void finishEpistasisWithError(const QString& error);
    bool startEpistasisStage(RunMode mode);
    void buildEpistasisResult();

    Connectome* connectome_;
    SimulationController* simulation_;
    RunMode runMode_;
    ExperimentParameters comparisonReferenceParameters_;
    ExperimentParameters comparisonPerturbedParameters_;
    ExperimentResult comparisonReferenceResult_;
    ComparisonResult comparisonResult_;

    ExperimentParameters epistasisWildTypeParameters_;
    ExperimentParameters epistasisAOffParameters_;
    ExperimentParameters epistasisBOffParameters_;
    ExperimentParameters epistasisDoubleOffParameters_;
    ExperimentResult epistasisWildTypeResult_;
    ExperimentResult epistasisAOffResult_;
    ExperimentResult epistasisBOffResult_;
    EpistasisResult epistasisResult_;
};
}
