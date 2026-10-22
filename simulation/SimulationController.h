#pragma once

#include "../experiment/ExperimentData.h"

#include <QMutex>
#include <QThread>
#include <QString>

class Connectome;

namespace DrosofilaLive
{
class SimulationController : public QThread
{
    Q_OBJECT

public:
    explicit SimulationController(QObject* parent = 0);
    ~SimulationController() override;

    void setConnectome(const Connectome* connectome);
    bool startExperiment(const ExperimentParameters& parameters);
    ExperimentResult result() const;
    static LifModelSnapshot validatedLifModelSnapshot();

signals:
    void simulationProgress(int percent, const QString& phase);

protected:
    void run() override;

private:
    ExperimentResult execute(const ExperimentParameters& parameters);

    const Connectome* connectome_;
    ExperimentParameters parameters_;
    mutable QMutex mutex_;
    ExperimentResult result_;
};
}
