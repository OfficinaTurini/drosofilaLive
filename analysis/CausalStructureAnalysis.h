#pragma once

#include "../explorer/CircuitExplorer.h"
#include "../discovery/CausalDiscoveryController.h"

#include <QString>
#include <QMetaType>
#include <QVector>

#include <cstdint>

namespace DrosofilaLive
{

struct CausalSurpriseEntry
{
    std::uint64_t rootId = 0;
    QString type;
    int graphRank = 0;
    int causalRank = 0;
    int surpriseRank = 0;
    int depth = 0;
    quint64 pathCount = 0;
    quint64 synapseStrength = 0;
    quint32 directSynapses = 0;
    double graphScore = 0.0;
    double validationDeltaHz = 0.0;
    double absoluteCausalEffectHz = 0.0;
    double ci95LowHz = 0.0;
    double ci95HighHz = 0.0;
    double signConsistency = 0.0;
    double structuralPercentile = 0.0;
    double causalPercentile = 0.0;
    double surpriseScore = 0.0;
    bool validationValid = false;
    bool gatePassed = false;
    QString quadrant;
};

struct CausalStructureAnalysisResult
{
    bool valid = false;
    QString error;
    QString createdUtc;
    std::uint64_t targetRootId = 0;
    QString targetType;
    QString gateMode;
    double minimumSignConsistency = 0.0;
    int validationRows = 0;
    int gatePassedRows = 0;
    bool correlationsAvailable = false;
    double pearson = 0.0;
    double spearman = 0.0;
    QVector<CausalSurpriseEntry> entries;
};

class CausalStructureAnalyzer
{
public:
    static CausalStructureAnalysisResult analyze(const CircuitExploreResult& explorer,
                                                 const CausalDiscoveryResult& discovery);

    static QString quadrantLabel(double structuralPercentile, double causalPercentile);

    static bool writeHtmlReport(const QString& filePath,
                                const CausalStructureAnalysisResult& result,
                                QString* error = 0);
};

}

Q_DECLARE_METATYPE(DrosofilaLive::CausalStructureAnalysisResult)
