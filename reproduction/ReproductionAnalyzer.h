#pragma once

#include "../archive/ExperimentArchive.h"

#include <QString>
#include <QVector>

namespace DrosofilaLive
{
struct ReproductionCheck
{
    QString category;
    QString item;
    QString archived;
    QString current;
    bool required = true;
    bool match = false;
};

struct ReproductionMetric
{
    QString phase;
    QString subject;
    QString metric;
    double archived = 0.0;
    double reproduced = 0.0;
    double delta = 0.0;
    double tolerance = 0.0;
    bool match = false;
};

struct ReproductionAnalysis
{
    bool valid = false;
    bool environmentExact = false;
    bool numericalPass = false;
    QString error;
    QString startedUtc;
    QString finishedUtc;
    QVector<ReproductionCheck> preflight;
    QVector<ReproductionMetric> metrics;
    CircuitExploreResult reproducedExplorer;
    CausalDiscoveryResult reproducedDiscovery;

    int mismatchCount() const;
    int requiredPreflightMismatchCount() const;
};

class ReproductionAnalyzer
{
public:
    static QVector<ReproductionMetric> compareExplorer(const CircuitExploreResult& archived,
                                                        const CircuitExploreResult& reproduced,
                                                        double tolerance = 1e-9);
    static QVector<ReproductionMetric> compareDiscovery(const CausalDiscoveryResult& archived,
                                                         const CausalDiscoveryResult& reproduced,
                                                         double tolerance = 1e-9);
    static bool writeHtmlReport(const QString& filePath,
                                const ExperimentArchiveBundle& archivedBundle,
                                const ReproductionAnalysis& analysis,
                                QString* error = 0);
};
}
