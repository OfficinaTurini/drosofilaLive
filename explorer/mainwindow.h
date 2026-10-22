#pragma once

#include "experiment/ExperimentData.h"
#include "archive/ExperimentArchive.h"
#include "model/NeuronSkeleton.h"
#include "explorer/CircuitExplorer.h"
#include "discovery/CausalDiscoveryController.h"
#include "reproduction/ReproductionAnalyzer.h"
#include "analysis/CausalStructureAnalysis.h"

#include <QMainWindow>
#include <QString>
#include <QVector>

#include <cstdint>
#include <vector>

namespace Ui { class MainWindow; }
class INeuronRenderer3D;
class QTimer;
class QThread;
class QCloseEvent;
namespace Qwt3D { class SurfacePlot; }
class QDockWidget;

namespace DrosofilaLive { class ExperimentController; class TimelinePlotController; class ScriptBridge; class StatisticsController; class StatisticsPlotController; class CircuitExplorer; class CircuitGraphController; class CausalSurprisePlotController; }

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = 0);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    void circuitExploreRequested(const DrosofilaLive::CircuitExploreParameters& parameters);
    void circuitExplorerInvalidateRequested();

private slots:
    void initializeRenderer();
    void loadConnectome();
    void loadCausalCircuit();
    void resetView();
    void lightingChanged(bool enabled);
    void surfaceStyle3DChanged(int index);
    void shadingStyle3DChanged(int index);
    void coordinateStyle3DChanged(int index);
    void smoothMesh3DChanged(bool enabled);
    void grid3DChanged(bool enabled);
    void meshLineWidth3DChanged(double width);
    void orthographic3DChanged(bool enabled);
    void colorPalette3DChanged(int index);
    void selectedNeuronChanged(int index);

    void startCircuitExplore();
    void circuitExploreProgress(int percent, const QString& phase);
    void circuitExploreFinished(const DrosofilaLive::CircuitExploreResult& result);
    void explorerAddSelected();
    void explorerRemoveSelected();
    void explorerClearWorkingCircuit();
    void explorerUseSelectedAsRoot();
    void explorerCompareSelected();
    void explorerResultSelectionChanged();
    void workingCircuitItemChanged();
    void graphNeuronSelected(qulonglong rootId);
    void graphZoomChanged(int percent);

    void startCausalDiscovery();
    void stopCausalDiscovery();
    void causalDiscoveryProgress(int percent, const QString& phase);
    void causalDiscoveryCandidateFinished(const DrosofilaLive::CausalDiscoveryEntry& entry, int completed, int total);
    void causalDiscoveryEpistasisPairFinished(const DrosofilaLive::CausalDiscoveryEpistasisEntry& entry, int completed, int total);
    void causalDiscoveryFinished();
    void causalDiscoveryReleaseGuard();
    void discoveryAddSelected();
    void discoveryCompareSelected();
    void discoveryValidateSelected();
    void discoveryValidateTop();
    void discoveryEpistasisTop();
    void discoveryGateChanged();

    void analyzeCausalSurprise();
    void openCausalSurpriseReport();
    void causalSurpriseSelectionChanged();
    void causalSurprisePlotRootSelected(qulonglong rootId);
    void scriptCausalSurpriseReady(const DrosofilaLive::CausalStructureAnalysisResult& result);

    void startExperiment();
    void experimentProgress(int percent, const QString& phase);
    void experimentFinished();
    void playExperiment();

    void startComparison();
    void comparisonProgress(int percent, const QString& phase);
    void comparisonFinished();
    void playComparison();

    void startEpistasis();
    void epistasisProgress(int percent, const QString& phase);
    void epistasisFinished();
    void epistasisSelectionChanged();
    void clearEpistasisMatrix();
    void scriptEpistasisBatchFinished(int pairCount);

    void startStatistics();
    void stopStatistics();
    void statisticsProgress(int percent, const QString& phase);
    void statisticsFinished();
    void statisticsSelectionChanged();
    void clearStatistics();
    void statisticsRankChanged(int index);
    void scriptStatisticsRankRequested();

    void openArchive();
    void saveArchive();
    void exportArchiveCsv();
    void playArchive();
    void scriptArchiveSave(const QString& path);
    void scriptArchiveLoad(const QString& path);
    void scriptArchiveReplay();
    void scriptArchiveExportCsv(const QString& path);

    void runReproductionPreflight();
    void startReproduction();
    void stopReproduction();
    void openReproductionReport();

    void playbackTick();
    void experimentSelectionChanged();
    void clearAblations();

    void openScript();
    void saveScript();
    void checkScriptSyntax();
    void runScript();
    void stopScript();
    void clearGlobalLog();
    void scriptOutputLine(const QString& text);
    void scriptStatusChanged(const QString& text);
    void scriptQueueProgress(int completed, int total, const QString& text);
    void scriptFinished(bool ok, const QString& message);
    void scriptPlaySingle();
    void scriptPlayComparison(int mode);
    void scriptDiscoveryGuardChanged(bool active);

    void openFoundationsItalian();
    void openFoundationsEnglish();
    void openHelpItalian();
    void openHelpEnglish();
    void openDataCredits();
    void showLicense();

