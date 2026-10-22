#pragma once

#include "../experiment/ExperimentData.h"

#include <QObject>

#include <cstdint>

namespace DrosofilaLive
{
class ExperimentController;

class StatisticsController : public QObject
{
    Q_OBJECT

public:
    explicit StatisticsController(ExperimentController* experiment, QObject* parent = 0);

    bool isRunning() const;
    void requestStop();
    bool runEpistasisValidation(const ExperimentParameters& parameters,
                                std::uint64_t rootA,
                                std::uint64_t rootB,
                                int replicates);
    StatisticalEpistasisResult result() const;

    static StatisticalSummary summarize(const QVector<double>& values);

signals:
    void progress(int percent, const QString& phase);
    void finished();

private slots:
    void epistasisProgress(int percent, const QString& phase);
    void epistasisFinished();

private:
    bool startCurrentReplicate();
    void completeWithError(const QString& error);
    void finalize();

    ExperimentController* experiment_;
    ExperimentParameters baseParameters_;
    std::uint64_t rootA_;
    std::uint64_t rootB_;
    int replicatesRequested_;
    int replicateIndex_;
    bool running_;
    bool stopRequested_;
    StatisticalEpistasisResult result_;
};
}
