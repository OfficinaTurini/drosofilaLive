#pragma once

#include "../experiment/ExperimentData.h"
#include "../explorer/CircuitExplorer.h"
#include "../discovery/CausalDiscoveryController.h"
#include "../analysis/CausalStructureAnalysis.h"

#include <QString>
#include <QByteArray>
#include <QStringList>
#include <QVector>

namespace DrosofilaLive
{
struct ExperimentArchiveBundle
{
    int formatVersion = 3;
    QString applicationVersion;
    QString connectomeVersion;
    QString createdUtc;
    QString sourcePath;
    ExperimentResult experiment;
    ComparisonResult comparison;
    QVector<EpistasisResult> epistasis;
    QVector<StatisticalEpistasisResult> statistics;
    CircuitExploreResult explorer;
    CausalDiscoveryResult discovery;
    CausalStructureAnalysisResult causalSurprise;
    QByteArray embeddedStimulus;

    bool hasAnyResult() const
    {
        return experiment.valid || comparison.valid || !epistasis.isEmpty() || !statistics.isEmpty() ||
               explorer.valid || !discovery.entries.isEmpty() || !discovery.epistasisEntries.isEmpty() || causalSurprise.valid;
    }
};

class ExperimentArchive
{
public:
    static bool save(const QString& filePath,
                     const ExperimentArchiveBundle& bundle,
                     QString* error = 0);

    static bool load(const QString& filePath,
                     ExperimentArchiveBundle* bundle,
                     QString* error = 0);

    // Creates a small set of human-readable CSV files beside baseFilePath.
    // The caller may pass "result.csv"; additional files use suffixes such as
    // "result_timeline.csv" and "result_epistasis.csv".
    static bool exportCsv(const QString& baseFilePath,
                          const ExperimentArchiveBundle& bundle,
                          QStringList* writtenFiles = 0,
                          QString* error = 0);

    static QString summary(const ExperimentArchiveBundle& bundle);
};
}