private:
    struct WorkingCircuitEntry
    {
        std::uint64_t rootId = 0;
        QString type;
        bool watch = true;
        bool ablate = false;
    };

    enum ReproductionPhase
    {
        ReproductionIdle = 0,
        ReproductionExplorer,
        ReproductionScreen,
        ReproductionValidation,
        ReproductionEpistasis
    };

    enum PlaybackMode
    {
        PlaybackSingle = 0,
        PlaybackComparisonReference,
        PlaybackComparisonPerturbed,
        PlaybackComparisonDifference,
        PlaybackEpistasisWildType,
        PlaybackEpistasisAOff,
        PlaybackEpistasisBOff,
        PlaybackEpistasisDoubleOff
    };

    std::uint64_t selectedNeuronId() const;
    QString defaultCircuitPath() const;
    QString defaultConnectomePath() const;
    QString defaultStimulusPath() const;
    bool connectomeFilesPresent(const QString& directory) const;
    QString selectedConnectomeInfo(std::uint64_t rootId) const;
    void installScene(const std::vector<NeuronSkeleton>& neurons, const QString& sourceLabel);
    void showSelectedNeuron();
    void updateSceneMetrics();
    void updateTimelineSelection();
    bool rendererReady() const;
    void selectCircuitGraph();
    void populateCircuitExplorerResult(const DrosofilaLive::CircuitExploreResult& result);
    void rebuildWorkingCircuitTable();
    std::uint64_t selectedExplorerRootId() const;
    QString typeForRoot(std::uint64_t rootId) const;
    bool geometryAvailableForRoot(std::uint64_t rootId) const;
    int workingCircuitIndex(std::uint64_t rootId) const;
    void addWorkingCircuitRoot(std::uint64_t rootId, const QString& type, bool watch);
    std::uint64_t selectedDiscoveryRootId() const;
    void populateCausalDiscoveryResult(const DrosofilaLive::CausalDiscoveryResult& result);
    bool discoveryValidationPassesGate(const DrosofilaLive::CausalDiscoveryEntry& entry) const;
    QString discoveryGateText() const;
    void populateCausalDiscoveryEpistasisResult(const DrosofilaLive::CausalDiscoveryResult& result);
    void startCausalValidation(const QVector<std::uint64_t>& rootIds);
    void startCausalEpistasis(const QVector<std::uint64_t>& rootIds);
    void startComparisonForRoot(std::uint64_t rootId, const QString& typeLabel);
    void setExperimentControlsEnabled(bool enabled);
    void stopPlayback(bool resetVisualState);
    void resetExperimentVisualState();
    void applyAblationVisualState(const QVector<std::uint64_t>& ablatedRootIds);
    void applyAbsoluteFrame(const DrosofilaLive::ExperimentFrame& frame,
                            const QVector<std::uint64_t>& ablatedRootIds);
    void applyDifferenceFrame(const DrosofilaLive::ExperimentFrame& reference,
                              const DrosofilaLive::ExperimentFrame& perturbed,
                              const QVector<std::uint64_t>& perturbedRootIds);
    QString experimentSummaryText(const DrosofilaLive::ExperimentResult& result) const;
    QString comparisonSummaryText(const DrosofilaLive::ComparisonResult& result) const;
    QString epistasisSummaryText(const DrosofilaLive::EpistasisResult& result) const;
    QString statisticsSummaryText(const DrosofilaLive::StatisticalEpistasisResult& result) const;
    void storeStatisticalResult(const DrosofilaLive::StatisticalEpistasisResult& result);
    void rebuildStatisticsRanking();
    void initializeEpistasisMatrix();
    void storeEpistasisResult(const DrosofilaLive::EpistasisResult& result);
    int causalIndexForRoot(std::uint64_t rootId) const;
    QVector<std::uint64_t> watchedRootIdsFromUi() const;
    QVector<std::uint64_t> ablatedRootIdsFromUi() const;
    QString rootListText(const QVector<std::uint64_t>& rootIds) const;
    DrosofilaLive::ExperimentParameters experimentParametersFromUi() const;
    void updateScriptControls();
    DrosofilaLive::ExperimentArchiveBundle currentArchiveBundle() const;
    void applyArchiveBundle(const DrosofilaLive::ExperimentArchiveBundle& bundle, const QString& path);
    void updateArchiveUi();
    bool bestDiscoveryReplayComparison(DrosofilaLive::ComparisonResult* result, QString* label = 0) const;
    bool bestDiscoveryReplayEpistasis(DrosofilaLive::EpistasisResult* result, QString* label = 0) const;
    bool selectReplayNeuron(const DrosofilaLive::ExperimentResult& result);
    QString resolvedArchivePath(const QString& path) const;
    bool saveArchiveToPath(const QString& path, bool fromScript);
    bool loadArchiveFromPath(const QString& path, bool fromScript);
    bool exportArchiveCsvToPath(const QString& path, bool fromScript);
    QVector<DrosofilaLive::ReproductionCheck> buildReproductionPreflight() const;
    bool reproductionArchiveReady(QString* reason = 0) const;
    void populateReproductionPreflight(const QVector<DrosofilaLive::ReproductionCheck>& checks);
    void populateReproductionMetrics(const QVector<DrosofilaLive::ReproductionMetric>& metrics);
    void handleReproductionExplorerFinished(const DrosofilaLive::CircuitExploreResult& result);
    void handleReproductionDiscoveryFinished();
    void finishReproduction(bool completed, const QString& error = QString());
    QVector<DrosofilaLive::CausalDiscoveryCandidate> reproductionCandidates(const DrosofilaLive::CircuitExploreResult& explorer) const;
    QString reproductionReportFilePath() const;
    bool prepareReproductionStimulus(QString* path, QString* error = 0) const;
    void appendGlobalLog(const QString& text);
    void appendGlobalLogSection(const QString& section, const QString& text);
    void selectTimelinePlot();
    void selectStatisticsPlot();
    void selectCausalSurprisePlot();
    void populateCausalSurpriseResult(const DrosofilaLive::CausalStructureAnalysisResult& result);
    void invalidateCausalSurprise(const QString& reason = QString());
    QString causalSurpriseReportFilePath() const;
    void setIdleActivityMessage(const QString& text);
    void beginActivity(const QString& title, const QString& overallText);
    void updateActivityOverall(int percent, const QString& text);
    void updateActivityDetail(int percent, const QString& text);
    void finishActivity(const QString& text, bool ok);
    int detailPercentFromPhase(const QString& phase) const;
    void updateScientificActivity(const QString& title, int overallPercent, const QString& phase);
    void applyVisualTheme();
    void setup3DViewDock();
    void restoreUiSettings();
    void saveUiSettings() const;
    void detachQwt3DForShutdown();

    Ui::MainWindow* ui;
    INeuronRenderer3D* renderer_;
    Qwt3D::SurfacePlot* plot3DWidget_;
    QDockWidget* view3DDock_;
    DrosofilaLive::ExperimentController* experiment_;
    DrosofilaLive::TimelinePlotController* timeline_;
    DrosofilaLive::ScriptBridge* script_;
    DrosofilaLive::StatisticsController* statistics_;
    DrosofilaLive::StatisticsPlotController* statisticsPlot_;
    DrosofilaLive::CausalSurprisePlotController* causalSurprisePlot_;
    DrosofilaLive::CircuitExplorer* circuitExplorer_;
    DrosofilaLive::CircuitGraphController* circuitGraph_;
    DrosofilaLive::CausalDiscoveryController* discovery_;
    QThread* circuitExplorerThread_;
    QTimer* playbackTimer_;
    DrosofilaLive::ExperimentResult lastExperiment_;
    DrosofilaLive::ComparisonResult lastComparison_;
    DrosofilaLive::EpistasisResult lastEpistasis_;
    DrosofilaLive::ComparisonResult archiveReplayComparison_;
    DrosofilaLive::EpistasisResult archiveReplayEpistasis_;
    DrosofilaLive::ExperimentResult archiveReplaySingle_;
    QVector<DrosofilaLive::EpistasisResult> epistasisResults_;
    DrosofilaLive::StatisticalEpistasisResult lastStatistics_;
    QVector<DrosofilaLive::StatisticalEpistasisResult> statisticalResults_;
    DrosofilaLive::CircuitExploreResult lastCircuitExplore_;
    DrosofilaLive::CausalStructureAnalysisResult lastCausalSurprise_;
    QString causalSurpriseReportPath_;
    QVector<WorkingCircuitEntry> workingCircuit_;
    bool circuitExplorerBusy_;
    bool causalDiscoveryGuard_;
    int playbackFrame_;
    PlaybackMode playbackMode_;
    bool circuitLoaded_;
    std::vector<std::uint64_t> neuronIds_;
    unsigned segmentCount_;
    unsigned nodeCount_;
    QString currentSource_;
    QString archiveSourcePath_;
    QString archiveCreatedUtc_;
    QString activityName_;
    DrosofilaLive::ExperimentArchiveBundle reproductionArchive_;
    DrosofilaLive::ReproductionAnalysis reproductionAnalysis_;
    ReproductionPhase reproductionPhase_;
    bool reproductionActive_;
    bool reproductionStopRequested_;
    QString reproductionReportPath_;
    QString reproductionStimulusPath_;
    bool activityRunning_;
};
