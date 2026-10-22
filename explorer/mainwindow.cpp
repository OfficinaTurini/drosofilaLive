#include "mainwindow.h"
#include "diagnostics.h"

// QwtPlot3D is deliberately NOT created by uic.  The .ui contains only a
// plain QWidget host; SurfacePlot is constructed after the first event-loop
// turn, when the native top-level window already exists.
#include "qwt3d_surfaceplot.h"
#include <qwt_plot.h>
using Qwt3D::SurfacePlot;
#include "ui_mainwindow.h"

#include "experiment/ExperimentController.h"
#include "io/PrecomputedSkeletonLoader.h"
#include "model/CausalCircuit.h"
#include "model/NeuronVisualState.h"
#include "model/SkeletonSceneTransform.h"
#include "renderer/INeuronRenderer3D.h"
#include "renderer/QwtPlot3DRenderer.h"
#include "timeline/TimelinePlotController.h"
#include "script/ScriptBridge.h"
#include "statistics/StatisticsController.h"
#include "statistics/StatisticsPlotController.h"
#include "explorer/CircuitGraphController.h"
#include "discovery/CausalDiscoveryController.h"
#include "simulation/SimulationController.h"
#include "reproduction/ReproductionAnalyzer.h"
#include "analysis/CausalStructureAnalysis.h"
#include "analysis/CausalSurprisePlotController.h"
#include "core/RuntimePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QTextStream>
#include <QStringList>
#include <QTimer>
#include <QThread>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QBrush>
#include <QColor>
#include <QSettings>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDockWidget>
#include <QAction>
#include <QLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QDialog>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QLabel>

#include <algorithm>
#include <cmath>

namespace
{
bool containsRoot(const QVector<std::uint64_t>& roots, std::uint64_t rootId)
{
    for (int i = 0; i < roots.size(); ++i)
        if (roots.at(i) == rootId)
            return true;
    return false;
}

QString sha256File(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
}

QString lifFingerprint(const DrosofilaLive::LifModelSnapshot& m)
{
    return QStringLiteral("dt=%1;Vrest=%2;Vth=%3;tauM=%4;tauS=%5;refr=%6;delay=%7;w=%8;pois=%9;edge=%10;min=%11;retain=%12;edgeSeed=%13;bin=%14;max=%15;runaway=%16;monitor=%17;trace=%18")
        .arg(m.dtMs, 0, 'g', 15).arg(m.vRestMv, 0, 'g', 15).arg(m.vThresholdMv, 0, 'g', 15)
        .arg(m.tauMembraneMs, 0, 'g', 15).arg(m.tauSynapseMs, 0, 'g', 15).arg(m.refractoryMs, 0, 'g', 15)
        .arg(m.delayMs, 0, 'g', 15).arg(m.weightPerSynapseMv, 0, 'g', 15).arg(m.poissonForceScale, 0, 'g', 15)
        .arg(m.edgeMode).arg(m.minimumConnectivity).arg(m.randomRetentionProbability, 0, 'g', 15)
        .arg(m.edgeControlSeed).arg(m.activityBinMs, 0, 'g', 15).arg(static_cast<qulonglong>(m.maxRecordedSpikes))
        .arg(static_cast<qulonglong>(m.runawayAbortSpikesPerTrial)).arg(m.monitorEnabled ? 1 : 0).arg(m.traceEnabled ? 1 : 0);
}

const DrosofilaLive::ExperimentResult* representativeArchiveResult(const DrosofilaLive::ExperimentArchiveBundle& b)
{
    // REPRODUCE reconstructs Discovery, therefore the representative model/stimulus
    // must come from Discovery first.  A legacy single/comparison may coexist in
    // the same archive and may have been produced with a different protocol.
    for (int i = 0; i < b.discovery.entries.size(); ++i)
        if (b.discovery.entries.at(i).comparison.reference.valid) return &b.discovery.entries.at(i).comparison.reference;
    for (int i = 0; i < b.discovery.epistasisEntries.size(); ++i)
        if (b.discovery.epistasisEntries.at(i).lastEpistasis.wildType.valid) return &b.discovery.epistasisEntries.at(i).lastEpistasis.wildType;
    if (b.experiment.valid) return &b.experiment;
    if (b.comparison.reference.valid) return &b.comparison.reference;
    if (!b.epistasis.isEmpty() && b.epistasis.first().wildType.valid) return &b.epistasis.first().wildType;
    if (!b.statistics.isEmpty() && b.statistics.first().lastEpistasis.wildType.valid) return &b.statistics.first().lastEpistasis.wildType;
    return 0;
}

QString releaseAssetPath(const QString& relativePath)
{
    const QString appPath = QDir(QCoreApplication::applicationDirPath()).filePath(relativePath);
    if (QFileInfo::exists(appPath))
        return QFileInfo(appPath).absoluteFilePath();

    // Source-tree fallback is handy when launching a developer build from the
    // project directory before the post-build copy has run.
    const QString cwdPath = QDir::current().filePath(relativePath);
    if (QFileInfo::exists(cwdPath))
        return QFileInfo(cwdPath).absoluteFilePath();

    return appPath;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      renderer_(0),
      plot3DWidget_(0),
      view3DDock_(0),
      experiment_(new DrosofilaLive::ExperimentController(this)),
      timeline_(0),
      script_(0),
      statistics_(0),
      statisticsPlot_(0),
      causalSurprisePlot_(0),
      circuitExplorer_(0),
      circuitGraph_(0),
      discovery_(0),
      circuitExplorerThread_(new QThread(this)),
      playbackTimer_(new QTimer(this)),
      playbackFrame_(0),
      playbackMode_(PlaybackSingle),
      circuitExplorerBusy_(false),
      causalDiscoveryGuard_(false),
      reproductionPhase_(ReproductionIdle),
      reproductionActive_(false),
      reproductionStopRequested_(false),
      circuitLoaded_(false),
      segmentCount_(0),
      nodeCount_(0),
      activityRunning_(false)
{
    DrosofilaLiveDiagnostics::log("MainWindow v0.18-r1: before setupUi");
    ui->setupUi(this);
    setWindowTitle("drosofilaLive v0.18-r1 - Release / Documentation");
    applyVisualTheme();
    setup3DViewDock();
    timeline_ = new DrosofilaLive::TimelinePlotController(ui->timelinePlot);
    statistics_ = new DrosofilaLive::StatisticsController(experiment_, this);
    statisticsPlot_ = new DrosofilaLive::StatisticsPlotController(ui->statisticsPlot);
    causalSurprisePlot_ = new DrosofilaLive::CausalSurprisePlotController(ui->causalSurprisePlot, this);
    ui->statisticsPlotLegendLB->setText(QStringLiteral("Blue: replicate | Orange: mean | Green dashed: 95% CI | hover=readout | left-drag/wheel=zoom | middle=pan"));
    discovery_ = new DrosofilaLive::CausalDiscoveryController(experiment_, this);
    qRegisterMetaType<DrosofilaLive::CircuitExploreParameters>("DrosofilaLive::CircuitExploreParameters");
    qRegisterMetaType<DrosofilaLive::CircuitExploreResult>("DrosofilaLive::CircuitExploreResult");
    qRegisterMetaType<DrosofilaLive::CausalDiscoveryProvenance>("DrosofilaLive::CausalDiscoveryProvenance");
    qRegisterMetaType<DrosofilaLive::CausalDiscoveryEntry>("DrosofilaLive::CausalDiscoveryEntry");
    qRegisterMetaType<DrosofilaLive::CausalDiscoveryEpistasisEntry>("DrosofilaLive::CausalDiscoveryEpistasisEntry");
    qRegisterMetaType<DrosofilaLive::CausalStructureAnalysisResult>("DrosofilaLive::CausalStructureAnalysisResult");
    circuitGraph_ = new DrosofilaLive::CircuitGraphController(ui->explorerGraphGV, this);
    circuitExplorer_ = new DrosofilaLive::CircuitExplorer(experiment_->readOnlyConnectome());
    circuitExplorer_->moveToThread(circuitExplorerThread_);
    script_ = new DrosofilaLive::ScriptBridge(experiment_, statistics_, circuitExplorer_, discovery_, this);
    connect(circuitExplorerThread_, SIGNAL(finished()), circuitExplorer_, SLOT(deleteLater()));
    connect(this, SIGNAL(circuitExploreRequested(DrosofilaLive::CircuitExploreParameters)),
            circuitExplorer_, SLOT(explore(DrosofilaLive::CircuitExploreParameters)));
    connect(this, SIGNAL(circuitExplorerInvalidateRequested()),
            circuitExplorer_, SLOT(invalidateCache()));
    connect(circuitExplorer_, SIGNAL(progress(int,QString)),
            this, SLOT(circuitExploreProgress(int,QString)));
    connect(circuitExplorer_, SIGNAL(finished(DrosofilaLive::CircuitExploreResult)),
            this, SLOT(circuitExploreFinished(DrosofilaLive::CircuitExploreResult)));
    connect(circuitGraph_, SIGNAL(neuronSelected(qulonglong)),
            this, SLOT(graphNeuronSelected(qulonglong)));
    connect(circuitGraph_, SIGNAL(zoomChanged(int)),
            this, SLOT(graphZoomChanged(int)));
    connect(ui->explorerGraphZoomInPB, SIGNAL(clicked()),
            circuitGraph_, SLOT(zoomIn()));
    connect(ui->explorerGraphZoomOutPB, SIGNAL(clicked()),
            circuitGraph_, SLOT(zoomOut()));
    connect(ui->explorerGraphZoom100PB, SIGNAL(clicked()),
            circuitGraph_, SLOT(zoom100()));
    connect(ui->explorerGraphFitPB, SIGNAL(clicked()),
            circuitGraph_, SLOT(fitAll()));
    circuitExplorerThread_->start();

    ui->circuitPathLE->setText(defaultCircuitPath());
    ui->connectomePathLE->setText(defaultConnectomePath());
    ui->loadCircuitPB->setEnabled(false);
    ui->loadConnectomePB->setEnabled(true);
    ui->explorerRunPB->setEnabled(false);
    ui->explorerResultsTW->setRowCount(0);
    ui->workingCircuitTW->setRowCount(0);
    ui->discoveryResultsTW->setRowCount(0);
    ui->discoveryEpistasisTW->setRowCount(0);
    ui->discoveryRunPB->setEnabled(false);
    ui->discoveryStopPB->setEnabled(false);
    ui->discoveryValidateSelectedPB->setEnabled(false);
    ui->discoveryValidateTopPB->setEnabled(false);
    ui->discoveryEpistasisTopPB->setEnabled(false);
    ui->neuronCB->clear();
    ui->runSimulationPB->setEnabled(false);
    ui->playSimulationPB->setEnabled(false);
    ui->timelineInfoLB->setText(QStringLiteral("Timeline: run/replay an experiment. Hover=readout | left-drag/wheel=zoom | middle=pan."));
    ui->runComparisonPB->setEnabled(false);
    ui->playComparisonPB->setEnabled(false);
    ui->runEpistasisPB->setEnabled(false);
    initializeEpistasisMatrix();
    ui->runStatisticsPB->setEnabled(false);
    ui->stopStatisticsPB->setEnabled(false);
    ui->statisticsRankingTW->setRowCount(0);
    ui->archivePathLE->clear();
    ui->archiveStatusLB->setText(QStringLiteral("No archive loaded. Results can be saved at any time after a completed run."));
    ui->archivePlayPB->setEnabled(false);
    ui->archiveExportPB->setEnabled(false);
    ui->archiveSavePB->setEnabled(false);
    ui->archivePreflightPB->setEnabled(false);
    ui->archiveReproducePB->setEnabled(false);
    ui->archiveReproduceStopPB->setEnabled(false);
    ui->archiveReproduceReportPB->setEnabled(false);
    ui->archiveReproduceTW->setColumnCount(8);
    ui->archiveReproduceTW->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("Phase") << QStringLiteral("Subject") << QStringLiteral("Metric")
        << QStringLiteral("Archived") << QStringLiteral("Current/New") << QStringLiteral("Delta")
        << QStringLiteral("Tolerance") << QStringLiteral("Result"));
    ui->archiveReproduceTW->horizontalHeader()->setStretchLastSection(true);
    ui->archiveReproduceTW->setRowCount(0);
    ui->causalSurpriseTW->setColumnCount(15);
    ui->causalSurpriseTW->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("Surprise rank") << QStringLiteral("Neuron") << QStringLiteral("Root ID")
        << QStringLiteral("Graph rank") << QStringLiteral("Depth") << QStringLiteral("Graph score")
        << QStringLiteral("Paths") << QStringLiteral("Synapses") << QStringLiteral("Validation Delta Hz")
        << QStringLiteral("CI95") << QStringLiteral("Sign %") << QStringLiteral("Structural %ile")
        << QStringLiteral("Causal %ile") << QStringLiteral("Surprise") << QStringLiteral("Quadrant"));
    ui->causalSurpriseTW->horizontalHeader()->setStretchLastSection(true);
    ui->causalSurpriseTW->setRowCount(0);
    ui->causalSurpriseAnalyzePB->setEnabled(false);
    ui->causalSurpriseReportPB->setEnabled(false);
    ui->scriptEngineInfoLB->setText(script_ ? script_->engineInfo() : QStringLiteral("cvBasicEngine unavailable"));
    ui->globalLogTE->clear();
    setIdleActivityMessage(QStringLiteral("Ready. No active operation."));
    appendGlobalLog(QStringLiteral("[SYSTEM] drosofilaLive v0.18-r1 - Release / Documentation ready. Scientific core frozen at v0.17."));
    appendGlobalLogSection(QStringLiteral("Runtime directories"),
        QStringLiteral("Application: %1\nWork: %2\nScripts: %3\nResults: %4\nReports: %5\nArchives: %6\nLogs: %7")
            .arg(DrosofilaLiveRuntime::applicationDir())
            .arg(DrosofilaLiveRuntime::workDir())
            .arg(DrosofilaLiveRuntime::scriptsDir())
            .arg(DrosofilaLiveRuntime::resultsDir())
            .arg(DrosofilaLiveRuntime::reportsDir())
            .arg(DrosofilaLiveRuntime::archivesDir())
            .arg(DrosofilaLiveRuntime::logsDir()));
    ui->scriptEditorTE->setPlainText(QStringLiteral(
        "' drosofilaLive v0.18 - FAST end-to-end Discovery + Surprise smoke test\n"
        "DRO.PATHS\n"
        "DRO.DURATION 1000\n"
        "DRO.SUGAR 100\n"
        "DRO.TRIALS 1\n"
        "DRO.SEED 12345\n"
        "DRO.CLEARWATCH\n"
        "DRO.WATCH \"TARGET\"\n"
        "\n"
        "DRO.EXPLORE \"TARGET\", \"UPSTREAM\", 2, 10, 20\n"
        "DRO.SCREEN 3, 2\n"
        "DRO.REPLICATES 2\n"
        "DRO.VALIDATE 2\n"
        "DRO.EPISTATSTOP 2\n"
        "DRO.DISCOVERYCHECK\n"
        "DRO.DISCOVERYRANK\n"
        "DRO.SURPRISE\n"
        "DRO.SURPRISEHTML \"v017_quick_surprise.html\"\n"));


    connect(ui->actionFoundationsItalian, SIGNAL(triggered(bool)), this, SLOT(openFoundationsItalian()));
    connect(ui->actionFoundationsEnglish, SIGNAL(triggered(bool)), this, SLOT(openFoundationsEnglish()));
    connect(ui->actionHelpItalian, SIGNAL(triggered(bool)), this, SLOT(openHelpItalian()));
    connect(ui->actionHelpEnglish, SIGNAL(triggered(bool)), this, SLOT(openHelpEnglish()));
    connect(ui->actionDataCredits, SIGNAL(triggered(bool)), this, SLOT(openDataCredits()));
    connect(ui->actionLicense, SIGNAL(triggered(bool)), this, SLOT(showLicense()));

    connect(ui->loadConnectomePB, SIGNAL(clicked()), this, SLOT(loadConnectome()));
    connect(ui->loadCircuitPB, SIGNAL(clicked()), this, SLOT(loadCausalCircuit()));
    connect(ui->explorerRunPB, SIGNAL(clicked()), this, SLOT(startCircuitExplore()));
    connect(ui->explorerAddSelectedPB, SIGNAL(clicked()), this, SLOT(explorerAddSelected()));
    connect(ui->explorerRemoveSelectedPB, SIGNAL(clicked()), this, SLOT(explorerRemoveSelected()));
    connect(ui->explorerClearCircuitPB, SIGNAL(clicked()), this, SLOT(explorerClearWorkingCircuit()));
    connect(ui->explorerUseSelectedAsRootPB, SIGNAL(clicked()), this, SLOT(explorerUseSelectedAsRoot()));
    connect(ui->explorerCompareSelectedPB, SIGNAL(clicked()), this, SLOT(explorerCompareSelected()));
    connect(ui->explorerResultsTW, SIGNAL(itemSelectionChanged()), this, SLOT(explorerResultSelectionChanged()));
    connect(ui->discoveryRunPB, SIGNAL(clicked()), this, SLOT(startCausalDiscovery()));
    connect(ui->discoveryStopPB, SIGNAL(clicked()), this, SLOT(stopCausalDiscovery()));
    connect(ui->discoveryAddSelectedPB, SIGNAL(clicked()), this, SLOT(discoveryAddSelected()));
    connect(ui->discoveryCompareSelectedPB, SIGNAL(clicked()), this, SLOT(discoveryCompareSelected()));
    connect(ui->discoveryValidateSelectedPB, SIGNAL(clicked()), this, SLOT(discoveryValidateSelected()));
    connect(ui->discoveryValidateTopPB, SIGNAL(clicked()), this, SLOT(discoveryValidateTop()));
    connect(ui->discoveryEpistasisTopPB, SIGNAL(clicked()), this, SLOT(discoveryEpistasisTop()));
    connect(ui->discoveryRequireCiCB, SIGNAL(toggled(bool)), this, SLOT(discoveryGateChanged()));
    connect(ui->discoveryRequireSignCB, SIGNAL(toggled(bool)), this, SLOT(discoveryGateChanged()));
    connect(ui->discoveryMinSignDSB, SIGNAL(valueChanged(double)), this, SLOT(discoveryGateChanged()));
    connect(ui->causalSurpriseAnalyzePB, SIGNAL(clicked()), this, SLOT(analyzeCausalSurprise()));
    connect(ui->causalSurpriseReportPB, SIGNAL(clicked()), this, SLOT(openCausalSurpriseReport()));
    connect(ui->causalSurpriseTW, SIGNAL(itemSelectionChanged()), this, SLOT(causalSurpriseSelectionChanged()));
    connect(causalSurprisePlot_, SIGNAL(rootSelected(qulonglong)), this, SLOT(causalSurprisePlotRootSelected(qulonglong)));
    connect(ui->workingCircuitTW, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(workingCircuitItemChanged()));
    connect(ui->resetViewPB, SIGNAL(clicked()), this, SLOT(resetView()));
    connect(ui->lightingCB, SIGNAL(clicked(bool)), this, SLOT(lightingChanged(bool)));
    connect(ui->surfaceStyleCB, SIGNAL(currentIndexChanged(int)), this, SLOT(surfaceStyle3DChanged(int)));
    connect(ui->shadingStyleCB, SIGNAL(currentIndexChanged(int)), this, SLOT(shadingStyle3DChanged(int)));
    connect(ui->coordinateStyleCB, SIGNAL(currentIndexChanged(int)), this, SLOT(coordinateStyle3DChanged(int)));
    connect(ui->smoothMeshCB, SIGNAL(clicked(bool)), this, SLOT(smoothMesh3DChanged(bool)));
    connect(ui->grid3DCB, SIGNAL(clicked(bool)), this, SLOT(grid3DChanged(bool)));
    connect(ui->meshWidthSB, SIGNAL(valueChanged(double)), this, SLOT(meshLineWidth3DChanged(double)));
    connect(ui->orthographicCB, SIGNAL(clicked(bool)), this, SLOT(orthographic3DChanged(bool)));
    connect(ui->colorPaletteCB, SIGNAL(currentIndexChanged(int)), this, SLOT(colorPalette3DChanged(int)));
    connect(ui->neuronCB, SIGNAL(currentIndexChanged(int)), this, SLOT(selectedNeuronChanged(int)));
    connect(ui->runSimulationPB, SIGNAL(clicked()), this, SLOT(startExperiment()));
    connect(ui->playSimulationPB, SIGNAL(clicked()), this, SLOT(playExperiment()));
    connect(ui->clearAblationsPB, SIGNAL(clicked()), this, SLOT(clearAblations()));
    connect(ui->runComparisonPB, SIGNAL(clicked()), this, SLOT(startComparison()));
    connect(ui->playComparisonPB, SIGNAL(clicked()), this, SLOT(playComparison()));
    connect(ui->runEpistasisPB, SIGNAL(clicked()), this, SLOT(startEpistasis()));
    connect(ui->clearEpistasisMatrixPB, SIGNAL(clicked()), this, SLOT(clearEpistasisMatrix()));
    connect(ui->epistasisNeuronACB, SIGNAL(currentIndexChanged(int)), this, SLOT(epistasisSelectionChanged()));
    connect(ui->statisticsNeuronACB, SIGNAL(currentIndexChanged(int)), this, SLOT(statisticsSelectionChanged()));
    connect(ui->statisticsNeuronBCB, SIGNAL(currentIndexChanged(int)), this, SLOT(statisticsSelectionChanged()));
    connect(ui->runStatisticsPB, SIGNAL(clicked()), this, SLOT(startStatistics()));
    connect(ui->stopStatisticsPB, SIGNAL(clicked()), this, SLOT(stopStatistics()));
    connect(ui->clearStatisticsPB, SIGNAL(clicked()), this, SLOT(clearStatistics()));
    connect(ui->statisticsRankMetricCB, SIGNAL(currentIndexChanged(int)), this, SLOT(statisticsRankChanged(int)));
    connect(ui->archiveOpenPB, SIGNAL(clicked()), this, SLOT(openArchive()));
    connect(ui->archiveSavePB, SIGNAL(clicked()), this, SLOT(saveArchive()));
    connect(ui->archiveExportPB, SIGNAL(clicked()), this, SLOT(exportArchiveCsv()));
    connect(ui->archivePlayPB, SIGNAL(clicked()), this, SLOT(playArchive()));
    connect(ui->archivePreflightPB, SIGNAL(clicked()), this, SLOT(runReproductionPreflight()));
    connect(ui->archiveReproducePB, SIGNAL(clicked()), this, SLOT(startReproduction()));
    connect(ui->archiveReproduceStopPB, SIGNAL(clicked()), this, SLOT(stopReproduction()));
    connect(ui->archiveReproduceReportPB, SIGNAL(clicked()), this, SLOT(openReproductionReport()));
    connect(ui->epistasisNeuronBCB, SIGNAL(currentIndexChanged(int)), this, SLOT(epistasisSelectionChanged()));
    connect(ui->scriptOpenPB, SIGNAL(clicked()), this, SLOT(openScript()));
    connect(ui->scriptSavePB, SIGNAL(clicked()), this, SLOT(saveScript()));
    connect(ui->scriptCheckPB, SIGNAL(clicked()), this, SLOT(checkScriptSyntax()));
    connect(ui->scriptRunPB, SIGNAL(clicked()), this, SLOT(runScript()));
    connect(ui->scriptStopPB, SIGNAL(clicked()), this, SLOT(stopScript()));
    connect(ui->globalLogClearPB, SIGNAL(clicked()), this, SLOT(clearGlobalLog()));

    connect(ui->watchDNge031CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->watchCB0393CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->watchCB0177CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->watchCB0248CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->watchCB0366CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->watchDNge062CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->watchCB0701CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->ablateDNge031CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->ablateCB0393CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->ablateCB0177CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->ablateCB0248CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->ablateCB0366CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->ablateDNge062CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));
    connect(ui->ablateCB0701CB, SIGNAL(clicked()), this, SLOT(experimentSelectionChanged()));

    connect(discovery_, SIGNAL(progress(int,QString)), this, SLOT(causalDiscoveryProgress(int,QString)));
    connect(discovery_, SIGNAL(candidateFinished(DrosofilaLive::CausalDiscoveryEntry,int,int)),
            this, SLOT(causalDiscoveryCandidateFinished(DrosofilaLive::CausalDiscoveryEntry,int,int)));
    connect(discovery_, SIGNAL(validationCandidateFinished(DrosofilaLive::CausalDiscoveryEntry,int,int)),
            this, SLOT(causalDiscoveryCandidateFinished(DrosofilaLive::CausalDiscoveryEntry,int,int)));
    connect(discovery_, SIGNAL(epistasisPairFinished(DrosofilaLive::CausalDiscoveryEpistasisEntry,int,int)),
            this, SLOT(causalDiscoveryEpistasisPairFinished(DrosofilaLive::CausalDiscoveryEpistasisEntry,int,int)));
    connect(discovery_, SIGNAL(finished()), this, SLOT(causalDiscoveryFinished()));

    connect(experiment_, SIGNAL(experimentProgress(int,QString)),
            this, SLOT(experimentProgress(int,QString)));
    connect(experiment_, SIGNAL(experimentFinished()),
            this, SLOT(experimentFinished()));
    connect(experiment_, SIGNAL(comparisonProgress(int,QString)),
            this, SLOT(comparisonProgress(int,QString)));
    connect(experiment_, SIGNAL(comparisonFinished()),
            this, SLOT(comparisonFinished()));
    connect(experiment_, SIGNAL(epistasisProgress(int,QString)),
            this, SLOT(epistasisProgress(int,QString)));
    connect(experiment_, SIGNAL(epistasisFinished()),
            this, SLOT(epistasisFinished()));
    connect(statistics_, SIGNAL(progress(int,QString)), this, SLOT(statisticsProgress(int,QString)));
    connect(statistics_, SIGNAL(finished()), this, SLOT(statisticsFinished()));
    connect(script_, SIGNAL(outputLine(QString)), this, SLOT(scriptOutputLine(QString)));
    connect(script_, SIGNAL(statusChanged(QString)), this, SLOT(scriptStatusChanged(QString)));
    connect(script_, SIGNAL(queueProgress(int,int,QString)), this, SLOT(scriptQueueProgress(int,int,QString)));
    connect(script_, SIGNAL(finished(bool,QString)), this, SLOT(scriptFinished(bool,QString)));
    connect(script_, SIGNAL(playSingleRequested()), this, SLOT(scriptPlaySingle()));
    connect(script_, SIGNAL(playComparisonRequested(int)), this, SLOT(scriptPlayComparison(int)));
    connect(script_, SIGNAL(scriptDiscoveryGuardRequested(bool)), this, SLOT(scriptDiscoveryGuardChanged(bool)));
    connect(script_, SIGNAL(epistasisBatchResetRequested()), this, SLOT(clearEpistasisMatrix()));
    connect(script_, SIGNAL(epistasisBatchFinishedRequested(int)), this, SLOT(scriptEpistasisBatchFinished(int)));
    connect(script_, SIGNAL(statisticsClearRequested()), this, SLOT(clearStatistics()));
    connect(script_, SIGNAL(statisticsRankRequested()), this, SLOT(scriptStatisticsRankRequested()));
    connect(script_, SIGNAL(archiveSaveRequested(QString)), this, SLOT(scriptArchiveSave(QString)));
    connect(script_, SIGNAL(archiveLoadRequested(QString)), this, SLOT(scriptArchiveLoad(QString)));
    connect(script_, SIGNAL(archiveReplayRequested()), this, SLOT(scriptArchiveReplay()));
    connect(script_, SIGNAL(archiveExportCsvRequested(QString)), this, SLOT(scriptArchiveExportCsv(QString)));
    connect(script_, SIGNAL(causalSurpriseReady(DrosofilaLive::CausalStructureAnalysisResult)),
            this, SLOT(scriptCausalSurpriseReady(DrosofilaLive::CausalStructureAnalysisResult)));
    connect(playbackTimer_, SIGNAL(timeout()), this, SLOT(playbackTick()));
    playbackTimer_->setInterval(50); // 20 visual frames/s; biological bins remain 10 ms.

    ui->connectomeSummaryLB->setText("-");
    const bool connectomePresent = connectomeFilesPresent(ui->connectomePathLE->text());
    ui->connectomeStatusLB->setText(connectomePresent ?
        "Validated v783 binary files found. Ready to load." :
        "Connectome binary files not found in ./data at the path above. The path can be edited.");
    ui->connectomeNeuronLB->setText("Connectome: not loaded");
    ui->statusbar->showMessage("v0.18-r1: waiting for 3D backend");

    experimentSelectionChanged();
    epistasisSelectionChanged();
    statisticsSelectionChanged();
    updateSceneMetrics();
    updateArchiveUi();
    restoreUiSettings();
    DrosofilaLiveDiagnostics::log("MainWindow v0.18-r1: scheduling renderer initialization");
    QTimer::singleShot(0, this, SLOT(initializeRenderer()));
}

void MainWindow::applyVisualTheme()
{
    // Keep all visual decoration in one place.  In particular, do not style the
    // Qwt/Qwt3D canvases globally: their own palettes are part of the plots.
    ui->runSimulationPB->setProperty("uiRole", "run");
    ui->runComparisonPB->setProperty("uiRole", "run");
    ui->runEpistasisPB->setProperty("uiRole", "run");
    ui->runStatisticsPB->setProperty("uiRole", "run");
    ui->explorerRunPB->setProperty("uiRole", "run");
    ui->discoveryRunPB->setProperty("uiRole", "run");
    ui->scriptRunPB->setProperty("uiRole", "run");
    ui->archiveReproducePB->setProperty("uiRole", "run");

    ui->discoveryStopPB->setProperty("uiRole", "stop");
    ui->archiveReproduceStopPB->setProperty("uiRole", "stop");
    ui->stopStatisticsPB->setProperty("uiRole", "stop");
    ui->scriptStopPB->setProperty("uiRole", "stop");

    ui->loadConnectomePB->setProperty("uiRole", "accent");
    ui->loadCircuitPB->setProperty("uiRole", "accent");
    ui->archiveOpenPB->setProperty("uiRole", "accent");
    ui->archivePreflightPB->setProperty("uiRole", "accent");
    ui->archiveReproduceReportPB->setProperty("uiRole", "accent");
    ui->causalSurpriseAnalyzePB->setProperty("uiRole", "accent");
    ui->causalSurpriseReportPB->setProperty("uiRole", "accent");
    ui->scriptOpenPB->setProperty("uiRole", "accent");

    ui->titleLB->setProperty("uiRole", "appTitle");
    ui->subtitleLB->setProperty("uiRole", "subtitle");
    ui->activityTitleLB->setProperty("uiRole", "activityTitle");
    ui->activityStateLB->setProperty("uiRole", "activityState");

    const QList<QTableWidget*> tables = findChildren<QTableWidget*>();
    for (int i = 0; i < tables.size(); ++i)
        tables.at(i)->setAlternatingRowColors(true);

    setStyleSheet(QStringLiteral(R"QSS(
QMainWindow {
    background: #e9edf2;
}
#centralWidget, #workspaceWidget {
    background: #e9edf2;
}
#controlPanel {
    background: #f6f2e9;
    border: 1px solid #c9c3b7;
    border-radius: 8px;
}
QLabel[uiRole="appTitle"] {
    color: #234f6d;
    font-size: 22px;
    font-weight: 700;
    padding: 4px 8px 0 8px;
}
QLabel[uiRole="subtitle"] {
    color: #6c655b;
    font-size: 11px;
    padding: 0 8px 5px 8px;
}
QGroupBox {
    background: #fffdf8;
    border: 1px solid #c7d1d8;
    border-radius: 7px;
    margin-top: 10px;
    padding-top: 8px;
    font-weight: 600;
    color: #34495e;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 5px;
    color: #2f617c;
    background: #fffdf8;
}
#discoverySetupGB, #discoveryResultsGB, #discoveryEpistasisResultsGB {
    border-color: #c7a85d;
}
#discoverySetupGB::title, #discoveryResultsGB::title, #discoveryEpistasisResultsGB::title {
    color: #8a651c;
}
#simulationGB, #experimentSelectionGB {
    border-color: #86ad91;
}
#simulationGB::title, #experimentSelectionGB::title {
    color: #3f7750;
}
#comparisonGB, #comparisonPlaybackGB {
    border-color: #9a91b7;
}
#comparisonGB::title, #comparisonPlaybackGB::title {
    color: #655b8a;
}
#epistasisPairGB, #epistasisMatrixGB {
    border-color: #c18a78;
}
#epistasisPairGB::title, #epistasisMatrixGB::title {
    color: #8b5544;
}
#statisticsValidationGB, #statisticsRankingGB {
    border-color: #9c8db0;
}
#statisticsValidationGB::title, #statisticsRankingGB::title {
    color: #67527f;
}
#archiveFileGB, #archiveReplayGB {
    border-color: #7fa0ae;
}
#archiveFileGB::title, #archiveReplayGB::title {
    color: #406b7e;
}
#causalSurpriseGB {
    border-color: #d39b58;
}
#causalSurpriseGB::title {
    color: #9a5a16;
}
QTabWidget::pane {
    border: 1px solid #b9c7d0;
    background: #fbfaf7;
    border-radius: 5px;
}
QTabBar::tab {
    background: #dfe8ed;
    color: #40515c;
    border: 1px solid #bdcbd3;
    border-bottom: none;
    padding: 6px 10px;
    margin-right: 1px;
    min-width: 54px;
}
QTabBar::tab:selected {
    background: #2f718d;
    color: white;
    font-weight: 600;
}
QTabBar::tab:hover:!selected {
    background: #edf3f6;
}
QPushButton {
    background: #edf1f3;
    color: #26343d;
    border: 1px solid #aebbc3;
    border-radius: 5px;
    padding: 5px 10px;
    min-height: 20px;
}
QPushButton:hover {
    background: #f7fafb;
    border-color: #7893a3;
}
QPushButton:pressed {
    background: #d8e1e6;
}
QPushButton:disabled {
    color: #9a9fa2;
    background: #e8e8e5;
    border-color: #d0d0cc;
}
QPushButton[uiRole="run"] {
    background: #4f8a63;
    color: white;
    border-color: #3e7350;
    font-weight: 600;
}
QPushButton[uiRole="run"]:hover {
    background: #5d9a70;
}
QPushButton[uiRole="stop"] {
    background: #a9544f;
    color: white;
    border-color: #8d443f;
    font-weight: 600;
}
QPushButton[uiRole="stop"]:hover {
    background: #b9655f;
}
QPushButton[uiRole="accent"] {
    background: #3e7897;
    color: white;
    border-color: #30647f;
}
QPushButton[uiRole="accent"]:hover {
    background: #4d8aa8;
}
QPushButton[uiRole="run"]:disabled,
QPushButton[uiRole="stop"]:disabled,
QPushButton[uiRole="accent"]:disabled {
    color: #9a9fa2;
    background: #e8e8e5;
    border-color: #d0d0cc;
}
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: white;
    color: #24333d;
    border: 1px solid #b8c3c9;
    border-radius: 4px;
    padding: 3px 5px;
    selection-background-color: #5a8fa8;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border: 1px solid #3e7897;
}
QTableWidget {
    background: #fffefb;
    alternate-background-color: #f2f6f7;
    gridline-color: #d8dfe2;
    border: 1px solid #bdc8ce;
    selection-background-color: #5a8fa8;
    selection-color: white;
}
QHeaderView::section {
    background: #d9e4e9;
    color: #314b5a;
    border: none;
    border-right: 1px solid #bdc8ce;
    border-bottom: 1px solid #aebbc3;
    padding: 4px;
    font-weight: 600;
}
QPlainTextEdit {
    background: #20262b;
    color: #dce6ea;
    border: 1px solid #52616a;
    border-radius: 4px;
    selection-background-color: #3e7897;
}
#scriptEditorTE {
    background: #fffdf7;
    color: #26343d;
    border-color: #b8c3c9;
    font-family: Consolas, "Courier New", monospace;
}
QDockWidget {
    color: #314b5a;
    font-weight: 600;
}
QDockWidget::title {
    background: #d9e4e9;
    border: 1px solid #bac8d0;
    padding: 5px;
}
QProgressBar {
    border: 1px solid #aebbc3;
    border-radius: 5px;
    background: #eef1f2;
    color: #24333d;
    text-align: center;
    min-height: 17px;
}
QProgressBar::chunk {
    border-radius: 4px;
    background: #4f8a63;
}
QLabel[uiRole="activityTitle"] {
    color: #2f617c;
    font-size: 13px;
    font-weight: 700;
}
QLabel[uiRole="activityState"] {
    color: #6d5535;
    font-weight: 600;
}
QStatusBar {
    background: #dbe4e9;
    color: #314b5a;
    border-top: 1px solid #bac8d0;
}
QToolTip {
    color: #26343d;
    background: #fff8d8;
    border: 1px solid #b9a96b;
}
)QSS"));
}

void MainWindow::openFoundationsItalian()
{
    const QString path = releaseAssetPath(QStringLiteral("help/it/fondamenti.html"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::warning(this, QStringLiteral("Drosofila Live Foundations"),
                             QStringLiteral("Italian foundations file not found:\n%1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::openFoundationsEnglish()
{
    const QString path = releaseAssetPath(QStringLiteral("help/en-GB/fundamentals.html"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::warning(this, QStringLiteral("Drosofila Live Foundations"),
                             QStringLiteral("English (UK) foundations file not found:\n%1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::openHelpItalian()
{
    const QString path = releaseAssetPath(QStringLiteral("help/it/index.html"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::warning(this, QStringLiteral("Drosofila Live Help"),
                             QStringLiteral("Italian help file not found:\n%1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::openHelpEnglish()
{
    const QString path = releaseAssetPath(QStringLiteral("help/en-GB/index.html"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::warning(this, QStringLiteral("Drosofila Live Help"),
                             QStringLiteral("English (UK) help file not found:\n%1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::openDataCredits()
{
    const QString path = releaseAssetPath(QStringLiteral("help/credits.html"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::warning(this, QStringLiteral("Drosofila Live Credits"),
                             QStringLiteral("Credits/data-source file not found:\n%1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::showLicense()
{
    const QString path = releaseAssetPath(QStringLiteral("help/license.html"));
    if (!QFileInfo::exists(path))
    {
        QMessageBox::warning(this, QStringLiteral("Drosofila Live License"),
                             QStringLiteral("License file not found:\n%1").arg(path));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Drosofila Live - License"));
    dialog.resize(860, 680);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QTextBrowser* browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(true);
    browser->setOpenLinks(true);
    browser->setSource(QUrl::fromLocalFile(path));
    layout->addWidget(browser, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::setup3DViewDock()
{
    if (!ui || !ui->viewGB || view3DDock_)
        return;

    // r5 originally placed the 3D presentation controls inside the Inspect
    // tab.  They are far more useful when they stay beside the viewport, so
    // move the existing designer-created group into a normal QMainWindow dock.
    // Creating it before restoreUiSettings() also lets QMainWindow::restoreState
    // persist its position, visibility, floating state and size automatically.
    if (ui->inspectTabLayout)
        ui->inspectTabLayout->removeWidget(ui->viewGB);

    ui->viewGB->setParent(0);
    ui->viewGB->setTitle(QStringLiteral("Rendering"));

    view3DDock_ = new QDockWidget(QStringLiteral("3D View"), this);
    view3DDock_->setObjectName(QStringLiteral("view3DDockWidget"));
    view3DDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    view3DDock_->setFeatures(QDockWidget::DockWidgetClosable |
                             QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetFloatable);
    view3DDock_->setMinimumWidth(250);
    view3DDock_->setWidget(ui->viewGB);
    addDockWidget(Qt::RightDockWidgetArea, view3DDock_);

    QAction* toggle = view3DDock_->toggleViewAction();
    toggle->setText(QStringLiteral("3D View"));
    toggle->setToolTip(QStringLiteral("Show or hide the QwtPlot3D rendering controls."));
    if (ui->menuView)
        ui->menuView->addAction(toggle);
    if (ui->mainToolbar)
        ui->mainToolbar->addAction(toggle);

    DrosofilaLiveDiagnostics::log("MainWindow: 3D View dock created");
}

void MainWindow::restoreUiSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));

    const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    if (!geometry.isEmpty())
    {
        restoreGeometry(geometry);

        // A saved position may belong to a monitor that is no longer attached.
        // Rescue the window onto the primary screen when its frame does not
        // intersect any current screen at all.
        bool visibleOnScreen = false;
        const QList<QScreen*> screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i)
        {
            if (screens.at(i) && screens.at(i)->availableGeometry().intersects(frameGeometry()))
            {
                visibleOnScreen = true;
                break;
            }
        }
        if (!visibleOnScreen && QGuiApplication::primaryScreen())
        {
            const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
            move(available.center() - rect().center());
        }
    }

    const QByteArray state = settings.value(QStringLiteral("state")).toByteArray();
    if (!state.isEmpty())
        restoreState(state, 1);

    const QByteArray splitter = settings.value(QStringLiteral("visualSplitter")).toByteArray();
    if (!splitter.isEmpty() && ui->visualSplitter)
        ui->visualSplitter->restoreState(splitter);

    if (ui->controlTabs)
    {
        const int index = settings.value(QStringLiteral("controlTab"), ui->controlTabs->currentIndex()).toInt();
        if (index >= 0 && index < ui->controlTabs->count())
            ui->controlTabs->setCurrentIndex(index);
    }
    if (ui->plotTabs)
    {
        const int index = settings.value(QStringLiteral("plotTab"), ui->plotTabs->currentIndex()).toInt();
        if (index >= 0 && index < ui->plotTabs->count())
            ui->plotTabs->setCurrentIndex(index);
    }

    // 3D presentation preferences are deliberately desktop/UI state, not
    // scientific experiment state.  Remember them with the rest of QSettings.
    if (ui->lightingCB) ui->lightingCB->setChecked(settings.value(QStringLiteral("view3d/lighting"), ui->lightingCB->isChecked()).toBool());
    if (ui->surfaceStyleCB) ui->surfaceStyleCB->setCurrentIndex(settings.value(QStringLiteral("view3d/surfaceStyle"), 0).toInt());
    if (ui->shadingStyleCB) ui->shadingStyleCB->setCurrentIndex(settings.value(QStringLiteral("view3d/shadingStyle"), 0).toInt());
    if (ui->coordinateStyleCB) ui->coordinateStyleCB->setCurrentIndex(settings.value(QStringLiteral("view3d/coordinateStyle"), 0).toInt());
    if (ui->smoothMeshCB) ui->smoothMeshCB->setChecked(settings.value(QStringLiteral("view3d/smoothMesh"), true).toBool());
    if (ui->grid3DCB) ui->grid3DCB->setChecked(settings.value(QStringLiteral("view3d/grid"), false).toBool());
    if (ui->meshWidthSB) ui->meshWidthSB->setValue(settings.value(QStringLiteral("view3d/meshWidth"), 0.8).toDouble());
    if (ui->orthographicCB) ui->orthographicCB->setChecked(settings.value(QStringLiteral("view3d/orthographic"), false).toBool());
    if (ui->colorPaletteCB) ui->colorPaletteCB->setCurrentIndex(settings.value(QStringLiteral("view3d/colorPalette"), 0).toInt());

    // First r5.1 run: make the new dock unmistakably visible.  On later runs
    // QMainWindow::restoreState owns visibility, so a user who closes the dock
    // does not have it forced open again.
    if (view3DDock_ && !settings.value(QStringLiteral("view3d/dockInitialized"), false).toBool())
    {
        view3DDock_->show();
        view3DDock_->raise();
    }

    settings.endGroup();
    DrosofilaLiveDiagnostics::log(QStringLiteral("MainWindow: UI settings restored from %1/%2")
        .arg(QCoreApplication::organizationName(), QCoreApplication::applicationName()));
}

void MainWindow::saveUiSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("state"), saveState(1));
    if (ui->visualSplitter)
        settings.setValue(QStringLiteral("visualSplitter"), ui->visualSplitter->saveState());
    if (ui->controlTabs)
        settings.setValue(QStringLiteral("controlTab"), ui->controlTabs->currentIndex());
    if (ui->plotTabs)
        settings.setValue(QStringLiteral("plotTab"), ui->plotTabs->currentIndex());
    if (ui->lightingCB) settings.setValue(QStringLiteral("view3d/lighting"), ui->lightingCB->isChecked());
    if (ui->surfaceStyleCB) settings.setValue(QStringLiteral("view3d/surfaceStyle"), ui->surfaceStyleCB->currentIndex());
    if (ui->shadingStyleCB) settings.setValue(QStringLiteral("view3d/shadingStyle"), ui->shadingStyleCB->currentIndex());
    if (ui->coordinateStyleCB) settings.setValue(QStringLiteral("view3d/coordinateStyle"), ui->coordinateStyleCB->currentIndex());
    if (ui->smoothMeshCB) settings.setValue(QStringLiteral("view3d/smoothMesh"), ui->smoothMeshCB->isChecked());
    if (ui->grid3DCB) settings.setValue(QStringLiteral("view3d/grid"), ui->grid3DCB->isChecked());
    if (ui->meshWidthSB) settings.setValue(QStringLiteral("view3d/meshWidth"), ui->meshWidthSB->value());
    if (ui->orthographicCB) settings.setValue(QStringLiteral("view3d/orthographic"), ui->orthographicCB->isChecked());
    if (ui->colorPaletteCB) settings.setValue(QStringLiteral("view3d/colorPalette"), ui->colorPaletteCB->currentIndex());
    settings.setValue(QStringLiteral("view3d/dockInitialized"), true);
    settings.endGroup();
    settings.sync();
}

void MainWindow::detachQwt3DForShutdown()
{
    // This QwtPlot3D build crashes from SurfacePlot::~SurfacePlot() both when
    // explicitly deleted and when QObject reaches it in MainWindow's child
    // cascade.  Detach it while the QApplication/event loop and native GL
    // window are still alive, then intentionally leave it process-lifetime.
    // The OS releases the native/OpenGL resources at process termination.
    if (!plot3DWidget_)
        return;

    DrosofilaLiveDiagnostics::log("MainWindow::detachQwt3DForShutdown: begin");

    if (renderer_)
        renderer_->prepareForShutdown();

    plot3DWidget_->hide();
    plot3DWidget_->setAttribute(Qt::WA_DeleteOnClose, false);

    if (ui && ui->plot3DHost && ui->plot3DHost->layout())
        ui->plot3DHost->layout()->removeWidget(plot3DWidget_);

    // Reparent while the GL/native subsystem is still fully operational.
    // Do NOT delete SurfacePlot: its destructor is the failing code path.
    plot3DWidget_->setParent(0);
    DrosofilaLiveDiagnostics::log("MainWindow::detachQwt3DForShutdown: SurfacePlot orphaned intentionally");
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveUiSettings();
    detachQwt3DForShutdown();
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: begin");

    if (playbackTimer_)
    {
        DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: stopping/deleting playback timer");
        playbackTimer_->stop();
        delete playbackTimer_;
        playbackTimer_ = 0;
    }

    if (circuitExplorerThread_)
    {
        DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: stopping CircuitExplorer thread");
        circuitExplorerThread_->quit();
        circuitExplorerThread_->wait();
        DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: CircuitExplorer thread stopped");
        delete circuitExplorerThread_;
        circuitExplorerThread_ = 0;
        circuitExplorer_ = 0; // deleted by finished()->deleteLater() in its own thread
    }

    // These QObject controllers are children of MainWindow, but deleting them
    // explicitly here makes shutdown order deterministic instead of leaving it
    // to QObject's final child cascade after the MainWindow destructor body.
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: deleting ScriptBridge");
    delete script_;
    script_ = 0;
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: ScriptBridge deleted");
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: deleting Discovery controller");
    delete discovery_;
    discovery_ = 0;
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: Discovery controller deleted");
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: deleting Statistics controller");
    delete statistics_;
    statistics_ = 0;
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: Statistics controller deleted");
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: deleting Experiment controller");
    delete experiment_; // waits for a running SimulationController if necessary
    experiment_ = 0;
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: Experiment controller deleted");

    // Qwt plot items/event filters are attached to widgets owned by MainWindow.
    // Destroy their controllers while the Qwt canvases are still alive.
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: deleting Qwt 2D controllers");
    delete causalSurprisePlot_;
    causalSurprisePlot_ = 0;
    delete statisticsPlot_;
    statisticsPlot_ = 0;
    delete timeline_;
    timeline_ = 0;

    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: deleting CircuitGraph controller");
    delete circuitGraph_;
    circuitGraph_ = 0;

    // closeEvent() normally detached SurfacePlot before the event loop exits.
    // Keep this fallback for non-standard destruction paths.
    detachQwt3DForShutdown();

    delete renderer_;
    renderer_ = 0;
    plot3DWidget_ = 0; // intentionally orphaned/process-lifetime; never delete
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: renderer deleted; Qwt3D SurfacePlot intentionally not destroyed");

    delete ui; // helper only; remaining QWidget children are owned by MainWindow
    ui = 0;
    DrosofilaLiveDiagnostics::log("MainWindow::~MainWindow: end");
}

void MainWindow::appendGlobalLog(const QString& text)
{
    if (!ui || !ui->globalLogTE || text.isEmpty())
        return;
    ui->globalLogTE->appendPlainText(text);
}

void MainWindow::appendGlobalLogSection(const QString& section, const QString& text)
{
    if (!ui || !ui->globalLogTE || text.isEmpty())
        return;

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    if (!ui->globalLogTE->toPlainText().isEmpty())
        ui->globalLogTE->appendPlainText(QString());
    ui->globalLogTE->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, section));
    ui->globalLogTE->appendPlainText(text);
}

void MainWindow::selectTimelinePlot()
{
    if (ui && ui->plotTabs && ui->timelinePlotTab)
        ui->plotTabs->setCurrentWidget(ui->timelinePlotTab);
}

void MainWindow::selectStatisticsPlot()
{
    if (ui && ui->plotTabs && ui->statisticsPlotTab)
        ui->plotTabs->setCurrentWidget(ui->statisticsPlotTab);
}

void MainWindow::selectCausalSurprisePlot()
{
    if (ui && ui->plotTabs && ui->causalSurprisePlotTab)
        ui->plotTabs->setCurrentWidget(ui->causalSurprisePlotTab);
}

void MainWindow::selectCircuitGraph()
{
    if (!ui || !ui->circuitGraphDockWidget)
        return;

    ui->circuitGraphDockWidget->show();
    ui->circuitGraphDockWidget->raise();
    if (ui->circuitGraphDockWidget->isFloating())
        ui->circuitGraphDockWidget->activateWindow();
}

void MainWindow::setIdleActivityMessage(const QString& text)
{
    if (!ui || activityRunning_)
        return;

    activityName_.clear();
    ui->activityTitleLB->setText(QStringLiteral("Activity: Ready"));
    ui->activityStateLB->setText(text);
    ui->activityOverallPB->setRange(0, 100);
    ui->activityDetailPB->setRange(0, 100);
    ui->activityOverallPB->setValue(0);
    ui->activityDetailPB->setValue(0);
    ui->activityOverallPB->setEnabled(false);
    ui->activityDetailPB->setEnabled(false);
    ui->activityOverallStatusLB->setText(QStringLiteral("Ready"));
    ui->activityDetailStatusLB->setText(QStringLiteral("-"));
}

void MainWindow::beginActivity(const QString& title, const QString& overallText)
{
    if (!ui) return;

    activityRunning_ = true;
    activityName_ = title;
    ui->activityTitleLB->setText(QStringLiteral("Activity: %1").arg(title));
    ui->activityStateLB->setText(QStringLiteral("Running"));
    ui->activityOverallPB->setRange(0, 100);
    ui->activityDetailPB->setRange(0, 100);
    ui->activityOverallPB->setEnabled(true);
    ui->activityDetailPB->setEnabled(true);
    ui->activityOverallPB->setValue(0);
    ui->activityDetailPB->setValue(0);
    ui->activityOverallStatusLB->setText(overallText);
    ui->activityDetailStatusLB->setText(QStringLiteral("Waiting for first progress update..."));
}

void MainWindow::updateActivityOverall(int percent, const QString& text)
{
    if (!ui) return;
    ui->activityOverallPB->setEnabled(true);
    ui->activityOverallPB->setValue(std::max(0, std::min(100, percent)));
    if (!text.isEmpty()) ui->activityOverallStatusLB->setText(text);
}

void MainWindow::updateActivityDetail(int percent, const QString& text)
{
    if (!ui) return;
    ui->activityDetailPB->setEnabled(true);
    if (percent >= 0)
        ui->activityDetailPB->setValue(std::max(0, std::min(100, percent)));
    if (!text.isEmpty()) ui->activityDetailStatusLB->setText(text);
}

void MainWindow::finishActivity(const QString& text, bool ok)
{
    if (!ui) return;

    activityRunning_ = false;
    ui->activityStateLB->setText(ok ? QStringLiteral("Complete") : QStringLiteral("Stopped / Error"));
    if (ok)
    {
        ui->activityOverallPB->setValue(100);
        ui->activityDetailPB->setValue(100);
    }
    ui->activityOverallStatusLB->setText(text);
}

int MainWindow::detailPercentFromPhase(const QString& phase) const
{
    const int close = phase.lastIndexOf(QStringLiteral("%)"));
    if (close < 0) return -1;
    const int open = phase.lastIndexOf(QLatin1Char('('), close);
    if (open < 0 || open + 1 >= close) return -1;

    bool ok = false;
    const int value = phase.mid(open + 1, close - open - 1).toInt(&ok);
    return ok ? std::max(0, std::min(100, value)) : -1;
}

void MainWindow::updateScientificActivity(const QString& title, int overallPercent, const QString& phase)
{
    const int detailPercent = detailPercentFromPhase(phase);
    const bool scriptOwnsActivity = (script_ && script_->isBusy()) ||
                                    activityName_ == QStringLiteral("cvBasic script");

    // For cvBasic, the script command queue is the Overall level and the
    // current LIF/epistasis/statistics phase is the Detail level.
    if (scriptOwnsActivity)
    {
        if (activityName_ != QStringLiteral("cvBasic script"))
            beginActivity(QStringLiteral("cvBasic script"), QStringLiteral("Executing DRO.* command queue"));
        updateActivityDetail(detailPercent >= 0 ? detailPercent : 0,
                             QStringLiteral("%1 | %2").arg(title, phase));
        return;
    }

    if (!activityRunning_ || activityName_ != title)
        beginActivity(title, phase);
    updateActivityOverall(overallPercent, phase);
    updateActivityDetail(detailPercent >= 0 ? detailPercent : 0, phase);
}

QString MainWindow::defaultCircuitPath() const
{
    const QString appCandidate = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/flywire783_circuit"));
    if (QDir(appCandidate).exists())
        return QDir::cleanPath(appCandidate);

    const QString currentCandidate = QDir::current().filePath(QStringLiteral("data/flywire783_circuit"));
    if (QDir(currentCandidate).exists())
        return QDir::cleanPath(currentCandidate);

    return QDir::cleanPath(appCandidate);
}

QString MainWindow::defaultConnectomePath() const
{
    const QString currentCandidate = QDir::current().filePath(QStringLiteral("data"));
    if (connectomeFilesPresent(currentCandidate))
        return QDir::cleanPath(currentCandidate);

    const QString appCandidate = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
    if (connectomeFilesPresent(appCandidate))
        return QDir::cleanPath(appCandidate);

    return QDir::cleanPath(currentCandidate);
}

QString MainWindow::defaultStimulusPath() const
{
    const QString fileName = QStringLiteral("sugarR_v783_20of21.txt");
    const QString appCandidate = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/stimuli/") + fileName);
    if (QFileInfo::exists(appCandidate))
        return QDir::cleanPath(appCandidate);

    const QString currentCandidate = QDir::current().filePath(QStringLiteral("data/stimuli/") + fileName);
    if (QFileInfo::exists(currentCandidate))
        return QDir::cleanPath(currentCandidate);

    return QDir::cleanPath(appCandidate);
}

bool MainWindow::connectomeFilesPresent(const QString& directory) const
{
    const QDir dir(directory);
    return dir.exists() &&
           QFileInfo::exists(dir.filePath(QStringLiteral("neurons.bin"))) &&
           QFileInfo::exists(dir.filePath(QStringLiteral("edges.bin")));
}

void MainWindow::initializeRenderer()
{
    if (renderer_) return;

    DrosofilaLiveDiagnostics::log("MainWindow::initializeRenderer r5: begin");

    if (!ui || !ui->plot3DHost)
    {
        DrosofilaLiveDiagnostics::log("MainWindow::initializeRenderer r5: plot3DHost unavailable");
        return;
    }

    // Construct the old QGLWidget-based QwtPlot3D only after MainWindow has
    // been shown.  This avoids creating its native GL surface inside setupUi().
    QVBoxLayout* hostLayout = qobject_cast<QVBoxLayout*>(ui->plot3DHost->layout());
    if (!hostLayout)
    {
        hostLayout = new QVBoxLayout(ui->plot3DHost);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        hostLayout->setSpacing(0);
    }

    plot3DWidget_ = new SurfacePlot(ui->plot3DHost);
    plot3DWidget_->setObjectName(QStringLiteral("plot3D"));
    plot3DWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    hostLayout->addWidget(plot3DWidget_);
    plot3DWidget_->show();

    renderer_ = new QwtPlot3DRenderer(plot3DWidget_);
    renderer_->setLighting(ui->lightingCB->isChecked());
    renderer_->setSurfaceStyle(ui->surfaceStyleCB->currentIndex());
    renderer_->setShadingStyle(ui->shadingStyleCB->currentIndex());
    renderer_->setCoordinateStyle(ui->coordinateStyleCB->currentIndex());
    renderer_->setSmoothMesh(ui->smoothMeshCB->isChecked());
    renderer_->setGridVisible(ui->grid3DCB->isChecked());
    renderer_->setMeshLineWidth(ui->meshWidthSB->value());
    renderer_->setOrthographic(ui->orthographicCB->isChecked());
    renderer_->setColorPalette(ui->colorPaletteCB->currentIndex());
    renderer_->clear();
    ui->loadCircuitPB->setEnabled(true);
    ui->rendererStatusLB->setText("QwtPlot3D backend ready (palette + replay overlay)");
    ui->statusbar->showMessage("3D backend ready. Load connectome and validated circuit.");
    DrosofilaLiveDiagnostics::log("MainWindow::initializeRenderer r5: complete");
}

void MainWindow::loadConnectome()
{
    if (!experiment_ || experiment_->isRunning() || circuitExplorerBusy_)
        return;

    stopPlayback(true);
    lastExperiment_ = DrosofilaLive::ExperimentResult();
    lastComparison_ = DrosofilaLive::ComparisonResult();
    if (timeline_) timeline_->clear();
    ui->timelineInfoLB->setText(QStringLiteral("Timeline: run an experiment to display firing-rate bins."));
    ui->playSimulationPB->setEnabled(false);
    ui->playComparisonPB->setEnabled(false);
    lastEpistasis_ = DrosofilaLive::EpistasisResult();
    clearEpistasisMatrix();
    clearStatistics();

    const QString directory = ui->connectomePathLE->text().trimmed();
    DrosofilaLiveDiagnostics::log(QString("MainWindow::loadConnectome: %1").arg(directory));

    QString message;
    if (!experiment_->loadConnectome(directory, &message))
    {
        ui->connectomeStatusLB->setText(QStringLiteral("ERROR: ") + message);
        ui->connectomeSummaryLB->setText("-");
        setIdleActivityMessage(QStringLiteral("Connectome load failed; scientific operations disabled."));
        appendGlobalLogSection(QStringLiteral("Connectome ERROR"), message);
        ui->runSimulationPB->setEnabled(false);
        ui->runComparisonPB->setEnabled(false);
        ui->runEpistasisPB->setEnabled(false);
        ui->runStatisticsPB->setEnabled(false);
        ui->explorerRunPB->setEnabled(false);
        ui->discoveryRunPB->setEnabled(false);
        ui->statusbar->showMessage(QStringLiteral("Connectome load failed: ") + message);
        DrosofilaLiveDiagnostics::log(QString("MainWindow::loadConnectome ERROR: %1").arg(message));
        showSelectedNeuron();
        return;
    }

    const DrosofilaLive::ConnectomeSummary summary = experiment_->connectomeSummary();
    const DrosofilaLive::CausalAnchorValidation validation = experiment_->validateCausalCircuit();

    ui->connectomeStatusLB->setText(message.isEmpty() ?
        QStringLiteral("Connectome v783 loaded and validated.") : message);
    ui->connectomeSummaryLB->setText(
        QStringLiteral("%1 neurons | %2 edges | %3 synapses | %4 MiB mapped\nScientific anchors: IDs %5/%6, labels %7/%6")
            .arg(summary.neuronCount)
            .arg(static_cast<qulonglong>(summary.edgeCount))
            .arg(static_cast<qulonglong>(summary.synapseCount))
            .arg(summary.mappedMiB, 0, 'f', 2)
            .arg(validation.idsFound)
            .arg(validation.expected)
            .arg(validation.labelsMatched));
    appendGlobalLogSection(QStringLiteral("Connectome"),
                           QStringLiteral("%1\n%2 neurons | %3 edges | %4 synapses | %5 MiB mapped\nScientific anchors: IDs %6/%7, labels %8/%7")
                               .arg(message.isEmpty() ? QStringLiteral("Connectome v783 loaded and validated.") : message)
                               .arg(summary.neuronCount)
                               .arg(static_cast<qulonglong>(summary.edgeCount))
                               .arg(static_cast<qulonglong>(summary.synapseCount))
                               .arg(summary.mappedMiB, 0, 'f', 2)
                               .arg(validation.idsFound)
                               .arg(validation.expected)
                               .arg(validation.labelsMatched));

    emit circuitExplorerInvalidateRequested();
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    if (discovery_) discovery_->clearResult();
    lastCircuitExplore_ = DrosofilaLive::CircuitExploreResult();
    workingCircuit_.clear();
    rebuildWorkingCircuitTable();
    if (circuitGraph_) circuitGraph_->clear();
    ui->explorerResultsTW->setRowCount(0);
    ui->discoveryResultsTW->setRowCount(0);
    ui->discoveryEpistasisTW->setRowCount(0);
    ui->discoveryRunPB->setEnabled(false);
    ui->discoveryStopPB->setEnabled(false);
    ui->discoveryStatusLB->setText(QStringLiteral("Run Circuit Explorer first, then screen its top candidates."));
    ui->explorerRunPB->setEnabled(true);
    ui->explorerStatusLB->setText(QStringLiteral("Connectome ready. Upstream index will be built on the first upstream query."));

    const bool anchorsOk = validation.complete();
    ui->runSimulationPB->setEnabled(anchorsOk && !watchedRootIdsFromUi().isEmpty());
    setIdleActivityMessage(anchorsOk
        ? QStringLiteral("Connectome v783 ready; scientific operations enabled.")
        : QStringLiteral("Validated causal anchors are incomplete; scientific operations disabled."));

    DrosofilaLiveDiagnostics::log(QString("Connectome anchors:\n%1").arg(validation.details));
    ui->statusbar->showMessage(QStringLiteral("Connectome v783 loaded: %1/%2 scientific anchors found")
                               .arg(validation.idsFound).arg(validation.expected), 5000);
    showSelectedNeuron();
    experimentSelectionChanged();
    epistasisSelectionChanged();
    statisticsSelectionChanged();
    // A connectome load changes the scientific environment checked by Preflight.
    // Invalidate any previous preflight instead of silently re-running it.
    if (reproductionArchiveReady())
    {
        reproductionAnalysis_ = DrosofilaLive::ReproductionAnalysis();
        ui->archiveReproduceStatusLB->setText(QStringLiteral("Connectome loaded - run Preflight before Reproduce."));
        updateArchiveUi();
    }
}

QString MainWindow::selectedConnectomeInfo(std::uint64_t rootId) const
{
    if (!experiment_ || !experiment_->isConnectomeLoaded())
        return QStringLiteral("Connectome: not loaded");

    const DrosofilaLive::ConnectomeNeuronInfo info = experiment_->neuronInfo(rootId);
    if (!info.present)
        return QStringLiteral("Connectome: root ID not present in loaded graph");

    QString signText;
    if (info.sign > 0) signText = QStringLiteral("E (+1)");
    else if (info.sign < 0) signText = QStringLiteral("I (-1)");
    else signText = QStringLiteral("0 / no output");

    return QStringLiteral("Graph index %1\nType: %2\nSign: %3\nFan in/out: %4 / %5\nSynapses in/out: %6 / %7")
        .arg(info.index)
        .arg(info.primaryType.isEmpty() ? QStringLiteral("unannotated") : info.primaryType)
        .arg(signText)
        .arg(info.fanIn)
        .arg(info.fanOut)
        .arg(info.synapsesIn)
        .arg(info.synapsesOut);
}

void MainWindow::startCircuitExplore()
{
    if (!experiment_ || !experiment_->isConnectomeLoaded() || circuitExplorerBusy_ ||
        experiment_->isRunning() || (statistics_ && statistics_->isRunning()) ||
        (discovery_ && discovery_->isRunning()) || (script_ && script_->isBusy()))
        return;

    DrosofilaLive::CircuitExploreParameters p;
    p.rootText = ui->explorerRootLE->text().trimmed();
    p.direction = ui->explorerDirectionCB->currentIndex();
    p.maxDepth = ui->explorerDepthSB->value();
    p.minimumSynapses = ui->explorerMinSynSB->value();
    p.maximumNeurons = ui->explorerMaxNeuronsSB->value();
    if (p.rootText.isEmpty())
    {
        setIdleActivityMessage(QStringLiteral("Circuit Explorer ERROR: root is empty."));
        return;
    }

    circuitExplorerBusy_ = true;
    invalidateCausalSurprise(QStringLiteral("Explorer changed; run Causal Surprise again after validation."));
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    if (discovery_) discovery_->clearResult();
    setExperimentControlsEnabled(false);
    ui->explorerResultsTW->setRowCount(0);
    ui->discoveryResultsTW->setRowCount(0);
    ui->discoveryEpistasisTW->setRowCount(0);
    ui->discoveryStatusLB->setText(QStringLiteral("Waiting for the new Explorer result."));
    ui->explorerStatusLB->setText(QStringLiteral("Exploration running..."));
    beginActivity(QStringLiteral("Circuit Explorer"), QStringLiteral("Graph neighborhood / candidate ranking"));
    updateActivityDetail(0, QStringLiteral("Resolving root"));
    appendGlobalLogSection(QStringLiteral("Circuit Explorer"),
        QStringLiteral("Root: %1 | direction %2 | depth %3 | min |synapses| %4 | max neurons %5")
            .arg(p.rootText, ui->explorerDirectionCB->currentText())
            .arg(p.maxDepth).arg(p.minimumSynapses).arg(p.maximumNeurons));
    emit circuitExploreRequested(p);
}

void MainWindow::circuitExploreProgress(int percent, const QString& phase)
{
    if (!circuitExplorerBusy_)
        return;
    if (reproductionActive_ && reproductionPhase_ == ReproductionExplorer)
    {
        updateActivityOverall(std::max(0, std::min(10, percent / 10)), QStringLiteral("REPRODUCE 1/4 - Explorer"));
        updateActivityDetail(percent, phase);
        ui->archiveReproduceStatusLB->setText(QStringLiteral("REPRODUCE 1/4 - %1").arg(phase));
        return;
    }
    updateActivityOverall(percent, QStringLiteral("Connectome graph exploration"));
    updateActivityDetail(percent, phase);
}

QString MainWindow::typeForRoot(std::uint64_t rootId) const
{
    if (!experiment_ || !experiment_->isConnectomeLoaded() || rootId == 0)
        return QString::number(static_cast<qulonglong>(rootId));
    const DrosofilaLive::ConnectomeNeuronInfo info = experiment_->neuronInfo(rootId);
    if (!info.present || info.primaryType.trimmed().isEmpty())
        return QString::number(static_cast<qulonglong>(rootId));
    return info.primaryType.trimmed();
}

bool MainWindow::geometryAvailableForRoot(std::uint64_t rootId) const
{
    return std::find(neuronIds_.begin(), neuronIds_.end(), rootId) != neuronIds_.end();
}

void MainWindow::populateCircuitExplorerResult(const DrosofilaLive::CircuitExploreResult& result)
{
    const QSignalBlocker block(ui->explorerResultsTW);
    ui->explorerResultsTW->setRowCount(result.candidates.size());
    for (int r = 0; r < result.candidates.size(); ++r)
    {
        const DrosofilaLive::CircuitExploreCandidate& c = result.candidates.at(r);
        QString direction;
        if (c.upstream && c.downstream) direction = QStringLiteral("Both");
        else if (c.upstream) direction = QStringLiteral("Up");
        else direction = QStringLiteral("Down");
        const QString values[9] = {
            QString::number(r + 1),
            c.primaryType.isEmpty() ? QStringLiteral("unannotated") : c.primaryType,
            QString::number(static_cast<qulonglong>(c.rootId)),
            direction,
            QString::number(c.depth),
            QString::number(static_cast<qulonglong>(c.synapseStrength)),
            QString::number(static_cast<qulonglong>(c.pathCount)),
            QString::number(c.score, 'f', 3),
            geometryAvailableForRoot(c.rootId) ? QStringLiteral("yes") : QStringLiteral("graph-only")
        };
        for (int col = 0; col < 9; ++col)
        {
            QTableWidgetItem* item = new QTableWidgetItem(values[col]);
            if (col != 1) item->setTextAlignment(Qt::AlignCenter);
            if (col == 5)
                item->setToolTip(QStringLiteral("Accumulated accepted edge strength across shortest discovered paths. Direct root connection: %1 synapses; root I/O fraction %2%")
                                 .arg(c.directSynapses).arg(100.0 * c.rootFraction, 0, 'f', 2));
            if (col == 7)
                item->setToolTip(QStringLiteral("Operational exploration score only: strength x path multiplicity / depth, normalized to this query. Not a biological probability."));
            ui->explorerResultsTW->setItem(r, col, item);
        }
    }
    ui->explorerResultsTW->resizeColumnsToContents();
}

void MainWindow::circuitExploreFinished(const DrosofilaLive::CircuitExploreResult& result)
{
    if (reproductionActive_ && reproductionPhase_ == ReproductionExplorer)
    {
        handleReproductionExplorerFinished(result);
        return;
    }
    const bool scriptDriven = script_ && script_->isBusy() && !circuitExplorerBusy_;
    circuitExplorerBusy_ = false;
    lastCircuitExplore_ = result;
    // Any new graph exploration changes the scientific state, including script-driven runs.
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    updateArchiveUi();
    if (script_) script_->restoreScientificState(lastCircuitExplore_, discovery_ ? discovery_->result() : DrosofilaLive::CausalDiscoveryResult());
    setExperimentControlsEnabled(true);

    if (!result.valid)
    {
        ui->explorerStatusLB->setText(QStringLiteral("ERROR: %1").arg(result.error));
        if (!scriptDriven)
        {
            finishActivity(QStringLiteral("Circuit exploration failed."), false);
            appendGlobalLogSection(QStringLiteral("Circuit Explorer ERROR"), result.error);
        }
        return;
    }

    populateCircuitExplorerResult(result);
    if (circuitGraph_) circuitGraph_->setResult(result);
    selectCircuitGraph();
    ui->explorerGraphInfoLB->setText(QStringLiteral("Root %1 [%2] | %3 candidate(s) | %4 displayed edge(s)%5")
        .arg(result.rootType)
        .arg(static_cast<qulonglong>(result.rootId))
        .arg(result.candidates.size())
        .arg(result.edges.size())
        .arg(result.truncated ? QStringLiteral(" | search safety cap reached") : QString()));
    ui->explorerStatusLB->setText(QStringLiteral("%1 candidate(s), %2 graph edge(s) inspected%3%4")
        .arg(result.candidates.size())
        .arg(static_cast<qulonglong>(result.exploredEdges))
        .arg(result.reverseIndexBuiltNow ? QStringLiteral(" | upstream index built") : QString())
        .arg(result.truncated ? QStringLiteral(" | truncated at safety cap") : QString()));
    if (scriptDriven)
    {
        // DRO.EXPLORE invalidates the previous Discovery generation. Keep the
        // GUI consistent with the script-side controller even though the script
        // owns the activity/progress lifecycle.
        if (discovery_)
        {
            populateCausalDiscoveryResult(discovery_->result());
            populateCausalDiscoveryEpistasisResult(discovery_->result());
        }
        ui->discoveryStatusLB->setText(QStringLiteral("Script Explorer complete; previous Discovery results cleared."));
        return;
    }
    finishActivity(QStringLiteral("Circuit exploration complete."), true);
    appendGlobalLogSection(QStringLiteral("Circuit Explorer"),
        QStringLiteral("Resolved root: %1 [%2]\nCandidates returned: %3\nEdges inspected: %4\nDisplayed graph edges: %5\nReverse index built now: %6\nTruncated: %7")
            .arg(result.rootType)
            .arg(static_cast<qulonglong>(result.rootId))
            .arg(result.candidates.size())
            .arg(static_cast<qulonglong>(result.exploredEdges))
            .arg(result.edges.size())
            .arg(result.reverseIndexBuiltNow ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(result.truncated ? QStringLiteral("yes") : QStringLiteral("no")));
}

std::uint64_t MainWindow::selectedExplorerRootId() const
{
    if (!ui || !ui->explorerResultsTW || !ui->explorerResultsTW->selectionModel())
        return 0;
    const QModelIndexList rows = ui->explorerResultsTW->selectionModel()->selectedRows(2);
    if (rows.isEmpty())
        return 0;
    bool ok = false;
    const qulonglong root = rows.first().data().toString().toULongLong(&ok);
    return ok ? static_cast<std::uint64_t>(root) : 0;
}

int MainWindow::workingCircuitIndex(std::uint64_t rootId) const
{
    for (int i = 0; i < workingCircuit_.size(); ++i)
        if (workingCircuit_.at(i).rootId == rootId)
            return i;
    return -1;
}

void MainWindow::addWorkingCircuitRoot(std::uint64_t rootId, const QString& type, bool watch)
{
    if (rootId == 0)
        return;
    const int existing = workingCircuitIndex(rootId);
    if (existing >= 0)
    {
        if (watch) workingCircuit_[existing].watch = true;
        return;
    }
    WorkingCircuitEntry e;
    e.rootId = rootId;
    e.type = type.isEmpty() ? typeForRoot(rootId) : type;
    e.watch = watch;
    workingCircuit_.append(e);
}

void MainWindow::rebuildWorkingCircuitTable()
{
    const QSignalBlocker block(ui->workingCircuitTW);
    ui->workingCircuitTW->setRowCount(workingCircuit_.size());
    for (int r = 0; r < workingCircuit_.size(); ++r)
    {
        const WorkingCircuitEntry& e = workingCircuit_.at(r);
        QTableWidgetItem* type = new QTableWidgetItem(e.type);
        QTableWidgetItem* root = new QTableWidgetItem(QString::number(static_cast<qulonglong>(e.rootId)));
        QTableWidgetItem* geom = new QTableWidgetItem(geometryAvailableForRoot(e.rootId) ? QStringLiteral("yes") : QStringLiteral("graph-only"));
        QTableWidgetItem* watch = new QTableWidgetItem;
        QTableWidgetItem* ablate = new QTableWidgetItem;
        watch->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        ablate->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        watch->setCheckState(e.watch ? Qt::Checked : Qt::Unchecked);
        ablate->setCheckState(e.ablate ? Qt::Checked : Qt::Unchecked);
        root->setTextAlignment(Qt::AlignCenter);
        geom->setTextAlignment(Qt::AlignCenter);
        watch->setTextAlignment(Qt::AlignCenter);
        ablate->setTextAlignment(Qt::AlignCenter);
        ui->workingCircuitTW->setItem(r, 0, type);
        ui->workingCircuitTW->setItem(r, 1, root);
        ui->workingCircuitTW->setItem(r, 2, geom);
        ui->workingCircuitTW->setItem(r, 3, watch);
        ui->workingCircuitTW->setItem(r, 4, ablate);
    }
    ui->workingCircuitTW->resizeColumnsToContents();
    ui->workingCircuitInfoLB->setText(QStringLiteral("Working Circuit: %1 neuron(s). Watch/Ablate flags augment the Experiment tab; graph-only means no local skeleton is loaded.")
                                       .arg(workingCircuit_.size()));
}

void MainWindow::explorerAddSelected()
{
    if (!lastCircuitExplore_.valid || !ui->explorerResultsTW->selectionModel())
        return;
    const QModelIndexList rows = ui->explorerResultsTW->selectionModel()->selectedRows();
    for (int i = 0; i < rows.size(); ++i)
    {
        const int row = rows.at(i).row();
        if (row < 0 || row >= lastCircuitExplore_.candidates.size())
            continue;
        const DrosofilaLive::CircuitExploreCandidate& c = lastCircuitExplore_.candidates.at(row);
        addWorkingCircuitRoot(c.rootId, c.primaryType, true);
    }
    rebuildWorkingCircuitTable();
    experimentSelectionChanged();
}

void MainWindow::explorerRemoveSelected()
{
    if (!ui->workingCircuitTW->selectionModel())
        return;
    QModelIndexList rows = ui->workingCircuitTW->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b){ return a.row() > b.row(); });
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).row() >= 0 && rows.at(i).row() < workingCircuit_.size())
            workingCircuit_.removeAt(rows.at(i).row());
    rebuildWorkingCircuitTable();
    experimentSelectionChanged();
}

void MainWindow::explorerClearWorkingCircuit()
{
    workingCircuit_.clear();
    rebuildWorkingCircuitTable();
    experimentSelectionChanged();
}

void MainWindow::explorerUseSelectedAsRoot()
{
    const std::uint64_t rootId = selectedExplorerRootId();
    if (!rootId)
        return;
    ui->explorerRootLE->setText(QString::number(static_cast<qulonglong>(rootId)));
    ui->explorerStatusLB->setText(QStringLiteral("Root set to %1 [%2]. Press Explore to continue from this neuron.")
                                  .arg(typeForRoot(rootId)).arg(static_cast<qulonglong>(rootId)));
}

void MainWindow::explorerResultSelectionChanged()
{
    const std::uint64_t rootId = selectedExplorerRootId();
    if (rootId && circuitGraph_)
        circuitGraph_->selectRoot(rootId);
}

void MainWindow::graphZoomChanged(int percent)
{
    if (!ui || !ui->explorerGraphZoomLB)
        return;
    ui->explorerGraphZoomLB->setText(QStringLiteral("%1%").arg(percent));
}

void MainWindow::graphNeuronSelected(qulonglong value)
{
    const std::uint64_t rootId = static_cast<std::uint64_t>(value);
    if (lastCircuitExplore_.valid)
    {
        for (int r = 0; r < lastCircuitExplore_.candidates.size(); ++r)
        {
            if (lastCircuitExplore_.candidates.at(r).rootId == rootId)
            {
                ui->explorerResultsTW->selectRow(r);
                break;
            }
        }
    }
    for (std::size_t i = 0; i < neuronIds_.size(); ++i)
    {
        if (neuronIds_[i] == rootId)
        {
            ui->neuronCB->setCurrentIndex(static_cast<int>(i));
            break;
        }
    }
    if (lastCausalSurprise_.valid)
    {
        for (int r = 0; r < lastCausalSurprise_.entries.size(); ++r)
            if (lastCausalSurprise_.entries.at(r).rootId == rootId) { ui->causalSurpriseTW->selectRow(r); break; }
        if (causalSurprisePlot_) causalSurprisePlot_->selectRoot(rootId);
    }
}

void MainWindow::workingCircuitItemChanged()
{
    for (int r = 0; r < ui->workingCircuitTW->rowCount(); ++r)
    {
        QTableWidgetItem* rootItem = ui->workingCircuitTW->item(r, 1);
        if (!rootItem) continue;
        bool ok = false;
        const std::uint64_t rootId = static_cast<std::uint64_t>(rootItem->text().toULongLong(&ok));
        if (!ok) continue;
        const int idx = workingCircuitIndex(rootId);
        if (idx < 0) continue;
        QTableWidgetItem* watch = ui->workingCircuitTW->item(r, 3);
        QTableWidgetItem* ablate = ui->workingCircuitTW->item(r, 4);
        workingCircuit_[idx].watch = watch && watch->checkState() == Qt::Checked;
        workingCircuit_[idx].ablate = ablate && ablate->checkState() == Qt::Checked;
    }
    experimentSelectionChanged();
}

void MainWindow::explorerCompareSelected()
{
    const std::uint64_t rootId = selectedExplorerRootId();
    if (!rootId)
        return;
    startComparisonForRoot(rootId, typeForRoot(rootId));
}

bool MainWindow::rendererReady() const
{
    return renderer_ != 0;
}

void MainWindow::loadCausalCircuit()
{
    if (!rendererReady() || (experiment_ && experiment_->isRunning())) return;

    stopPlayback(true);

    const QString directory = ui->circuitPathLE->text().trimmed();
    const QDir dir(directory);
    const std::vector<DrosofilaLive::CausalNeuronSpec>& specs = DrosofilaLive::validatedCausalCircuit();
    std::vector<NeuronSkeleton> neurons;
    neurons.reserve(specs.size());
    QStringList lines;
    bool allOk = true;
    unsigned totalSourceEdges = 0;
    unsigned totalTreeSegments = 0;

    DrosofilaLiveDiagnostics::log(QString("MainWindow::loadCausalCircuit: %1").arg(directory));

    for (std::vector<DrosofilaLive::CausalNeuronSpec>::const_iterator spec = specs.begin(); spec != specs.end(); ++spec)
    {
        const QString fileName = QString::fromLatin1(spec->fileName);
        const QString path = dir.filePath(fileName);
        if (!QFileInfo::exists(path))
        {
            allOk = false;
            lines << QStringLiteral("MISSING  %1  [%2]")
                        .arg(QString::fromLatin1(spec->type))
                        .arg(static_cast<qulonglong>(spec->rootId));
            continue;
        }

        NeuronSkeleton neuron;
        DrosofilaLive::PrecomputedSkeletonStats stats;
        QString error;
        const QString displayName = QStringLiteral("%1 - %2")
                                        .arg(QString::fromLatin1(spec->type), QString::fromLatin1(spec->role));
        if (!DrosofilaLive::loadPrecomputedSkeleton(path, spec->rootId, displayName, neuron, stats, error))
        {
            allOk = false;
            lines << QStringLiteral("ERROR    %1  [%2]  %3")
                        .arg(QString::fromLatin1(spec->type))
                        .arg(static_cast<qulonglong>(spec->rootId))
                        .arg(error);
            continue;
        }

        totalSourceEdges += stats.sourceEdges;
        totalTreeSegments += stats.treeSegments;
        neurons.push_back(neuron);
        lines << QStringLiteral("OK       %1  [%2]  %3 vertices, %4 edges")
                    .arg(QString::fromLatin1(spec->type))
                    .arg(static_cast<qulonglong>(spec->rootId))
                    .arg(stats.vertices)
                    .arg(stats.sourceEdges);
    }

    if (!allOk || neurons.size() != specs.size())
    {
        lines << QStringLiteral("")
              << QStringLiteral("Circuit NOT installed: all 7 exact materialization-783 skeletons are required.");
        appendGlobalLogSection(QStringLiteral("Circuit"), lines.join(QLatin1Char('\n')));
        ui->statusbar->showMessage(QStringLiteral("Causal circuit incomplete: scene left unchanged"), 6000);
        DrosofilaLiveDiagnostics::log(QString("Causal circuit rejected:\n%1").arg(lines.join(QLatin1Char('\n'))));
        return;
    }

    DrosofilaLive::SkeletonSceneTransformStats transform;
    if (!DrosofilaLive::normalizeSkeletonScene(neurons, transform, 18.0))
    {
        lines << QStringLiteral("ERROR: global scene normalization failed; scene left unchanged.");
        appendGlobalLogSection(QStringLiteral("Circuit"), lines.join(QLatin1Char('\n')));
        ui->statusbar->showMessage(QStringLiteral("Causal circuit normalization failed"), 6000);
        return;
    }

    installScene(neurons, QStringLiteral("FlyWire v783 validated causal circuit"));
    circuitLoaded_ = true;
    rebuildWorkingCircuitTable();
    if (lastCircuitExplore_.valid)
        populateCircuitExplorerResult(lastCircuitExplore_);
    ui->unitsValueLB->setText(QStringLiteral("nm source / um anatomical"));
    ui->spanValueLB->setText(QStringLiteral("%1 x %2 x %3 um")
        .arg(transform.spanX, 0, 'f', 1)
        .arg(transform.spanY, 0, 'f', 1)
        .arg(transform.spanZ, 0, 'f', 1));

    lines << QStringLiteral("")
          << QStringLiteral("INSTALLED: 7/7 exact root IDs")
          << QStringLiteral("%1 source edges -> %2 rendered tree segments | common display scale=%3")
                .arg(totalSourceEdges).arg(totalTreeSegments).arg(transform.scale, 0, 'f', 6);
    appendGlobalLogSection(QStringLiteral("Circuit"), lines.join(QLatin1Char('\n')));
    ui->playSimulationPB->setEnabled(lastExperiment_.valid && !lastExperiment_.frames.isEmpty());
    ui->playComparisonPB->setEnabled(lastComparison_.valid && !lastComparison_.reference.frames.isEmpty());
    applyAblationVisualState(ablatedRootIdsFromUi());
    ui->statusbar->showMessage(QStringLiteral("Real 7-neuron FlyWire v783 causal circuit loaded"), 6000);
    updateArchiveUi();
    DrosofilaLiveDiagnostics::log(QString("Causal circuit installed:\n%1").arg(lines.join(QLatin1Char('\n'))));
}

void MainWindow::installScene(const std::vector<NeuronSkeleton>& neurons, const QString& sourceLabel)
{
    neuronIds_.clear();
    segmentCount_ = 0;
    nodeCount_ = 0;
    currentSource_ = sourceLabel;

    const QSignalBlocker blockCombo(ui->neuronCB);
    ui->neuronCB->clear();

    for (std::vector<NeuronSkeleton>::const_iterator n = neurons.begin(); n != neurons.end(); ++n)
    {
        neuronIds_.push_back(n->id);
        ui->neuronCB->addItem(QString::fromStdString(n->name));
        nodeCount_ += static_cast<unsigned>(n->nodes.size());
        for (std::vector<SkeletonNode>::const_iterator p = n->nodes.begin(); p != n->nodes.end(); ++p)
            if (p->parent >= 0) ++segmentCount_;
    }

    renderer_->setSkeleton(neurons);
    renderer_->resetView();

    if (!neuronIds_.empty())
        ui->neuronCB->setCurrentIndex(0);

    showSelectedNeuron();
    updateTimelineSelection();
    updateSceneMetrics();
}

void MainWindow::resetView()
{
    if (rendererReady()) renderer_->resetView();
}

void MainWindow::lightingChanged(bool enabled)
{
    if (rendererReady()) renderer_->setLighting(enabled);
}

void MainWindow::surfaceStyle3DChanged(int index)
{
    if (rendererReady()) renderer_->setSurfaceStyle(index);
}

void MainWindow::shadingStyle3DChanged(int index)
{
    if (rendererReady()) renderer_->setShadingStyle(index);
}

void MainWindow::coordinateStyle3DChanged(int index)
{
    if (rendererReady()) renderer_->setCoordinateStyle(index);
}

void MainWindow::smoothMesh3DChanged(bool enabled)
{
    if (rendererReady()) renderer_->setSmoothMesh(enabled);
}

void MainWindow::grid3DChanged(bool enabled)
{
    if (rendererReady()) renderer_->setGridVisible(enabled);
}

void MainWindow::meshLineWidth3DChanged(double width)
{
    if (rendererReady()) renderer_->setMeshLineWidth(width);
}

void MainWindow::orthographic3DChanged(bool enabled)
{
    if (rendererReady()) renderer_->setOrthographic(enabled);
}

void MainWindow::colorPalette3DChanged(int index)
{
    if (rendererReady()) renderer_->setColorPalette(index);
}

std::uint64_t MainWindow::selectedNeuronId() const
{
    const int index = ui->neuronCB->currentIndex();
    if (index < 0 || index >= static_cast<int>(neuronIds_.size())) return 0;
    return neuronIds_[static_cast<std::size_t>(index)];
}

void MainWindow::selectedNeuronChanged(int)
{
    showSelectedNeuron();
    updateTimelineSelection();
}

void MainWindow::showSelectedNeuron()
{
    const std::uint64_t id = selectedNeuronId();
    if (!id)
    {
        ui->neuronIdValueLB->setText("-");
        ui->connectomeNeuronLB->setText(experiment_ && experiment_->isConnectomeLoaded() ?
            "Connectome: no scene neuron selected" : "Connectome: not loaded");
        return;
    }

    ui->neuronIdValueLB->setText(QString::number(static_cast<qulonglong>(id)));
    ui->connectomeNeuronLB->setText(selectedConnectomeInfo(id));
}

void MainWindow::updateSceneMetrics()
{
    ui->backendValueLB->setText(QStringLiteral("QwtPlot3DRenderer"));
    ui->sourceValueLB->setText(currentSource_.isEmpty() ? QStringLiteral("-") : currentSource_);
    ui->neuronsValueLB->setText(QString::number(static_cast<unsigned>(neuronIds_.size())));
    ui->nodesValueLB->setText(QString::number(nodeCount_));
    ui->segmentsValueLB->setText(QString::number(segmentCount_));
    if (ui->unitsValueLB->text().isEmpty()) ui->unitsValueLB->setText("-");
    if (ui->spanValueLB->text().isEmpty()) ui->spanValueLB->setText("-");
}

void MainWindow::updateTimelineSelection()
{
    if (!timeline_)
        return;

    const std::uint64_t id = selectedNeuronId();
    const QString label = ui->neuronCB->currentText();
    timeline_->setNeuron(id, label);

    if (lastComparison_.valid)
    {
        if (!timeline_->hasNeuron(id))
        {
            ui->timelineInfoLB->setText(QStringLiteral("Selected neuron was not watched in the causal comparison."));
            return;
        }

        const QString onset = lastComparison_.sustainedOnsetMs >= 0.0
            ? QStringLiteral(" | TARGET sustained onset %1 ms").arg(lastComparison_.sustainedOnsetMs, 0, 'f', 1)
            : QStringLiteral(" | no sustained TARGET onset detected");
        ui->timelineInfoLB->setText(QStringLiteral("%1 | Reference + Perturbed + Delta | bin %2 ms%3")
            .arg(label)
            .arg(lastComparison_.reference.temporalBinMs, 0, 'f', 1)
            .arg(onset));
        return;
    }

    if (!lastExperiment_.valid)
    {
        ui->timelineInfoLB->setText(QStringLiteral("Timeline: run an experiment or comparison to display temporal activity."));
        return;
    }

    if (!timeline_->hasNeuron(id))
    {
        ui->timelineInfoLB->setText(QStringLiteral("Selected neuron was not watched in the last experiment."));
        return;
    }

    ui->timelineInfoLB->setText(QStringLiteral("%1 | firing rate | bin %2 ms | playback cursor synchronized")
        .arg(label)
        .arg(lastExperiment_.temporalBinMs, 0, 'f', 1));
}

QVector<std::uint64_t> MainWindow::watchedRootIdsFromUi() const
{
    QVector<std::uint64_t> roots;
    const std::vector<DrosofilaLive::CausalNeuronSpec>& specs = DrosofilaLive::validatedCausalCircuit();
    if (specs.size() == 7)
    {
        if (ui->watchDNge031CB->isChecked()) roots.append(specs[0].rootId);
        if (ui->watchCB0393CB->isChecked())  roots.append(specs[1].rootId);
        if (ui->watchCB0177CB->isChecked())  roots.append(specs[2].rootId);
        if (ui->watchCB0248CB->isChecked())  roots.append(specs[3].rootId);
        if (ui->watchCB0366CB->isChecked())  roots.append(specs[4].rootId);
        if (ui->watchDNge062CB->isChecked()) roots.append(specs[5].rootId);
        if (ui->watchCB0701CB->isChecked())  roots.append(specs[6].rootId);
    }
    for (int i = 0; i < workingCircuit_.size(); ++i)
        if (workingCircuit_.at(i).watch && !containsRoot(roots, workingCircuit_.at(i).rootId))
            roots.append(workingCircuit_.at(i).rootId);
    return roots;
}

QVector<std::uint64_t> MainWindow::ablatedRootIdsFromUi() const
{
    QVector<std::uint64_t> roots;
    const std::vector<DrosofilaLive::CausalNeuronSpec>& specs = DrosofilaLive::validatedCausalCircuit();
    if (specs.size() == 7)
    {
        if (ui->ablateDNge031CB->isChecked()) roots.append(specs[0].rootId);
        if (ui->ablateCB0393CB->isChecked())  roots.append(specs[1].rootId);
        if (ui->ablateCB0177CB->isChecked())  roots.append(specs[2].rootId);
        if (ui->ablateCB0248CB->isChecked())  roots.append(specs[3].rootId);
        if (ui->ablateCB0366CB->isChecked())  roots.append(specs[4].rootId);
        if (ui->ablateDNge062CB->isChecked()) roots.append(specs[5].rootId);
        if (ui->ablateCB0701CB->isChecked())  roots.append(specs[6].rootId);
    }
    for (int i = 0; i < workingCircuit_.size(); ++i)
        if (workingCircuit_.at(i).ablate && !containsRoot(roots, workingCircuit_.at(i).rootId))
            roots.append(workingCircuit_.at(i).rootId);
    return roots;
}

QString MainWindow::rootListText(const QVector<std::uint64_t>& rootIds) const
{
    if (rootIds.isEmpty())
        return QStringLiteral("none");

    QStringList names;
    for (int r = 0; r < rootIds.size(); ++r)
        names << typeForRoot(rootIds.at(r));
    return names.join(QStringLiteral(", "));
}

int MainWindow::causalIndexForRoot(std::uint64_t rootId) const
{
    const std::vector<DrosofilaLive::CausalNeuronSpec>& specs = DrosofilaLive::validatedCausalCircuit();
    for (std::size_t i = 0; i < specs.size(); ++i)
        if (specs[i].rootId == rootId)
            return static_cast<int>(i);
    return -1;
}

void MainWindow::initializeEpistasisMatrix()
{
    if (!ui || !ui->epistasisMatrixTW)
        return;

    ui->epistasisMatrixTW->clearContents();
    for (int row = 0; row < 7; ++row)
    {
        for (int column = 0; column < 7; ++column)
        {
            QTableWidgetItem* item = new QTableWidgetItem(row == column ? QStringLiteral("-") : QString());
            item->setTextAlignment(Qt::AlignCenter);
            if (row == column)
                item->setToolTip(QStringLiteral("A neuron is not paired with itself."));
            ui->epistasisMatrixTW->setItem(row, column, item);
        }
    }
}

void MainWindow::storeEpistasisResult(const DrosofilaLive::EpistasisResult& result)
{
    if (!result.valid)
        return;

    bool replaced = false;
    for (int i = 0; i < epistasisResults_.size(); ++i)
    {
        const DrosofilaLive::EpistasisResult& old = epistasisResults_.at(i);
        if ((old.rootA == result.rootA && old.rootB == result.rootB) ||
            (old.rootA == result.rootB && old.rootB == result.rootA))
        {
            epistasisResults_[i] = result;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        epistasisResults_.append(result);

    ui->epistasisMatrixInfoLB->setText(
        QStringLiteral("Interaction matrix: %1/21 pair(s). Cells are Hz; red positive, blue negative; color saturates at |50 Hz|.")
            .arg(epistasisResults_.size()));

    const int row = causalIndexForRoot(result.rootA);
    const int column = causalIndexForRoot(result.rootB);
    if (row < 0 || column < 0)
        return;

    const double value = result.interactionHz;
    const double strength = std::min(1.0, std::fabs(value) / 50.0);
    const int fade = static_cast<int>(70.0 * strength);
    const QColor background = value >= 0.0
        ? QColor(255, 235 - fade, 235 - fade)
        : QColor(235 - fade, 235 - fade, 255);
    const QString text = QString::number(value, 'f', 1);
    const QString tip = QStringLiteral("%1 x %2\nWT %3 Hz\nA OFF %4 Hz\nB OFF %5 Hz\nA+B OFF %6 Hz\nExpected additive %7 Hz\nInteraction %8 Hz")
        .arg(result.typeA, result.typeB)
        .arg(result.wildType.targetMeanHz, 0, 'f', 2)
        .arg(result.aOff.targetMeanHz, 0, 'f', 2)
        .arg(result.bOff.targetMeanHz, 0, 'f', 2)
        .arg(result.doubleOff.targetMeanHz, 0, 'f', 2)
        .arg(result.expectedAdditiveHz, 0, 'f', 2)
        .arg(result.interactionHz, 0, 'f', 2);

    const int rows[2] = { row, column };
    const int cols[2] = { column, row };
    for (int k = 0; k < 2; ++k)
    {
        QTableWidgetItem* item = ui->epistasisMatrixTW->item(rows[k], cols[k]);
        if (!item)
        {
            item = new QTableWidgetItem();
            ui->epistasisMatrixTW->setItem(rows[k], cols[k], item);
        }
        item->setText(text);
        item->setTextAlignment(Qt::AlignCenter);
        item->setBackground(QBrush(background));
        item->setToolTip(tip);
    }
}

DrosofilaLive::ExperimentParameters MainWindow::experimentParametersFromUi() const
{
    DrosofilaLive::ExperimentParameters parameters;
    parameters.durationMs = ui->durationSB->value();
    parameters.sugarRateHz = ui->sugarRateSB->value();
    parameters.trials = ui->trialsSB->value();
    parameters.randomSeed = static_cast<quint32>(ui->seedSB->value());
    parameters.temporalBinMs = 10.0;
    parameters.stimulusPath = defaultStimulusPath();
    parameters.watchedRootIds = watchedRootIdsFromUi();
    parameters.ablatedRootIds = ablatedRootIdsFromUi();
    return parameters;
}

void MainWindow::updateScriptControls()
{
    if (!ui) return;
    const bool bridgeBusy = script_ && script_->isBusy();
    const bool experimentBusy = experiment_ && experiment_->isRunning();
    const bool statisticsBusy = statistics_ && statistics_->isRunning();
    const bool discoveryBusy = discovery_ && discovery_->isRunning();
    const bool canEdit = !bridgeBusy && !experimentBusy && !statisticsBusy && !discoveryBusy && !circuitExplorerBusy_;
    ui->scriptOpenPB->setEnabled(canEdit);
    ui->scriptSavePB->setEnabled(!bridgeBusy);
    ui->scriptCheckPB->setEnabled(canEdit);
    ui->scriptRunPB->setEnabled(canEdit && experiment_);
    ui->scriptStopPB->setEnabled(bridgeBusy);
    ui->scriptEditorTE->setReadOnly(bridgeBusy);
}

void MainWindow::experimentSelectionChanged()
{
    const QVector<std::uint64_t> watched = watchedRootIdsFromUi();
    const QVector<std::uint64_t> ablated = ablatedRootIdsFromUi();

    ui->experimentSelectionLB->setText(QStringLiteral("Watch %1 | Ablated %2 | Working Circuit %3")
        .arg(watched.size()).arg(ablated.size()).arg(workingCircuit_.size()));

    const bool ready = experiment_ && experiment_->isConnectomeLoaded() &&
                       !experiment_->isRunning() && !watched.isEmpty();
    ui->runSimulationPB->setEnabled(ready);
    const bool anchorsReady = experiment_ && experiment_->isConnectomeLoaded() &&
                              experiment_->validateCausalCircuit().complete();
    ui->runComparisonPB->setEnabled(anchorsReady && !experiment_->isRunning());
    ui->runEpistasisPB->setEnabled(anchorsReady && !experiment_->isRunning() && !(statistics_ && statistics_->isRunning()) &&
                                   ui->epistasisNeuronACB->currentIndex() != ui->epistasisNeuronBCB->currentIndex());
    const bool statsPairOk = ui->statisticsNeuronACB->currentIndex() != ui->statisticsNeuronBCB->currentIndex();
    ui->runStatisticsPB->setEnabled(anchorsReady && !experiment_->isRunning() && !(statistics_ && statistics_->isRunning()) &&
                                    !(script_ && script_->isBusy()) && statsPairOk);

    if (circuitLoaded_ && !playbackTimer_->isActive())
    {
        resetExperimentVisualState();
        applyAblationVisualState(ablated);
    }
    updateScriptControls();
}

void MainWindow::clearAblations()
{
    ui->ablateDNge031CB->setChecked(false);
    ui->ablateCB0393CB->setChecked(false);
    ui->ablateCB0177CB->setChecked(false);
    ui->ablateCB0248CB->setChecked(false);
    ui->ablateCB0366CB->setChecked(false);
    ui->ablateDNge062CB->setChecked(false);
    ui->ablateCB0701CB->setChecked(false);
    for (int i = 0; i < workingCircuit_.size(); ++i)
        workingCircuit_[i].ablate = false;
    rebuildWorkingCircuitTable();
    experimentSelectionChanged();
}

void MainWindow::setExperimentControlsEnabled(bool enabled)
{
    const bool scriptBusy = script_ && script_->isBusy();
    const bool interactive = enabled && !scriptBusy;
    const bool connectomeReady = experiment_ && experiment_->isConnectomeLoaded();
    ui->durationSB->setEnabled(interactive);
    ui->sugarRateSB->setEnabled(interactive);
    ui->trialsSB->setEnabled(interactive);
    ui->seedSB->setEnabled(interactive);
    ui->experimentSelectionGB->setEnabled(interactive);
    ui->runSimulationPB->setEnabled(interactive && connectomeReady && !watchedRootIdsFromUi().isEmpty());
    ui->comparisonNeuronCB->setEnabled(interactive);
    ui->epistasisNeuronACB->setEnabled(interactive);
    ui->epistasisNeuronBCB->setEnabled(interactive);
    ui->statisticsNeuronACB->setEnabled(interactive);
    ui->statisticsNeuronBCB->setEnabled(interactive);
    ui->statisticsReplicatesSB->setEnabled(interactive);
    const bool anchorsReady = connectomeReady && experiment_->validateCausalCircuit().complete();
    ui->runComparisonPB->setEnabled(interactive && anchorsReady);
    ui->runEpistasisPB->setEnabled(interactive && anchorsReady &&
                                   ui->epistasisNeuronACB->currentIndex() != ui->epistasisNeuronBCB->currentIndex());
    ui->runStatisticsPB->setEnabled(interactive && anchorsReady &&
                                    ui->statisticsNeuronACB->currentIndex() != ui->statisticsNeuronBCB->currentIndex());
    ui->stopStatisticsPB->setEnabled(statistics_ && statistics_->isRunning());
    ui->clearStatisticsPB->setEnabled(interactive);
    ui->clearEpistasisMatrixPB->setEnabled(interactive);
    ui->loadConnectomePB->setEnabled(interactive);
    ui->loadCircuitPB->setEnabled(interactive && rendererReady());
    ui->connectomePathLE->setEnabled(interactive);
    ui->circuitPathLE->setEnabled(interactive);
    ui->explorerRunPB->setEnabled(interactive && connectomeReady && !circuitExplorerBusy_);
    ui->explorerRootLE->setEnabled(interactive && !circuitExplorerBusy_);
    ui->explorerDirectionCB->setEnabled(interactive && !circuitExplorerBusy_);
    ui->explorerDepthSB->setEnabled(interactive && !circuitExplorerBusy_);
    ui->explorerMinSynSB->setEnabled(interactive && !circuitExplorerBusy_);
    ui->explorerMaxNeuronsSB->setEnabled(interactive && !circuitExplorerBusy_);
    const bool discoveryBusy = discovery_ && discovery_->isRunning();
    const bool discoveryReady = lastCircuitExplore_.valid && !lastCircuitExplore_.candidates.isEmpty();
    const DrosofilaLive::CausalDiscoveryResult discoveryResult = discovery_ ? discovery_->result() : DrosofilaLive::CausalDiscoveryResult();
    const bool discoveryResultsReady = discoveryReady && discoveryResult.valid && !discoveryResult.entries.isEmpty();
    ui->discoveryTopSB->setEnabled(interactive && !discoveryBusy);
    ui->discoverySelectionCB->setEnabled(interactive && !discoveryBusy);
    ui->discoveryScreenReplicatesSB->setEnabled(interactive && !discoveryBusy);
    ui->discoveryRunPB->setEnabled(interactive && connectomeReady && discoveryReady && !discoveryBusy);
    ui->discoveryStopPB->setEnabled(discoveryBusy);
    ui->discoveryReplicatesSB->setEnabled(interactive && !discoveryBusy);
    ui->discoveryValidateTopSB->setEnabled(interactive && !discoveryBusy);
    ui->discoveryValidateSelectedPB->setEnabled(interactive && connectomeReady && discoveryResultsReady && !discoveryBusy);
    ui->discoveryValidateTopPB->setEnabled(interactive && connectomeReady && discoveryResultsReady && !discoveryBusy);
    int validatedDiscoveryHits = 0;
    for (int i = 0; i < discoveryResult.entries.size(); ++i)
    {
        const DrosofilaLive::CausalDiscoveryEntry& entry = discoveryResult.entries.at(i);
        if (entry.validationValid && containsRoot(discoveryResult.validationRootIds, entry.candidate.rootId) && discoveryValidationPassesGate(entry))
            ++validatedDiscoveryHits;
    }
    ui->discoveryEpistasisTopSB->setEnabled(interactive && !discoveryBusy);
    ui->discoveryRequireCiCB->setEnabled(interactive && !discoveryBusy);
    ui->discoveryRequireSignCB->setEnabled(interactive && !discoveryBusy);
    ui->discoveryMinSignDSB->setEnabled(interactive && !discoveryBusy && ui->discoveryRequireSignCB->isChecked());
    ui->discoveryEpistasisReplicatesSB->setEnabled(interactive && !discoveryBusy);
    ui->discoveryEpistasisTopPB->setEnabled(interactive && connectomeReady && validatedDiscoveryHits >= 2 && !discoveryBusy);
    int causalSurpriseValidationRows = 0;
    for (int i = 0; i < discoveryResult.entries.size(); ++i)
        if (discoveryResult.entries.at(i).validationValid && discoveryResult.entries.at(i).validationDelta.n > 0) ++causalSurpriseValidationRows;
    ui->causalSurpriseAnalyzePB->setEnabled(interactive && lastCircuitExplore_.valid && causalSurpriseValidationRows > 0 && !discoveryBusy);
    ui->causalSurpriseReportPB->setEnabled(!causalSurpriseReportPath_.isEmpty() && QFileInfo::exists(causalSurpriseReportPath_));
    ui->discoveryAddSelectedPB->setEnabled(interactive && !discoveryBusy);
    ui->discoveryCompareSelectedPB->setEnabled(interactive && !discoveryBusy);
    updateScriptControls();
    ui->playSimulationPB->setEnabled(interactive && circuitLoaded_ && lastExperiment_.valid && !lastExperiment_.frames.isEmpty());
    ui->playComparisonPB->setEnabled(interactive && circuitLoaded_ && lastComparison_.valid &&
                                     !lastComparison_.reference.frames.isEmpty() &&
                                     !lastComparison_.perturbed.frames.isEmpty());
    updateArchiveUi();
}

std::uint64_t MainWindow::selectedDiscoveryRootId() const
{
    if (!ui || !ui->discoveryResultsTW || !ui->discoveryResultsTW->selectionModel())
        return 0;
    const QModelIndexList rows = ui->discoveryResultsTW->selectionModel()->selectedRows(2);
    if (rows.isEmpty())
        return 0;
    bool ok = false;
    const qulonglong root = rows.first().data().toString().toULongLong(&ok);
    return ok ? static_cast<std::uint64_t>(root) : 0;
}

void MainWindow::populateCausalDiscoveryResult(const DrosofilaLive::CausalDiscoveryResult& result)
{
    const QSignalBlocker blockSignals(ui->discoveryResultsTW);
    ui->discoveryResultsTW->setRowCount(result.entries.size());

    for (int r = 0; r < result.entries.size(); ++r)
    {
        const DrosofilaLive::CausalDiscoveryEntry& e = result.entries.at(r);
        const DrosofilaLive::StatisticalSummary& screen = e.screenDelta;
        const bool screenHave = screen.n > 0;
        const bool validationCiPass = e.validationValid && e.validationDelta.uncertaintyAvailable &&
                                      (e.validationDelta.ci95Low > 0.0 || e.validationDelta.ci95High < 0.0);

        QString statN = QStringLiteral("-");
        QString statMean = QStringLiteral("-");
        QString statStd = QStringLiteral("-");
        QString statSem = QStringLiteral("-");
        QString statLow = QStringLiteral("-");
        QString statHigh = QStringLiteral("-");
        QString statSign = QStringLiteral("-");
        QString ciGate = QStringLiteral("-");
        if (e.validationAttempted)
        {
            statN = QStringLiteral("%1/%2%3")
                .arg(e.validationReplicatesCompleted)
                .arg(e.validationReplicatesRequested)
                .arg(e.validationValid ? QString() : QStringLiteral("*"));
            if (e.validationDelta.n > 0)
            {
                statMean = QString::number(e.validationDelta.mean, 'f', 3);
                statStd = QString::number(e.validationDelta.stddev, 'f', 3);
                statSem = QString::number(e.validationDelta.sem, 'f', 3);
                statLow = e.validationDelta.uncertaintyAvailable
                    ? QString::number(e.validationDelta.ci95Low, 'f', 3) : QStringLiteral("n/a");
                statHigh = e.validationDelta.uncertaintyAvailable
                    ? QString::number(e.validationDelta.ci95High, 'f', 3) : QStringLiteral("n/a");
                statSign = QStringLiteral("%1").arg(100.0 * e.validationDelta.signConsistency, 0, 'f', 1);
                ciGate = e.validationDelta.uncertaintyAvailable ? (validationCiPass ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                                                                : QStringLiteral("n/a");
            }
        }

        const double screenMean = screenHave ? screen.mean : (e.valid ? e.comparison.targetDeltaHz : 0.0);
        const double screenRefMean = e.screenReference.n > 0 ? e.screenReference.mean : (e.valid ? e.comparison.reference.targetMeanHz : 0.0);
        const double screenOffMean = e.screenPerturbed.n > 0 ? e.screenPerturbed.mean : (e.valid ? e.comparison.perturbed.targetMeanHz : 0.0);
        const QString screenN = e.screenReplicatesRequested > 0
            ? QStringLiteral("%1/%2%3").arg(e.screenReplicatesCompleted).arg(e.screenReplicatesRequested).arg(e.valid ? QString() : QStringLiteral("*"))
            : QStringLiteral("-");

        const QString values[20] = {
            e.causalRank > 0 ? QString::number(e.causalRank) : QStringLiteral("-"),
            e.candidate.type.isEmpty() ? QStringLiteral("unannotated") : e.candidate.type,
            QString::number(static_cast<qulonglong>(e.candidate.rootId)),
            QString::number(e.candidate.graphRank),
            QString::number(e.candidate.graphScore, 'f', 3),
            screenN,
            e.screenReference.n > 0 || e.valid ? QString::number(screenRefMean, 'f', 3) : QStringLiteral("ERROR"),
            e.screenPerturbed.n > 0 || e.valid ? QString::number(screenOffMean, 'f', 3) : QStringLiteral("-"),
            screenHave || e.valid ? QString::number(screenMean, 'f', 3) : QStringLiteral("-"),
            screenHave || e.valid ? QString::number(std::fabs(screenMean), 'f', 3) : QStringLiteral("-"),
            screenHave ? QString::number(screen.stddev, 'f', 3) : QStringLiteral("-"),
            screenHave ? QString::number(100.0 * screen.signConsistency, 'f', 1) : QStringLiteral("-"),
            statN,
            statMean,
            statStd,
            statSem,
            statLow,
            statHigh,
            statSign,
            ciGate
        };

        for (int c = 0; c < 20; ++c)
        {
            QTableWidgetItem* item = new QTableWidgetItem(values[c]);
            if (c != 1) item->setTextAlignment(Qt::AlignCenter);
            if (!e.valid && !e.error.isEmpty()) item->setToolTip(e.error);
            if (e.validationAttempted && !e.validationValid && !e.validationError.isEmpty() && c >= 12)
                item->setToolTip(e.validationError);
            if (c == 4)
                item->setToolTip(QStringLiteral("Explorer score only. It ranks graph candidates and is not a causal probability."));
            if (c >= 5 && c <= 11)
                item->setToolTip(QStringLiteral("Coarse causal screen. With Screen N > 1, values are summarized across independent consecutive seeds and causal rank uses |mean Delta|."));
            if (c >= 12)
                item->setToolTip(item->toolTip() + (item->toolTip().isEmpty() ? QString() : QStringLiteral("\n")) +
                    QStringLiteral("Independent confirmatory validation. CI95 uses a two-sided Student-t interval across protocol replicates."));
            ui->discoveryResultsTW->setItem(r, c, item);
        }
    }
    ui->discoveryResultsTW->resizeColumnsToContents();
}

bool MainWindow::discoveryValidationPassesGate(const DrosofilaLive::CausalDiscoveryEntry& entry) const
{
    if (!entry.validationValid)
        return false;
    const bool ciPass = entry.validationDelta.uncertaintyAvailable &&
                        (entry.validationDelta.ci95Low > 0.0 || entry.validationDelta.ci95High < 0.0);
    const bool signPass = entry.validationDelta.signConsistency * 100.0 + 1e-12 >= ui->discoveryMinSignDSB->value();
    if (ui->discoveryRequireCiCB->isChecked() && !ciPass) return false;
    if (ui->discoveryRequireSignCB->isChecked() && !signPass) return false;
    return true;
}

QString MainWindow::discoveryGateText() const
{
    QStringList parts;
    if (ui->discoveryRequireCiCB->isChecked()) parts << QStringLiteral("CI95 excludes zero");
    if (ui->discoveryRequireSignCB->isChecked())
        parts << QStringLiteral("sign >= %1%").arg(ui->discoveryMinSignDSB->value(), 0, 'f', 1);
    return parts.isEmpty() ? QStringLiteral("NONE") : parts.join(QStringLiteral(" AND "));
}

void MainWindow::populateCausalDiscoveryEpistasisResult(const DrosofilaLive::CausalDiscoveryResult& result)
{
    const QSignalBlocker blockSignals(ui->discoveryEpistasisTW);
    ui->discoveryEpistasisTW->setRowCount(result.epistasisEntries.size());

    for (int r = 0; r < result.epistasisEntries.size(); ++r)
    {
        const DrosofilaLive::CausalDiscoveryEpistasisEntry& e = result.epistasisEntries.at(r);
        const bool have = e.interaction.n > 0;
        const QString ciLow = have && e.interaction.uncertaintyAvailable
            ? QString::number(e.interaction.ci95Low, 'f', 3) : (have ? QStringLiteral("n/a") : QStringLiteral("-"));
        const QString ciHigh = have && e.interaction.uncertaintyAvailable
            ? QString::number(e.interaction.ci95High, 'f', 3) : (have ? QStringLiteral("n/a") : QStringLiteral("-"));
        const QString nText = QStringLiteral("%1/%2%3")
            .arg(e.replicatesCompleted).arg(e.replicatesRequested)
            .arg(e.valid ? QString() : QStringLiteral("*"));

        const QString values[17] = {
            e.interactionRank > 0 ? QString::number(e.interactionRank) : QStringLiteral("-"),
            e.typeA.isEmpty() ? QStringLiteral("unannotated") : e.typeA,
            QString::number(static_cast<qulonglong>(e.rootA)),
            e.typeB.isEmpty() ? QStringLiteral("unannotated") : e.typeB,
            QString::number(static_cast<qulonglong>(e.rootB)),
            nText,
            e.wildType.n > 0 ? QString::number(e.wildType.mean, 'f', 3) : QStringLiteral("-"),
            e.aOff.n > 0 ? QString::number(e.aOff.mean, 'f', 3) : QStringLiteral("-"),
            e.bOff.n > 0 ? QString::number(e.bOff.mean, 'f', 3) : QStringLiteral("-"),
            e.doubleOff.n > 0 ? QString::number(e.doubleOff.mean, 'f', 3) : QStringLiteral("-"),
            e.expectedAdditive.n > 0 ? QString::number(e.expectedAdditive.mean, 'f', 3) : QStringLiteral("-"),
            have ? QString::number(e.interaction.mean, 'f', 3) : QStringLiteral("ERROR"),
            have ? QString::number(e.interaction.stddev, 'f', 3) : QStringLiteral("-"),
            have ? QString::number(e.interaction.sem, 'f', 3) : QStringLiteral("-"),
            ciLow,
            ciHigh,
            have ? QStringLiteral("%1").arg(100.0 * e.interaction.signConsistency, 0, 'f', 1) : QStringLiteral("-")
        };

        for (int c = 0; c < 17; ++c)
        {
            QTableWidgetItem* item = new QTableWidgetItem(values[c]);
            if (c != 1 && c != 3) item->setTextAlignment(Qt::AlignCenter);
            QString tip = QStringLiteral("A validated mean Delta: %1 Hz | B validated mean Delta: %2 Hz\nInteraction = observed(A+B OFF) - [A OFF + B OFF - WT].")
                .arg(e.validatedDeltaAHz, 0, 'f', 3)
                .arg(e.validatedDeltaBHz, 0, 'f', 3);
            if (!e.error.isEmpty())
                tip += QStringLiteral("\n") + e.error;
            if (c >= 11)
                tip += QStringLiteral("\nCI95 uses a two-sided Student-t interval across independent-seed epistasis replicates.");
            item->setToolTip(tip);
            ui->discoveryEpistasisTW->setItem(r, c, item);
        }
    }
    ui->discoveryEpistasisTW->resizeColumnsToContents();
}

void MainWindow::startCausalDiscovery()
{
    if (!discovery_ || discovery_->isRunning() || !experiment_ || !experiment_->isConnectomeLoaded() ||
        experiment_->isRunning() || circuitExplorerBusy_ || (statistics_ && statistics_->isRunning()) ||
        (script_ && script_->isBusy()) || !lastCircuitExplore_.valid || lastCircuitExplore_.candidates.isEmpty())
        return;

    const QString stimulusPath = defaultStimulusPath();
    if (!QFileInfo::exists(stimulusPath))
    {
        setIdleActivityMessage(QStringLiteral("Causal Discovery ERROR: local Sugar stimulus file not found."));
        appendGlobalLogSection(QStringLiteral("Discovery"), QStringLiteral("Expected: %1").arg(stimulusPath));
        return;
    }

    const int requested = ui->discoveryTopSB->value();
    const bool layered = ui->discoverySelectionCB->currentIndex() == 1;
    QVector<DrosofilaLive::CausalDiscoveryCandidate> candidates;
    QVector<std::uint64_t> seen;
    if (layered)
    {
        for (int depth = 1; depth <= lastCircuitExplore_.maxDepth; ++depth)
        {
            int added = 0;
            for (int i = 0; i < lastCircuitExplore_.candidates.size() && added < requested; ++i)
            {
                const DrosofilaLive::CircuitExploreCandidate& source = lastCircuitExplore_.candidates.at(i);
                if (source.depth != depth || containsRoot(seen, source.rootId)) continue;
                DrosofilaLive::CausalDiscoveryCandidate candidate;
                candidate.rootId = source.rootId; candidate.type = source.primaryType; candidate.graphRank = i + 1;
                candidate.depth = source.depth; candidate.synapseStrength = source.synapseStrength; candidate.graphScore = source.score;
                candidates.append(candidate); seen.append(source.rootId); ++added;
            }
        }
    }
    else
    {
        const int n = std::min(requested, lastCircuitExplore_.candidates.size());
        candidates.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            const DrosofilaLive::CircuitExploreCandidate& source = lastCircuitExplore_.candidates.at(i);
            DrosofilaLive::CausalDiscoveryCandidate candidate;
            candidate.rootId = source.rootId; candidate.type = source.primaryType; candidate.graphRank = i + 1;
            candidate.depth = source.depth; candidate.synapseStrength = source.synapseStrength; candidate.graphScore = source.score;
            candidates.append(candidate);
        }
    }
    const int screenReplicates = ui->discoveryScreenReplicatesSB->value();

    DrosofilaLive::ExperimentParameters parameters = experimentParametersFromUi();
    parameters.stimulusPath = stimulusPath;
    parameters.ablatedRootIds.clear();

    const std::uint64_t targetId = 720575940660219265ULL;
    if (!containsRoot(parameters.watchedRootIds, targetId))
        parameters.watchedRootIds.append(targetId);

    stopPlayback(true);
    ui->discoveryResultsTW->setRowCount(0);
    ui->discoveryEpistasisTW->setRowCount(0);
    ui->discoveryStatusLB->setText(QStringLiteral("Screening %1 candidate(s), %2 seed replicate(s) each...").arg(candidates.size()).arg(screenReplicates));
    causalDiscoveryGuard_ = true;
    setExperimentControlsEnabled(false);
    ui->discoveryStopPB->setEnabled(true);
    beginActivity(QStringLiteral("Causal Discovery"), screenReplicates > 1 ? QStringLiteral("Multi-seed coarse causal screening") : QStringLiteral("Fast single-seed causal screening"));
    appendGlobalLogSection(QStringLiteral("Discovery"),
        QStringLiteral("Target: CB0701/MN9 [%1]\nExplorer root: %2 [%3]\nSelection: %4 | requested N %5 | actual candidates %6\nScreen replicates/candidate: %7 | seeds %8 .. %9\nProtocol: Sugar %10 Hz | %11 ms | %12 trial(s)\nRanking after screen: descending |mean TARGET delta Hz|")
            .arg(static_cast<qulonglong>(targetId))
            .arg(lastCircuitExplore_.rootType)
            .arg(static_cast<qulonglong>(lastCircuitExplore_.rootId))
            .arg(layered ? QStringLiteral("Top N per depth") : QStringLiteral("Global Top N"))
            .arg(requested)
            .arg(candidates.size())
            .arg(screenReplicates)
            .arg(parameters.randomSeed)
            .arg(static_cast<quint64>(parameters.randomSeed) + static_cast<quint64>(screenReplicates - 1))
            .arg(parameters.sugarRateHz, 0, 'f', 1)
            .arg(parameters.durationMs, 0, 'f', 1)
            .arg(parameters.trials));

    if (!discovery_->runScreen(parameters, candidates, screenReplicates))
    {
        causalDiscoveryGuard_ = false;
        setExperimentControlsEnabled(true);
        ui->discoveryStopPB->setEnabled(false);
        finishActivity(QStringLiteral("Unable to start causal discovery."), false);
    }
    else
    {
        DrosofilaLive::CausalDiscoveryProvenance provenance;
        provenance.valid = true;
        provenance.origin = QStringLiteral("GUI");
        provenance.explorerParameters.rootText = lastCircuitExplore_.rootQuery.isEmpty()
            ? QString::number(static_cast<qulonglong>(lastCircuitExplore_.rootId)) : lastCircuitExplore_.rootQuery;
        provenance.explorerParameters.direction = lastCircuitExplore_.direction;
        provenance.explorerParameters.maxDepth = lastCircuitExplore_.maxDepth;
        provenance.explorerParameters.minimumSynapses = lastCircuitExplore_.minimumSynapses;
        provenance.explorerParameters.maximumNeurons = lastCircuitExplore_.maximumNeurons;
        provenance.selectionMode = layered ? QStringLiteral("LAYERED") : QStringLiteral("GLOBAL");
        provenance.selectionTopN = requested;
        provenance.selectedCandidates = candidates.size();
        provenance.screenParametersValid = true;
        provenance.screenParameters = parameters;
        const bool requireCi = ui->discoveryRequireCiCB->isChecked();
        const bool requireSign = ui->discoveryRequireSignCB->isChecked();
        provenance.epistasisGateMode = requireCi && requireSign ? QStringLiteral("BOTH")
            : (requireCi ? QStringLiteral("CI95") : (requireSign ? QStringLiteral("SIGN") : QStringLiteral("NONE")));
        provenance.epistasisMinimumSignConsistency = ui->discoveryMinSignDSB->value() / 100.0;
        provenance.startedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        provenance.updatedUtc = provenance.startedUtc;
        discovery_->setProvenance(provenance);
        archiveSourcePath_.clear();
        archiveCreatedUtc_.clear();
        updateArchiveUi();
    }
}

void MainWindow::stopCausalDiscovery()
{
    if (!discovery_ || !discovery_->isRunning())
        return;
    const DrosofilaLive::CausalDiscoveryOperation op = discovery_->operation();
    const bool validating = op == DrosofilaLive::CausalDiscoveryValidation;
    const bool epistasis = op == DrosofilaLive::CausalDiscoveryEpistasis;
    discovery_->requestStop();
    ui->discoveryStopPB->setEnabled(false);
    ui->discoveryStatusLB->setText(epistasis
        ? QStringLiteral("Stop requested; the current WT/A OFF/B OFF/A+B OFF epistasis replicate will finish first.")
        : (validating
            ? QStringLiteral("Stop requested; the current validation Reference/Perturbed comparison will finish first.")
            : QStringLiteral("Stop requested; the current screening Reference/Perturbed comparison will finish first.")));
    updateActivityDetail(0, epistasis
        ? QStringLiteral("Stop requested after current epistasis replicate")
        : (validating
            ? QStringLiteral("Stop requested after current validation replicate")
            : QStringLiteral("Stop requested after current candidate")));
}

void MainWindow::causalDiscoveryProgress(int percent, const QString& phase)
{
    if (!causalDiscoveryGuard_ || (script_ && script_->isBusy()))
        return;
    if (reproductionActive_)
    {
        int base = 10;
        int span = 40;
        QString stage = QStringLiteral("REPRODUCE 2/4 - Screen");
        if (reproductionPhase_ == ReproductionValidation) { base = 50; span = 25; stage = QStringLiteral("REPRODUCE 3/4 - Validation"); }
        else if (reproductionPhase_ == ReproductionEpistasis) { base = 75; span = 25; stage = QStringLiteral("REPRODUCE 4/4 - Epistasis"); }
        updateActivityOverall(base + (std::max(0, std::min(100, percent)) * span) / 100, stage);
        updateActivityDetail(percent, phase);
        ui->archiveReproduceStatusLB->setText(QStringLiteral("%1 - %2").arg(stage, phase));
        return;
    }
    const DrosofilaLive::CausalDiscoveryOperation op = discovery_ ? discovery_->operation() : DrosofilaLive::CausalDiscoveryNone;
    const bool validating = op == DrosofilaLive::CausalDiscoveryValidation;
    const bool epistasis = op == DrosofilaLive::CausalDiscoveryEpistasis;
    updateActivityOverall(percent, epistasis ? QStringLiteral("Validated pairwise epistasis")
                                             : (validating ? QStringLiteral("Statistical validation")
                                                           : QStringLiteral("Automated candidate screening")));
    updateActivityDetail(percent, phase);
    ui->discoveryStatusLB->setText(phase);
}

void MainWindow::causalDiscoveryCandidateFinished(const DrosofilaLive::CausalDiscoveryEntry& entry, int completed, int total)
{
    if (!causalDiscoveryGuard_ || !discovery_)
        return;
    if (reproductionActive_)
    {
        ui->archiveReproduceStatusLB->setText(QStringLiteral("REPRODUCE - %1/%2 %3 [%4]")
            .arg(completed).arg(total)
            .arg(entry.candidate.type.isEmpty() ? QStringLiteral("unannotated") : entry.candidate.type)
            .arg(static_cast<qulonglong>(entry.candidate.rootId)));
        return;
    }
    populateCausalDiscoveryResult(discovery_->result());
    if (script_ && script_->isBusy())
        return;
    const QString label = entry.candidate.type.isEmpty() ? QStringLiteral("unannotated") : entry.candidate.type;

    if (discovery_->operation() == DrosofilaLive::CausalDiscoveryValidation)
    {
        if (entry.validationDelta.n > 0)
        {
            appendGlobalLog(QStringLiteral("[VALIDATION %1/%2] %3 [%4] | N %5/%6 | mean delta %7 Hz | std %8 | CI95 [%9, %10] | sign %11%% %12")
                .arg(completed).arg(total).arg(label)
                .arg(static_cast<qulonglong>(entry.candidate.rootId))
                .arg(entry.validationReplicatesCompleted).arg(entry.validationReplicatesRequested)
                .arg(entry.validationDelta.mean, 0, 'f', 3)
                .arg(entry.validationDelta.stddev, 0, 'f', 3)
                .arg(entry.validationDelta.ci95Low, 0, 'f', 3)
                .arg(entry.validationDelta.ci95High, 0, 'f', 3)
                .arg(100.0 * entry.validationDelta.signConsistency, 0, 'f', 1)
                .arg(entry.validationValid ? QStringLiteral("OK") : QStringLiteral("PARTIAL")));
        }
        else
        {
            appendGlobalLog(QStringLiteral("[VALIDATION %1/%2 ERROR] %3 [%4] | %5")
                .arg(completed).arg(total).arg(label)
                .arg(static_cast<qulonglong>(entry.candidate.rootId)).arg(entry.validationError));
        }
        return;
    }

    if (entry.valid)
    {
        const double meanDelta = entry.screenDelta.n > 0 ? entry.screenDelta.mean : entry.comparison.targetDeltaHz;
        const double refMean = entry.screenReference.n > 0 ? entry.screenReference.mean : entry.comparison.reference.targetMeanHz;
        const double offMean = entry.screenPerturbed.n > 0 ? entry.screenPerturbed.mean : entry.comparison.perturbed.targetMeanHz;
        appendGlobalLog(QStringLiteral("[DISCOVERY %1/%2] %3 [%4] | graph rank %5 | screen N %6/%7 | ref mean %8 Hz | OFF mean %9 Hz | mean delta %10 Hz | std %11 | sign %12%%")
            .arg(completed).arg(total).arg(label)
            .arg(static_cast<qulonglong>(entry.candidate.rootId))
            .arg(entry.candidate.graphRank)
            .arg(entry.screenReplicatesCompleted).arg(entry.screenReplicatesRequested)
            .arg(refMean, 0, 'f', 3)
            .arg(offMean, 0, 'f', 3)
            .arg(meanDelta, 0, 'f', 3)
            .arg(entry.screenDelta.stddev, 0, 'f', 3)
            .arg(100.0 * entry.screenDelta.signConsistency, 0, 'f', 1));
    }
    else
    {
        appendGlobalLog(QStringLiteral("[DISCOVERY %1/%2 ERROR] %3 [%4] | %5")
            .arg(completed).arg(total).arg(label)
            .arg(static_cast<qulonglong>(entry.candidate.rootId)).arg(entry.error));
    }
}

void MainWindow::causalDiscoveryEpistasisPairFinished(const DrosofilaLive::CausalDiscoveryEpistasisEntry& entry,
                                                        int completed, int total)
{
    if (!causalDiscoveryGuard_ || !discovery_)
        return;
    if (reproductionActive_)
    {
        ui->archiveReproduceStatusLB->setText(QStringLiteral("REPRODUCE epistasis %1/%2 - %3 x %4")
            .arg(completed).arg(total)
            .arg(entry.typeA.isEmpty() ? QStringLiteral("A") : entry.typeA)
            .arg(entry.typeB.isEmpty() ? QStringLiteral("B") : entry.typeB));
        return;
    }

    populateCausalDiscoveryEpistasisResult(discovery_->result());
    if (script_ && script_->isBusy())
        return;
    const QString a = entry.typeA.isEmpty() ? QStringLiteral("unannotated") : entry.typeA;
    const QString b = entry.typeB.isEmpty() ? QStringLiteral("unannotated") : entry.typeB;
    if (entry.interaction.n > 0)
    {
        const QString ciLow = entry.interaction.uncertaintyAvailable
            ? QString::number(entry.interaction.ci95Low, 'f', 3) : QStringLiteral("n/a");
        const QString ciHigh = entry.interaction.uncertaintyAvailable
            ? QString::number(entry.interaction.ci95High, 'f', 3) : QStringLiteral("n/a");
        appendGlobalLog(QStringLiteral("[DISCOVERY EPISTASIS %1/%2] %3 [%4] x %5 [%6] | N %7/%8 | interaction %9 Hz | std %10 | CI95 [%11, %12] | sign %13%% %14")
            .arg(completed).arg(total)
            .arg(a).arg(static_cast<qulonglong>(entry.rootA))
            .arg(b).arg(static_cast<qulonglong>(entry.rootB))
            .arg(entry.replicatesCompleted).arg(entry.replicatesRequested)
            .arg(entry.interaction.mean, 0, 'f', 3)
            .arg(entry.interaction.stddev, 0, 'f', 3)
            .arg(ciLow)
            .arg(ciHigh)
            .arg(100.0 * entry.interaction.signConsistency, 0, 'f', 1)
            .arg(entry.valid ? QStringLiteral("OK") : QStringLiteral("PARTIAL")));
    }
    else
    {
        appendGlobalLog(QStringLiteral("[DISCOVERY EPISTASIS %1/%2 ERROR] %3 [%4] x %5 [%6] | %7")
            .arg(completed).arg(total)
            .arg(a).arg(static_cast<qulonglong>(entry.rootA))
            .arg(b).arg(static_cast<qulonglong>(entry.rootB))
            .arg(entry.error));
    }
}

void MainWindow::causalDiscoveryFinished()
{
    if (!discovery_)
        return;
    if (reproductionActive_)
    {
        handleReproductionDiscoveryFinished();
        return;
    }

    invalidateCausalSurprise(QStringLiteral("Discovery/validation changed; run Causal Surprise analysis."));
    const DrosofilaLive::CausalDiscoveryResult result = discovery_->result();
    const DrosofilaLive::CausalDiscoveryOperation operation = discovery_->lastOperation();
    // Discovery may have been launched by cvBasic, so mark a previously loaded archive dirty here too.
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    updateArchiveUi();
    populateCausalDiscoveryResult(result);
    populateCausalDiscoveryEpistasisResult(result);
    setExperimentControlsEnabled(true);
    ui->discoveryStopPB->setEnabled(false);

    if (script_ && script_->isBusy())
    {
        if (operation == DrosofilaLive::CausalDiscoveryScreen)
            ui->discoveryStatusLB->setText(QStringLiteral("Script screening complete: %1/%2 candidate(s).").arg(result.candidatesCompleted).arg(result.candidatesRequested));
        else if (operation == DrosofilaLive::CausalDiscoveryValidation)
            ui->discoveryStatusLB->setText(QStringLiteral("Script validation complete: %1/%2 candidate(s).").arg(result.validationCandidatesCompleted).arg(result.validationCandidatesRequested));
        else if (operation == DrosofilaLive::CausalDiscoveryEpistasis)
            ui->discoveryStatusLB->setText(QStringLiteral("Script epistasis complete: %1/%2 pair(s).").arg(result.epistasisPairsCompleted).arg(result.epistasisPairsRequested));
        return;
    }

    if (operation == DrosofilaLive::CausalDiscoveryEpistasis)
    {
        QStringList lines;
        int validCount = 0;
        int partialCount = 0;
        lines << QStringLiteral("Pairs processed: %1/%2%3 | replicates requested: %4")
                     .arg(result.epistasisPairsCompleted)
                     .arg(result.epistasisPairsRequested)
                     .arg(result.stopped ? QStringLiteral(" | STOPPED") : QString())
                     .arg(result.epistasisReplicatesRequested);
        for (int i = 0; i < result.epistasisEntries.size(); ++i)
        {
            const DrosofilaLive::CausalDiscoveryEpistasisEntry& e = result.epistasisEntries.at(i);
            if (e.interaction.n <= 0)
            {
                ++partialCount;
                lines << QStringLiteral("%1 [%2] x %3 [%4] | EPISTASIS ERROR | %5")
                            .arg(e.typeA.isEmpty() ? QStringLiteral("unannotated") : e.typeA)
                            .arg(static_cast<qulonglong>(e.rootA))
                            .arg(e.typeB.isEmpty() ? QStringLiteral("unannotated") : e.typeB)
                            .arg(static_cast<qulonglong>(e.rootB))
                            .arg(e.error);
                continue;
            }
            if (e.valid) ++validCount; else ++partialCount;
            const QString ciLow = e.interaction.uncertaintyAvailable ? QString::number(e.interaction.ci95Low, 'f', 3) : QStringLiteral("n/a");
            const QString ciHigh = e.interaction.uncertaintyAvailable ? QString::number(e.interaction.ci95High, 'f', 3) : QStringLiteral("n/a");
            lines << QStringLiteral("#%1  %2 [%3] x %4 [%5] | N %6/%7 | interaction %8 Hz | std %9 | SEM %10 | CI95 [%11, %12] | sign %13%% %14")
                        .arg(e.interactionRank > 0 ? QString::number(e.interactionRank) : QStringLiteral("-"))
                        .arg(e.typeA.isEmpty() ? QStringLiteral("unannotated") : e.typeA)
                        .arg(static_cast<qulonglong>(e.rootA))
                        .arg(e.typeB.isEmpty() ? QStringLiteral("unannotated") : e.typeB)
                        .arg(static_cast<qulonglong>(e.rootB))
                        .arg(e.replicatesCompleted).arg(e.replicatesRequested)
                        .arg(e.interaction.mean, 0, 'f', 3)
                        .arg(e.interaction.stddev, 0, 'f', 3)
                        .arg(e.interaction.sem, 0, 'f', 3)
                        .arg(ciLow).arg(ciHigh)
                        .arg(100.0 * e.interaction.signConsistency, 0, 'f', 1)
                        .arg(e.valid ? QStringLiteral("OK") : QStringLiteral("PARTIAL"));
        }
        if (!result.error.isEmpty())
            lines << QStringLiteral("ERROR: %1").arg(result.error);
        appendGlobalLogSection(QStringLiteral("Discovery epistasis ranking"), lines.join(QLatin1Char('\n')));

        const bool haveStats = (validCount + partialCount) > 0;
        if (haveStats)
        {
            ui->discoveryStatusLB->setText(result.stopped
                ? QStringLiteral("Epistasis stopped with partial results. Full pairs: %1, partial: %2.").arg(validCount).arg(partialCount)
                : QStringLiteral("Epistasis complete. %1 pair(s) fully validated, ranked by |mean interaction Hz|; %2 incomplete.").arg(validCount).arg(partialCount));
            finishActivity(result.stopped ? QStringLiteral("Discovery epistasis stopped with partial results.")
                                          : QStringLiteral("Discovery pairwise epistasis complete."), true);
        }
        else
        {
            ui->discoveryStatusLB->setText(QStringLiteral("Discovery epistasis failed: %1").arg(result.error));
            finishActivity(QStringLiteral("Discovery pairwise epistasis failed."), false);
        }

        QTimer::singleShot(0, this, SLOT(causalDiscoveryReleaseGuard()));
        return;
    }

    if (operation == DrosofilaLive::CausalDiscoveryValidation)
    {
        QStringList lines;
        int validCount = 0;
        int gatePassCount = 0;
        int partialCount = 0;
        lines << QStringLiteral("Candidates processed: %1/%2%3 | replicates requested: %4")
                     .arg(result.validationCandidatesCompleted)
                     .arg(result.validationCandidatesRequested)
                     .arg(result.stopped ? QStringLiteral(" | STOPPED") : QString())
                     .arg(result.validationReplicatesRequested);
        for (int i = 0; i < result.entries.size(); ++i)
        {
            const DrosofilaLive::CausalDiscoveryEntry& e = result.entries.at(i);
            if (!containsRoot(result.validationRootIds, e.candidate.rootId) || !e.validationAttempted) continue;
            if (e.validationDelta.n <= 0)
            {
                ++partialCount;
                lines << QStringLiteral("#%1  %2 [%3] | VALIDATION ERROR | %4")
                            .arg(e.causalRank)
                            .arg(e.candidate.type.isEmpty() ? QStringLiteral("unannotated") : e.candidate.type)
                            .arg(static_cast<qulonglong>(e.candidate.rootId))
                            .arg(e.validationError);
                continue;
            }
            if (e.validationValid) { ++validCount; if (discoveryValidationPassesGate(e)) ++gatePassCount; } else ++partialCount;
            const QString ciLow = e.validationDelta.uncertaintyAvailable ? QString::number(e.validationDelta.ci95Low, 'f', 3) : QStringLiteral("n/a");
            const QString ciHigh = e.validationDelta.uncertaintyAvailable ? QString::number(e.validationDelta.ci95High, 'f', 3) : QStringLiteral("n/a");
            lines << QStringLiteral("#%1  %2 [%3] | N %4/%5 | mean delta %6 Hz | std %7 | SEM %8 | CI95 [%9, %10] | sign %11%% %12")
                        .arg(e.causalRank)
                        .arg(e.candidate.type.isEmpty() ? QStringLiteral("unannotated") : e.candidate.type)
                        .arg(static_cast<qulonglong>(e.candidate.rootId))
                        .arg(e.validationReplicatesCompleted).arg(e.validationReplicatesRequested)
                        .arg(e.validationDelta.mean, 0, 'f', 3)
                        .arg(e.validationDelta.stddev, 0, 'f', 3)
                        .arg(e.validationDelta.sem, 0, 'f', 3)
                        .arg(ciLow).arg(ciHigh)
                        .arg(100.0 * e.validationDelta.signConsistency, 0, 'f', 1)
                        .arg(e.validationValid ? QStringLiteral("OK") : QStringLiteral("PARTIAL"));
        }
        if (!result.error.isEmpty())
            lines << QStringLiteral("ERROR: %1").arg(result.error);
        appendGlobalLogSection(QStringLiteral("Discovery statistical validation"), lines.join(QLatin1Char('\n')));

        const bool haveStats = (validCount + partialCount) > 0;
        if (haveStats)
        {
            ui->discoveryStatusLB->setText(result.stopped
                ? QStringLiteral("Validation stopped with partial statistics. Full: %1, partial: %2.").arg(validCount).arg(partialCount)
                : (gatePassCount >= 2
                    ? QStringLiteral("Validation complete. %1/%2 full hit(s) pass gate [%3]; %4 incomplete. Epistasis Top N is ready.").arg(gatePassCount).arg(validCount).arg(discoveryGateText()).arg(partialCount)
                    : QStringLiteral("Validation complete. %1 full hit(s), but only %2 pass gate [%3]; %4 incomplete.").arg(validCount).arg(gatePassCount).arg(discoveryGateText()).arg(partialCount)));
            finishActivity(result.stopped ? QStringLiteral("Causal validation stopped with partial results.")
                                          : QStringLiteral("Causal statistical validation complete."), true);
        }
        else
        {
            ui->discoveryStatusLB->setText(QStringLiteral("Causal validation failed: %1").arg(result.error));
            finishActivity(QStringLiteral("Causal statistical validation failed."), false);
        }

        QTimer::singleShot(0, this, SLOT(causalDiscoveryReleaseGuard()));
        return;
    }

    QStringList lines;
    lines << QStringLiteral("Completed: %1/%2 candidate(s)%3 | screen replicates/candidate: %4")
                 .arg(result.candidatesCompleted).arg(result.candidatesRequested)
                 .arg(result.stopped ? QStringLiteral(" | STOPPED") : QString())
                 .arg(result.screenReplicatesRequested);
    for (int i = 0; i < result.entries.size(); ++i)
    {
        const DrosofilaLive::CausalDiscoveryEntry& e = result.entries.at(i);
        if (!e.valid) continue;
        const double meanDelta = e.screenDelta.n > 0 ? e.screenDelta.mean : e.comparison.targetDeltaHz;
        lines << QStringLiteral("#%1  %2 [%3] | graph #%4 score %5 | screen N %6/%7 | mean delta %8 Hz | std %9 | sign %10%%")
                    .arg(e.causalRank)
                    .arg(e.candidate.type.isEmpty() ? QStringLiteral("unannotated") : e.candidate.type)
                    .arg(static_cast<qulonglong>(e.candidate.rootId))
                    .arg(e.candidate.graphRank)
                    .arg(e.candidate.graphScore, 0, 'f', 3)
                    .arg(e.screenReplicatesCompleted).arg(e.screenReplicatesRequested)
                    .arg(meanDelta, 0, 'f', 3)
                    .arg(e.screenDelta.stddev, 0, 'f', 3)
                    .arg(100.0 * e.screenDelta.signConsistency, 0, 'f', 1);
    }
    if (!result.error.isEmpty())
        lines << QStringLiteral("ERROR: %1").arg(result.error);
    appendGlobalLogSection(QStringLiteral("Discovery ranking"), lines.join(QLatin1Char('\n')));

    if (result.valid)
    {
        ui->discoveryStatusLB->setText(result.stopped
            ? QStringLiteral("Screen stopped after %1/%2 candidate(s). Partial ranking shown.").arg(result.candidatesCompleted).arg(result.candidatesRequested)
            : QStringLiteral("Screen complete: %1 candidate(s), %2 replicate(s)/candidate, ranked by |mean TARGET delta Hz|. Select hits or validate Top N.").arg(result.candidatesCompleted).arg(result.screenReplicatesRequested));
        finishActivity(result.stopped ? QStringLiteral("Causal discovery stopped with partial results.")
                                      : QStringLiteral("Causal discovery complete."), true);
    }
    else
    {
        ui->discoveryStatusLB->setText(QStringLiteral("Causal discovery failed: %1").arg(result.error));
        finishActivity(QStringLiteral("Causal discovery failed."), false);
    }

    // Keep the guard through the rest of the ExperimentController::comparisonFinished()
    // signal delivery. Otherwise the last discovery comparison would be mistaken for a
    // normal user comparison by MainWindow.
    QTimer::singleShot(0, this, SLOT(causalDiscoveryReleaseGuard()));
}

void MainWindow::causalDiscoveryReleaseGuard()
{
    causalDiscoveryGuard_ = false;
}

void MainWindow::discoveryAddSelected()
{
    if (!ui->discoveryResultsTW->selectionModel())
        return;
    const QModelIndexList rows = ui->discoveryResultsTW->selectionModel()->selectedRows();
    for (int i = 0; i < rows.size(); ++i)
    {
        const int row = rows.at(i).row();
        if (row < 0 || row >= ui->discoveryResultsTW->rowCount())
            continue;
        bool ok = false;
        const std::uint64_t rootId = static_cast<std::uint64_t>(ui->discoveryResultsTW->item(row, 2)->text().toULongLong(&ok));
        if (!ok || rootId == 0)
            continue;
        addWorkingCircuitRoot(rootId, ui->discoveryResultsTW->item(row, 1)->text(), true);
    }
    rebuildWorkingCircuitTable();
    experimentSelectionChanged();
}

void MainWindow::discoveryCompareSelected()
{
    if (discovery_ && discovery_->isRunning())
        return;
    const std::uint64_t rootId = selectedDiscoveryRootId();
    if (!rootId)
        return;
    startComparisonForRoot(rootId, typeForRoot(rootId));
}


void MainWindow::discoveryValidateSelected()
{
    if (!discovery_ || discovery_->isRunning() || !ui->discoveryResultsTW->selectionModel())
        return;

    QVector<std::uint64_t> roots;
    const QModelIndexList rows = ui->discoveryResultsTW->selectionModel()->selectedRows(2);
    for (int i = 0; i < rows.size(); ++i)
    {
        bool ok = false;
        const qulonglong root = rows.at(i).data().toString().toULongLong(&ok);
        if (ok && root != 0 && !containsRoot(roots, static_cast<std::uint64_t>(root)))
            roots.append(static_cast<std::uint64_t>(root));
    }
    if (roots.isEmpty())
    {
        ui->discoveryStatusLB->setText(QStringLiteral("Select one or more valid screening rows to validate."));
        return;
    }
    startCausalValidation(roots);
}

void MainWindow::discoveryValidateTop()
{
    if (!discovery_ || discovery_->isRunning())
        return;

    const DrosofilaLive::CausalDiscoveryResult result = discovery_->result();
    QVector<std::uint64_t> roots;
    const int requested = ui->discoveryValidateTopSB->value();
    for (int i = 0; i < result.entries.size() && roots.size() < requested; ++i)
    {
        const DrosofilaLive::CausalDiscoveryEntry& entry = result.entries.at(i);
        if (entry.valid && entry.causalRank > 0)
            roots.append(entry.candidate.rootId);
    }
    if (roots.isEmpty())
    {
        ui->discoveryStatusLB->setText(QStringLiteral("No valid causal-screening hit is available for validation."));
        return;
    }
    startCausalValidation(roots);
}

void MainWindow::startCausalValidation(const QVector<std::uint64_t>& rootIds)
{
    if (!discovery_ || discovery_->isRunning() || rootIds.isEmpty() || !experiment_ ||
        !experiment_->isConnectomeLoaded() || experiment_->isRunning() || circuitExplorerBusy_ ||
        (statistics_ && statistics_->isRunning()) || (script_ && script_->isBusy()))
        return;

    const QString stimulusPath = defaultStimulusPath();
    if (!QFileInfo::exists(stimulusPath))
    {
        setIdleActivityMessage(QStringLiteral("Causal Validation ERROR: local Sugar stimulus file not found."));
        appendGlobalLogSection(QStringLiteral("Discovery validation"), QStringLiteral("Expected: %1").arg(stimulusPath));
        return;
    }

    DrosofilaLive::ExperimentParameters parameters = experimentParametersFromUi();
    parameters.stimulusPath = stimulusPath;
    parameters.ablatedRootIds.clear();
    const std::uint64_t targetId = 720575940660219265ULL;
    if (!containsRoot(parameters.watchedRootIds, targetId))
        parameters.watchedRootIds.append(targetId);

    const int replicates = ui->discoveryReplicatesSB->value();
    stopPlayback(true);
    ui->discoveryEpistasisTW->setRowCount(0);
    causalDiscoveryGuard_ = true;
    setExperimentControlsEnabled(false);
    ui->discoveryStopPB->setEnabled(true);
    ui->discoveryStatusLB->setText(QStringLiteral("Validating %1 candidate(s), %2 replicate(s) each...")
                                   .arg(rootIds.size()).arg(replicates));
    beginActivity(QStringLiteral("Causal Validation"), QStringLiteral("Independent-seed single-ablation validation"));
    appendGlobalLogSection(QStringLiteral("Discovery validation"),
        QStringLiteral("Target: CB0701/MN9 [%1]\nCandidates: %2\nReplicates/candidate: %3\nProtocol: Sugar %4 Hz | %5 ms | %6 trial(s)/replicate | seeds %7 .. %8\nStatistics: mean delta, STD, SEM, two-sided Student-t CI95, sign consistency")
            .arg(static_cast<qulonglong>(targetId))
            .arg(rootListText(rootIds))
            .arg(replicates)
            .arg(parameters.sugarRateHz, 0, 'f', 1)
            .arg(parameters.durationMs, 0, 'f', 1)
            .arg(parameters.trials)
            .arg(parameters.randomSeed)
            .arg(static_cast<quint64>(parameters.randomSeed) + static_cast<quint64>(replicates - 1)));

    if (!discovery_->runValidation(parameters, rootIds, replicates))
    {
        causalDiscoveryGuard_ = false;
        setExperimentControlsEnabled(true);
        ui->discoveryStopPB->setEnabled(false);
        finishActivity(QStringLiteral("Unable to start causal statistical validation."), false);
    }
    else
    {
        DrosofilaLive::CausalDiscoveryProvenance provenance = discovery_->result().provenance;
        provenance.valid = true;
        if (provenance.origin.isEmpty()) provenance.origin = QStringLiteral("GUI");
        provenance.validationTopN = rootIds.size();
        provenance.validationParametersValid = true;
        provenance.validationParameters = parameters;
        provenance.updatedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        discovery_->setProvenance(provenance);
        archiveSourcePath_.clear();
        archiveCreatedUtc_.clear();
        updateArchiveUi();
    }
}

void MainWindow::discoveryGateChanged()
{
    if (!ui) return;
    ui->discoveryMinSignDSB->setEnabled(ui->discoveryRequireSignCB->isChecked() &&
                                        !(discovery_ && discovery_->isRunning()) &&
                                        !(script_ && script_->isBusy()));
    setExperimentControlsEnabled(!(experiment_ && experiment_->isRunning()) &&
                                 !(statistics_ && statistics_->isRunning()) &&
                                 !(script_ && script_->isBusy()) && !circuitExplorerBusy_);
    if (discovery_ && !discovery_->result().entries.isEmpty())
    {
        DrosofilaLive::CausalDiscoveryProvenance provenance = discovery_->result().provenance;
        provenance.valid = true;
        if (provenance.origin.isEmpty()) provenance.origin = QStringLiteral("GUI");
        const bool requireCi = ui->discoveryRequireCiCB->isChecked();
        const bool requireSign = ui->discoveryRequireSignCB->isChecked();
        provenance.epistasisGateMode = requireCi && requireSign ? QStringLiteral("BOTH")
            : (requireCi ? QStringLiteral("CI95") : (requireSign ? QStringLiteral("SIGN") : QStringLiteral("NONE")));
        provenance.epistasisMinimumSignConsistency = ui->discoveryMinSignDSB->value() / 100.0;
        provenance.updatedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        discovery_->setProvenance(provenance);
        if (script_) script_->restoreScientificState(lastCircuitExplore_, discovery_->result());
        invalidateCausalSurprise(QStringLiteral("Discovery gate changed; rerun Causal Surprise analysis."));
        archiveSourcePath_.clear();
        archiveCreatedUtc_.clear();
    }
    ui->discoveryStatusLB->setText(QStringLiteral("Epistasis gate: %1").arg(discoveryGateText()));
}

QString MainWindow::causalSurpriseReportFilePath() const
{
    QString base = QStringLiteral("causal_surprise");
    if (!archiveSourcePath_.isEmpty())
    {
        const QString candidate = QFileInfo(archiveSourcePath_).completeBaseName();
        if (!candidate.isEmpty()) base = candidate + QStringLiteral("_causal_surprise");
    }
    return QDir(DrosofilaLiveRuntime::reportsDir()).filePath(
        QStringLiteral("%1_%2.html").arg(base, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
}

void MainWindow::invalidateCausalSurprise(const QString& reason)
{
    lastCausalSurprise_ = DrosofilaLive::CausalStructureAnalysisResult();
    causalSurpriseReportPath_.clear();
    if (!ui) return;
    ui->causalSurpriseTW->setRowCount(0);
    if (causalSurprisePlot_) causalSurprisePlot_->clear();
    ui->causalSurpriseReportPB->setEnabled(false);
    if (!reason.isEmpty()) ui->causalSurpriseStatusLB->setText(reason);
}

void MainWindow::populateCausalSurpriseResult(const DrosofilaLive::CausalStructureAnalysisResult& result)
{
    if (!ui) return;
    const QSignalBlocker blocker(ui->causalSurpriseTW);
    ui->causalSurpriseTW->setRowCount(result.valid ? result.entries.size() : 0);
    if (!result.valid)
    {
        if (causalSurprisePlot_) causalSurprisePlot_->clear();
        ui->causalSurpriseStatusLB->setText(result.error.isEmpty() ? QStringLiteral("No Causal Surprise result.") : result.error);
        return;
    }

    for (int r = 0; r < result.entries.size(); ++r)
    {
        const DrosofilaLive::CausalSurpriseEntry& e = result.entries.at(r);
        const QString ci = QStringLiteral("[%1, %2]").arg(e.ci95LowHz, 0, 'f', 3).arg(e.ci95HighHz, 0, 'f', 3);
        const QString values[15] = {
            e.surpriseRank > 0 ? QString::number(e.surpriseRank) : QStringLiteral("-"),
            e.type.isEmpty() ? QStringLiteral("unannotated") : e.type,
            QString::number(static_cast<qulonglong>(e.rootId)),
            QString::number(e.graphRank),
            QString::number(e.depth),
            QString::number(e.graphScore, 'f', 5),
            QString::number(static_cast<qulonglong>(e.pathCount)),
            QString::number(static_cast<qulonglong>(e.synapseStrength)),
            QString::number(e.validationDeltaHz, 'f', 3),
            ci,
            QString::number(100.0 * e.signConsistency, 'f', 1),
            QString::number(100.0 * e.structuralPercentile, 'f', 1),
            QString::number(100.0 * e.causalPercentile, 'f', 1),
            QString::number(e.surpriseScore, 'f', 3),
            e.gatePassed ? e.quadrant : QStringLiteral("NOT GATE-PASSED")
        };
        QColor rowColor;
        if (!e.gatePassed) rowColor = QColor(245, 246, 248);
        else if (e.quadrant == QStringLiteral("CAUSAL SURPRISE")) rowColor = QColor(255, 247, 237);
        else if (e.quadrant == QStringLiteral("EXPECTED STRONG")) rowColor = QColor(239, 246, 255);
        else if (e.quadrant == QStringLiteral("STRUCTURAL OVERESTIMATE")) rowColor = QColor(250, 245, 255);
        else rowColor = QColor(248, 250, 252);
        for (int c = 0; c < 15; ++c)
        {
            QTableWidgetItem* item = new QTableWidgetItem(values[c]);
            if (c != 1 && c != 14) item->setTextAlignment(Qt::AlignCenter);
            item->setBackground(QBrush(rowColor));
            if (c == 13) item->setToolTip(QStringLiteral("Causal percentile minus structural percentile. Positive = causal importance exceeds structural expectation."));
            if (c == 14 && !e.gatePassed) item->setToolTip(QStringLiteral("Validation exists, but this row does not pass the archived Discovery statistical gate and receives no Surprise rank."));
            ui->causalSurpriseTW->setItem(r, c, item);
        }
    }
    ui->causalSurpriseTW->resizeColumnsToContents();
    if (causalSurprisePlot_) causalSurprisePlot_->setResult(result);

    QString correlation = result.correlationsAvailable
        ? QStringLiteral("Pearson %1 | Spearman %2").arg(result.pearson, 0, 'f', 3).arg(result.spearman, 0, 'f', 3)
        : QStringLiteral("correlation unavailable (<2 gate-passed rows)");
    QString topText;
    for (int i = 0; i < result.entries.size(); ++i)
    {
        const DrosofilaLive::CausalSurpriseEntry& e = result.entries.at(i);
        if (e.surpriseRank == 1)
        {
            topText = QStringLiteral(" | top surprise: %1 [%2], score %3")
                .arg(e.type.isEmpty() ? QStringLiteral("unannotated") : e.type)
                .arg(static_cast<qulonglong>(e.rootId)).arg(e.surpriseScore, 0, 'f', 3);
            break;
        }
    }
    ui->causalSurpriseStatusLB->setText(QStringLiteral("%1 validated row(s), %2 gate-passed | %3%4")
        .arg(result.validationRows).arg(result.gatePassedRows).arg(correlation, topText));
    ui->causalSurpriseReportPB->setEnabled(!causalSurpriseReportPath_.isEmpty() && QFileInfo::exists(causalSurpriseReportPath_));
}

void MainWindow::analyzeCausalSurprise()
{
    if (!discovery_) return;
    const DrosofilaLive::CausalDiscoveryResult d = discovery_->result();
    DrosofilaLive::CausalStructureAnalysisResult result = DrosofilaLive::CausalStructureAnalyzer::analyze(lastCircuitExplore_, d);
    if (!result.valid)
    {
        lastCausalSurprise_ = result;
        populateCausalSurpriseResult(result);
        setIdleActivityMessage(QStringLiteral("Causal Surprise analysis unavailable: %1").arg(result.error));
        return;
    }

    lastCausalSurprise_ = result;
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    causalSurpriseReportPath_ = causalSurpriseReportFilePath();
    QString error;
    if (!DrosofilaLive::CausalStructureAnalyzer::writeHtmlReport(causalSurpriseReportPath_, result, &error))
    {
        appendGlobalLog(QStringLiteral("[CAUSAL SURPRISE] report error: %1").arg(error));
        causalSurpriseReportPath_.clear();
    }
    populateCausalSurpriseResult(result);
    selectCausalSurprisePlot();
    appendGlobalLogSection(QStringLiteral("Causal Surprise"), ui->causalSurpriseStatusLB->text() +
        (causalSurpriseReportPath_.isEmpty() ? QString() : QStringLiteral("\nReport: %1").arg(causalSurpriseReportPath_)));
    setIdleActivityMessage(QStringLiteral("Causal Surprise analysis complete."));
    updateArchiveUi();
}

void MainWindow::openCausalSurpriseReport()
{
    if (causalSurpriseReportPath_.isEmpty() || !QFileInfo::exists(causalSurpriseReportPath_)) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(causalSurpriseReportPath_));
}

void MainWindow::causalSurpriseSelectionChanged()
{
    if (!ui || !ui->causalSurpriseTW || !ui->causalSurpriseTW->selectionModel()) return;
    const QModelIndexList rows = ui->causalSurpriseTW->selectionModel()->selectedRows(2);
    if (rows.isEmpty()) return;
    bool ok = false;
    const std::uint64_t rootId = static_cast<std::uint64_t>(rows.first().data().toString().toULongLong(&ok));
    if (!ok || !rootId) return;
    if (causalSurprisePlot_) causalSurprisePlot_->selectRoot(rootId);
    if (circuitGraph_) circuitGraph_->selectRoot(rootId);
    for (std::size_t i = 0; i < neuronIds_.size(); ++i)
    {
        if (neuronIds_[i] == rootId)
        {
            ui->neuronCB->setCurrentIndex(static_cast<int>(i));
            break;
        }
    }
}

void MainWindow::causalSurprisePlotRootSelected(qulonglong value)
{
    if (!ui || !lastCausalSurprise_.valid) return;
    const std::uint64_t rootId = static_cast<std::uint64_t>(value);
    for (int r = 0; r < lastCausalSurprise_.entries.size(); ++r)
    {
        if (lastCausalSurprise_.entries.at(r).rootId == rootId)
        {
            ui->causalSurpriseTW->selectRow(r);
            causalSurpriseSelectionChanged();
            return;
        }
    }
}

void MainWindow::scriptCausalSurpriseReady(const DrosofilaLive::CausalStructureAnalysisResult& result)
{
    lastCausalSurprise_ = result;
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    causalSurpriseReportPath_ = causalSurpriseReportFilePath();
    QString error;
    if (!DrosofilaLive::CausalStructureAnalyzer::writeHtmlReport(causalSurpriseReportPath_, result, &error))
    {
        appendGlobalLog(QStringLiteral("[CAUSAL SURPRISE] script report error: %1").arg(error));
        causalSurpriseReportPath_.clear();
    }
    populateCausalSurpriseResult(result);
    selectCausalSurprisePlot();
    updateArchiveUi();
}

void MainWindow::discoveryEpistasisTop()
{
    if (!discovery_ || discovery_->isRunning())
        return;

    const DrosofilaLive::CausalDiscoveryResult result = discovery_->result();
    QVector<int> eligible;
    for (int i = 0; i < result.entries.size(); ++i)
    {
        const DrosofilaLive::CausalDiscoveryEntry& entry = result.entries.at(i);
        if (entry.validationValid && containsRoot(result.validationRootIds, entry.candidate.rootId) && discoveryValidationPassesGate(entry))
            eligible.append(i);
    }

    std::stable_sort(eligible.begin(), eligible.end(),
        [&result](int a, int b)
        {
            const DrosofilaLive::CausalDiscoveryEntry& ea = result.entries.at(a);
            const DrosofilaLive::CausalDiscoveryEntry& eb = result.entries.at(b);
            const double aa = std::fabs(ea.validationDelta.mean);
            const double bb = std::fabs(eb.validationDelta.mean);
            if (std::fabs(aa - bb) > 1e-12) return aa > bb;
            return ea.causalRank < eb.causalRank;
        });

    const int requested = std::min(ui->discoveryEpistasisTopSB->value(), eligible.size());
    if (requested < 2)
    {
        ui->discoveryStatusLB->setText(QStringLiteral("At least two validation hits must pass the epistasis gate: %1").arg(discoveryGateText()));
        return;
    }

    QVector<std::uint64_t> roots;
    for (int i = 0; i < requested; ++i)
        roots.append(result.entries.at(eligible.at(i)).candidate.rootId);
    startCausalEpistasis(roots);
}

void MainWindow::startCausalEpistasis(const QVector<std::uint64_t>& rootIds)
{
    if (!discovery_ || discovery_->isRunning() || rootIds.size() < 2 || !experiment_ ||
        !experiment_->isConnectomeLoaded() || experiment_->isRunning() || circuitExplorerBusy_ ||
        (statistics_ && statistics_->isRunning()) || (script_ && script_->isBusy()))
        return;

    const QString stimulusPath = defaultStimulusPath();
    if (!QFileInfo::exists(stimulusPath))
    {
        setIdleActivityMessage(QStringLiteral("Discovery Epistasis ERROR: local Sugar stimulus file not found."));
        appendGlobalLogSection(QStringLiteral("Discovery epistasis"), QStringLiteral("Expected: %1").arg(stimulusPath));
        return;
    }

    DrosofilaLive::ExperimentParameters parameters = experimentParametersFromUi();
    parameters.stimulusPath = stimulusPath;
    parameters.ablatedRootIds.clear();
    const std::uint64_t targetId = 720575940660219265ULL;
    if (!containsRoot(parameters.watchedRootIds, targetId))
        parameters.watchedRootIds.append(targetId);

    const int replicates = ui->discoveryEpistasisReplicatesSB->value();
    const int pairCount = rootIds.size() * (rootIds.size() - 1) / 2;
    stopPlayback(true);
    causalDiscoveryGuard_ = true;
    setExperimentControlsEnabled(false);
    ui->discoveryStopPB->setEnabled(true);
    ui->discoveryEpistasisTW->setRowCount(0);
    ui->discoveryStatusLB->setText(QStringLiteral("Testing %1 validated hit(s): %2 pair(s), %3 replicate(s) each...")
                                   .arg(rootIds.size()).arg(pairCount).arg(replicates));
    beginActivity(QStringLiteral("Discovery Epistasis"), QStringLiteral("Validated pairwise interaction screening"));
    appendGlobalLogSection(QStringLiteral("Discovery epistasis"),
        QStringLiteral("Target: CB0701/MN9 [%1]\nGate: %2\nValidated hits passing gate: %3\nUnique pairs: %4\nReplicates/pair: %5\nProtocol: WT / A OFF / B OFF / A+B OFF | Sugar %6 Hz | %7 ms | %8 trial(s)/replicate | seeds %9 .. %10\nPair ranking: descending |mean interaction Hz|\nInteraction = observed(A+B OFF) - [A OFF + B OFF - WT]")
            .arg(static_cast<qulonglong>(targetId))
            .arg(discoveryGateText())
            .arg(rootListText(rootIds))
            .arg(pairCount)
            .arg(replicates)
            .arg(parameters.sugarRateHz, 0, 'f', 1)
            .arg(parameters.durationMs, 0, 'f', 1)
            .arg(parameters.trials)
            .arg(parameters.randomSeed)
            .arg(static_cast<quint64>(parameters.randomSeed) + static_cast<quint64>(replicates - 1)));

    if (!discovery_->runEpistasis(parameters, rootIds, replicates))
    {
        causalDiscoveryGuard_ = false;
        setExperimentControlsEnabled(true);
        ui->discoveryStopPB->setEnabled(false);
        finishActivity(QStringLiteral("Unable to start discovery pairwise epistasis."), false);
    }
    else
    {
        DrosofilaLive::CausalDiscoveryProvenance provenance = discovery_->result().provenance;
        provenance.valid = true;
        if (provenance.origin.isEmpty()) provenance.origin = QStringLiteral("GUI");
        provenance.epistasisTopN = rootIds.size();
        provenance.epistasisParametersValid = true;
        provenance.epistasisParameters = parameters;
        const bool requireCi = ui->discoveryRequireCiCB->isChecked();
        const bool requireSign = ui->discoveryRequireSignCB->isChecked();
        provenance.epistasisGateMode = requireCi && requireSign ? QStringLiteral("BOTH")
            : (requireCi ? QStringLiteral("CI95") : (requireSign ? QStringLiteral("SIGN") : QStringLiteral("NONE")));
        provenance.epistasisMinimumSignConsistency = ui->discoveryMinSignDSB->value() / 100.0;
        provenance.updatedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        discovery_->setProvenance(provenance);
        archiveSourcePath_.clear();
        archiveCreatedUtc_.clear();
        updateArchiveUi();
    }
}

void MainWindow::startExperiment()
{
    selectTimelinePlot();
    if (!experiment_ || experiment_->isRunning() || !experiment_->isConnectomeLoaded())
        return;

    const QString stimulusPath = defaultStimulusPath();
    if (!QFileInfo::exists(stimulusPath))
    {
        setIdleActivityMessage(QStringLiteral("Experiment ERROR: local Sugar stimulus file not found."));
        appendGlobalLogSection(QStringLiteral("Experiment"), QStringLiteral("Expected: %1").arg(stimulusPath));
        ui->statusbar->showMessage(QStringLiteral("Experiment stimulus file missing"), 6000);
        return;
    }

    const QVector<std::uint64_t> watched = watchedRootIdsFromUi();
    if (watched.isEmpty())
    {
        setIdleActivityMessage(QStringLiteral("Experiment ERROR: select at least one watched neuron."));
        return;
    }

    stopPlayback(true);
    lastExperiment_ = DrosofilaLive::ExperimentResult();
    lastComparison_ = DrosofilaLive::ComparisonResult();
    if (timeline_) timeline_->clear();
    ui->playComparisonPB->setEnabled(false);
    ui->timelineInfoLB->setText(QStringLiteral("Timeline: experiment running..."));
    playbackFrame_ = 0;

    DrosofilaLive::ExperimentParameters parameters;
    parameters.durationMs = ui->durationSB->value();
    parameters.sugarRateHz = ui->sugarRateSB->value();
    parameters.trials = ui->trialsSB->value();
    parameters.randomSeed = static_cast<quint32>(ui->seedSB->value());
    parameters.temporalBinMs = 10.0;
    parameters.stimulusPath = stimulusPath;
    parameters.watchedRootIds = watched;
    parameters.ablatedRootIds = ablatedRootIdsFromUi();

    beginActivity(QStringLiteral("Experiment"), QStringLiteral("Validated LIF experiment"));
    appendGlobalLogSection(QStringLiteral("Experiment"), 
        QStringLiteral("Sugar %1 Hz | %2 ms | %3 trial(s) | seed %4\nWatch: %5\nAblate: %6")
            .arg(parameters.sugarRateHz, 0, 'f', 1)
            .arg(parameters.durationMs, 0, 'f', 1)
            .arg(parameters.trials)
            .arg(parameters.randomSeed)
            .arg(rootListText(parameters.watchedRootIds))
            .arg(rootListText(parameters.ablatedRootIds)));
    ui->statusbar->showMessage(QStringLiteral("LIF experiment running..."));
    setExperimentControlsEnabled(false);

    if (!experiment_->run(parameters))
    {
        setExperimentControlsEnabled(true);
        finishActivity(QStringLiteral("Unable to start experiment."), false);
        return;
    }

    DrosofilaLiveDiagnostics::log(QString("Experiment start: duration=%1 rate=%2 trials=%3 seed=%4 watch=[%5] ablate=[%6]")
        .arg(parameters.durationMs).arg(parameters.sugarRateHz).arg(parameters.trials)
        .arg(parameters.randomSeed).arg(rootListText(parameters.watchedRootIds))
        .arg(rootListText(parameters.ablatedRootIds)));
}

void MainWindow::experimentProgress(int percent, const QString& phase)
{
    updateScientificActivity(QStringLiteral("Experiment"), percent, phase);
}

QString MainWindow::experimentSummaryText(const DrosofilaLive::ExperimentResult& result) const
{
    if (!result.valid)
        return result.error.isEmpty() ? QStringLiteral("No valid experiment result.") : result.error;

    QStringList lines;
    lines << QStringLiteral("REAL LIF EXPERIMENT")
          << QStringLiteral("Sugar: %1 @ %2 Hz | duration %3 ms | trials %4/%5 | seed %6")
                .arg(result.stimulusName)
                .arg(result.sugarRateHz, 0, 'f', 1)
                .arg(result.durationMs, 0, 'f', 1)
                .arg(result.trialsCompleted)
                .arg(result.trialsRequested)
                .arg(result.randomSeed)
          << QStringLiteral("Watch: %1").arg(rootListText(result.watchedRootIds))
          << QStringLiteral("Ablate: %1").arg(rootListText(result.ablatedRootIds))
          << QStringLiteral("Wall time: %1 s | total network spikes: %2 | Poisson events: %3")
                .arg(result.wallTimeMs / 1000.0, 0, 'f', 2)
                .arg(static_cast<qulonglong>(result.totalSpikes))
                .arg(static_cast<qulonglong>(result.poissonEvents))
          << QStringLiteral("TARGET/MN9: %1 Hz (SD %2), latency %3 ms")
                .arg(result.targetMeanHz, 0, 'f', 2)
                .arg(result.targetStdHz, 0, 'f', 2)
                .arg(result.targetMeanLatencyMs, 0, 'f', 2)
          << QStringLiteral("")
          << QStringLiteral("Watched neurons:");

    for (int i = 0; i < result.neurons.size(); ++i)
    {
        const DrosofilaLive::ExperimentNeuronSummary& n = result.neurons.at(i);
        const QString ablated = containsRoot(result.ablatedRootIds, n.rootId) ? QStringLiteral(" [ABLATED]") : QString();
        lines << QStringLiteral("%1%2  %3 Hz | first %4 ms | P(active) %5")
                    .arg(n.type, -8)
                    .arg(ablated)
                    .arg(n.meanHz, 7, 'f', 2)
                    .arg(n.meanFirstSpikeMs, 7, 'f', 2)
                    .arg(n.activationProbability, 0, 'f', 2);
    }
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::experimentFinished()
{
    selectTimelinePlot();
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    lastComparison_ = DrosofilaLive::ComparisonResult();
    lastExperiment_ = experiment_->result();
    if (timeline_) timeline_->setResult(lastExperiment_);
    updateTimelineSelection();
    setExperimentControlsEnabled(true);

    if (!lastExperiment_.valid)
    {
        if (activityName_ == QStringLiteral("cvBasic script"))
            updateActivityDetail(0, QStringLiteral("DRO.RUN failed: %1").arg(lastExperiment_.error));
        else
            finishActivity(QStringLiteral("Experiment failed: %1").arg(lastExperiment_.error), false);
        appendGlobalLogSection(QStringLiteral("Experiment"), experimentSummaryText(lastExperiment_));
        ui->statusbar->showMessage(QStringLiteral("LIF experiment failed: %1").arg(lastExperiment_.error), 8000);
        DrosofilaLiveDiagnostics::log(QString("Experiment ERROR: %1").arg(lastExperiment_.error));
        return;
    }

    if (activityName_ == QStringLiteral("cvBasic script"))
        updateActivityDetail(100, QStringLiteral("DRO.RUN complete"));
    else
        finishActivity(QStringLiteral("Experiment complete. Numerical result is ready."), true);
    appendGlobalLogSection(QStringLiteral("Experiment"), experimentSummaryText(lastExperiment_));
    ui->playSimulationPB->setEnabled(circuitLoaded_ && !lastExperiment_.frames.isEmpty());
    resetExperimentVisualState();
    applyAblationVisualState(lastExperiment_.ablatedRootIds);
    ui->statusbar->showMessage(QStringLiteral("LIF complete: TARGET/MN9 %1 Hz - Play result to visualize temporal activity")
                               .arg(lastExperiment_.targetMeanHz, 0, 'f', 2), 8000);
    updateArchiveUi();
    DrosofilaLiveDiagnostics::log(QString("Experiment complete: target=%1 Hz wall=%2 ms frames=%3 ablations=%4")
        .arg(lastExperiment_.targetMeanHz).arg(lastExperiment_.wallTimeMs).arg(lastExperiment_.frames.size())
        .arg(lastExperiment_.ablatedRootIds.size()));
}

void MainWindow::playExperiment()
{
    selectTimelinePlot();
    if (playbackTimer_->isActive())
    {
        stopPlayback(false);
        return;
    }

    if (!rendererReady() || !circuitLoaded_ || !lastExperiment_.valid || lastExperiment_.frames.isEmpty())
        return;

    playbackFrame_ = 0;
    playbackMode_ = PlaybackSingle;
    if (timeline_) timeline_->setCursorTime(0.0);
    resetExperimentVisualState();
    applyAblationVisualState(lastExperiment_.ablatedRootIds);
    ui->playSimulationPB->setText(QStringLiteral("Stop playback"));
    ui->statusbar->showMessage(QStringLiteral("Playback of real LIF temporal bins..."));
    playbackTimer_->start();
}

void MainWindow::startComparison()
{
    const std::vector<DrosofilaLive::CausalNeuronSpec>& specs = DrosofilaLive::validatedCausalCircuit();
    const int perturbIndex = ui->comparisonNeuronCB->currentIndex();
    if (perturbIndex < 0 || perturbIndex >= static_cast<int>(specs.size()))
        return;
    startComparisonForRoot(specs[static_cast<std::size_t>(perturbIndex)].rootId,
                           QString::fromLatin1(specs[static_cast<std::size_t>(perturbIndex)].type));
}

void MainWindow::startComparisonForRoot(std::uint64_t rootId, const QString& typeLabel)
{
    selectTimelinePlot();
    if (!experiment_ || experiment_->isRunning() || !experiment_->isConnectomeLoaded() || rootId == 0)
        return;

    const QString stimulusPath = defaultStimulusPath();
    if (!QFileInfo::exists(stimulusPath))
    {
        setIdleActivityMessage(QStringLiteral("Comparison ERROR: local Sugar stimulus file not found."));
        appendGlobalLogSection(QStringLiteral("Compare"), QStringLiteral("Expected: %1").arg(stimulusPath));
        return;
    }

    QVector<std::uint64_t> watched = watchedRootIdsFromUi();
    const std::uint64_t targetId = 720575940660219265ULL;
    if (!containsRoot(watched, targetId)) watched.append(targetId);
    if (!containsRoot(watched, rootId)) watched.append(rootId);

    QVector<std::uint64_t> perturbed;
    perturbed.append(rootId);

    DrosofilaLive::ExperimentParameters parameters = experimentParametersFromUi();
    parameters.watchedRootIds = watched;
    parameters.ablatedRootIds.clear();

    stopPlayback(true);
    lastComparison_ = DrosofilaLive::ComparisonResult();
    lastExperiment_ = DrosofilaLive::ExperimentResult();
    if (timeline_) timeline_->clear();
    playbackFrame_ = 0;

    beginActivity(QStringLiteral("Comparison"), QStringLiteral("Reference / Perturbed"));
    appendGlobalLogSection(QStringLiteral("Compare"),
        QStringLiteral("Sugar %1 Hz | %2 ms | %3 trial(s) | seed %4\nReference: no ablations\nPerturbed: %5 [%6] OFF\nWatch: %7")
            .arg(parameters.sugarRateHz, 0, 'f', 1)
            .arg(parameters.durationMs, 0, 'f', 1)
            .arg(parameters.trials)
            .arg(parameters.randomSeed)
            .arg(typeLabel)
            .arg(static_cast<qulonglong>(rootId))
            .arg(rootListText(parameters.watchedRootIds)));
    ui->playSimulationPB->setEnabled(false);
    setExperimentControlsEnabled(false);

    if (!experiment_->runComparison(parameters, perturbed))
    {
        setExperimentControlsEnabled(true);
        finishActivity(QStringLiteral("Unable to start comparison."), false);
    }
}

void MainWindow::comparisonProgress(int percent, const QString& phase)
{
    if (causalDiscoveryGuard_) return;
    updateScientificActivity(QStringLiteral("Comparison"), percent, phase);
}

void MainWindow::epistasisSelectionChanged()
{
    if (!ui || !experiment_)
        return;

    const bool same = ui->epistasisNeuronACB->currentIndex() == ui->epistasisNeuronBCB->currentIndex();
    const bool ready = experiment_->isConnectomeLoaded() &&
                       experiment_->validateCausalCircuit().complete() &&
                       !experiment_->isRunning() &&
                       !(script_ && script_->isBusy()) && !same;
    ui->runEpistasisPB->setEnabled(ready);

}

void MainWindow::startEpistasis()
{
    selectTimelinePlot();
    if (!experiment_ || experiment_->isRunning() || !experiment_->isConnectomeLoaded())
        return;

    const int aIndex = ui->epistasisNeuronACB->currentIndex();
    const int bIndex = ui->epistasisNeuronBCB->currentIndex();
    const std::vector<DrosofilaLive::CausalNeuronSpec>& specs = DrosofilaLive::validatedCausalCircuit();
    if (aIndex < 0 || bIndex < 0 ||
        aIndex >= static_cast<int>(specs.size()) || bIndex >= static_cast<int>(specs.size()) ||
        aIndex == bIndex)
    {
        setIdleActivityMessage(QStringLiteral("Epistasis ERROR: select two different causal neurons."));
        return;
    }

    const QString stimulusPath = defaultStimulusPath();
    if (!QFileInfo::exists(stimulusPath))
    {
        setIdleActivityMessage(QStringLiteral("Epistasis ERROR: local Sugar stimulus file not found."));
        appendGlobalLogSection(QStringLiteral("Epistasis"), QStringLiteral("Expected: %1").arg(stimulusPath));
        return;
    }

    DrosofilaLive::ExperimentParameters parameters = experimentParametersFromUi();
    parameters.ablatedRootIds.clear();
    const std::uint64_t targetRootId = 720575940660219265ULL;
    if (!containsRoot(parameters.watchedRootIds, targetRootId))
        parameters.watchedRootIds.append(targetRootId);

    stopPlayback(true);
    lastEpistasis_ = DrosofilaLive::EpistasisResult();
    beginActivity(QStringLiteral("Epistasis"), QStringLiteral("WT / A OFF / B OFF / A+B OFF"));
    appendGlobalLogSection(QStringLiteral("Epistasis"), QStringLiteral("Epistasis pair running..."));
    setExperimentControlsEnabled(false);

    if (!experiment_->runEpistasis(parameters, specs[static_cast<std::size_t>(aIndex)].rootId,
                                   specs[static_cast<std::size_t>(bIndex)].rootId))
    {
        setExperimentControlsEnabled(true);
        finishActivity(QStringLiteral("Unable to start epistasis experiment."), false);
    }
}

void MainWindow::epistasisProgress(int percent, const QString& phase)
{
    if (causalDiscoveryGuard_) return;
    // A statistics run wraps this same epistasis progress with replicate-level Overall progress.
    if (statistics_ && statistics_->isRunning())
        return;
    updateScientificActivity(QStringLiteral("Epistasis"), percent, phase);
}

QString MainWindow::epistasisSummaryText(const DrosofilaLive::EpistasisResult& result) const
{
    if (!result.valid)
        return result.error.isEmpty() ? QStringLiteral("No valid epistasis result.") : result.error;

    QStringList lines;
    lines << QStringLiteral("PAIRWISE EPISTASIS - TARGET/MN9")
          << QStringLiteral("A: %1 | B: %2").arg(result.typeA, result.typeB)
          << QStringLiteral("Common seed: %1 | trials %2 | duration %3 ms | Sugar %4 @ %5 Hz")
                .arg(result.wildType.randomSeed)
                .arg(result.wildType.trialsCompleted)
                .arg(result.wildType.durationMs, 0, 'f', 1)
                .arg(result.wildType.stimulusName)
                .arg(result.wildType.sugarRateHz, 0, 'f', 1)
          << QStringLiteral("")
          << QStringLiteral("WT        : %1 Hz | latency %2 ms")
                .arg(result.wildType.targetMeanHz, 0, 'f', 2)
                .arg(result.wildType.targetMeanLatencyMs, 0, 'f', 2)
          << QStringLiteral("A OFF     : %1 Hz | delta %2 Hz | latency %3 ms")
                .arg(result.aOff.targetMeanHz, 0, 'f', 2)
                .arg(result.aDeltaHz, 0, 'f', 2)
                .arg(result.aOff.targetMeanLatencyMs, 0, 'f', 2)
          << QStringLiteral("B OFF     : %1 Hz | delta %2 Hz | latency %3 ms")
                .arg(result.bOff.targetMeanHz, 0, 'f', 2)
                .arg(result.bDeltaHz, 0, 'f', 2)
                .arg(result.bOff.targetMeanLatencyMs, 0, 'f', 2)
          << QStringLiteral("A+B OFF   : %1 Hz | delta %2 Hz | latency %3 ms")
                .arg(result.doubleOff.targetMeanHz, 0, 'f', 2)
                .arg(result.doubleDeltaHz, 0, 'f', 2)
                .arg(result.doubleOff.targetMeanLatencyMs, 0, 'f', 2)
          << QStringLiteral("")
          << QStringLiteral("Expected additive A+B : %1 Hz").arg(result.expectedAdditiveHz, 0, 'f', 2)
          << QStringLiteral("Observed A+B          : %1 Hz").arg(result.doubleOff.targetMeanHz, 0, 'f', 2)
          << QStringLiteral("Interaction           : %1 Hz").arg(result.interactionHz, 0, 'f', 2)
          << QStringLiteral("")
          << QStringLiteral("Interaction = observed(A+B OFF) - [A OFF + B OFF - WT]")
          << QStringLiteral("This is an effect-size descriptor, not a significance test.");
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::epistasisFinished()
{
    if (causalDiscoveryGuard_) return;
    if (statistics_ && statistics_->isRunning())
        return;

    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    lastEpistasis_ = experiment_->epistasisResult();
    setExperimentControlsEnabled(true);

    if (!lastEpistasis_.valid)
    {
        if (activityName_ == QStringLiteral("cvBasic script"))
            updateActivityDetail(0, QStringLiteral("DRO.EPISTASIS failed: %1").arg(lastEpistasis_.error));
        else
            finishActivity(QStringLiteral("Epistasis failed: %1").arg(lastEpistasis_.error), false);
        appendGlobalLogSection(QStringLiteral("Epistasis ERROR"), epistasisSummaryText(lastEpistasis_));
        ui->statusbar->showMessage(QStringLiteral("Epistasis failed: %1").arg(lastEpistasis_.error), 8000);
        return;
    }

    storeEpistasisResult(lastEpistasis_);
    if (activityName_ == QStringLiteral("cvBasic script"))
        updateActivityDetail(100, QStringLiteral("DRO.EPISTASIS complete"));
    else
        finishActivity(QStringLiteral("Epistasis pair complete."), true);
    appendGlobalLogSection(QStringLiteral("Epistasis"), epistasisSummaryText(lastEpistasis_));
    ui->statusbar->showMessage(QStringLiteral("Epistasis %1 x %2: interaction %3 Hz")
                               .arg(lastEpistasis_.typeA, lastEpistasis_.typeB)
                               .arg(lastEpistasis_.interactionHz, 0, 'f', 2), 8000);
    updateArchiveUi();
    DrosofilaLiveDiagnostics::log(QString("Epistasis %1 x %2: WT=%3 A=%4 B=%5 AB=%6 expected=%7 interaction=%8")
        .arg(lastEpistasis_.typeA, lastEpistasis_.typeB)
        .arg(lastEpistasis_.wildType.targetMeanHz)
        .arg(lastEpistasis_.aOff.targetMeanHz)
        .arg(lastEpistasis_.bOff.targetMeanHz)
        .arg(lastEpistasis_.doubleOff.targetMeanHz)
        .arg(lastEpistasis_.expectedAdditiveHz)
        .arg(lastEpistasis_.interactionHz));
}

void MainWindow::clearEpistasisMatrix()
{
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    epistasisResults_.clear();
    initializeEpistasisMatrix();
    if (ui)
    {
        ui->epistasisMatrixInfoLB->setText(QStringLiteral("Interaction matrix: 0/21 pairs. Cells are Hz; red positive, blue negative; color saturates at |50 Hz|."));
    }
    updateArchiveUi();
}

void MainWindow::scriptEpistasisBatchFinished(int pairCount)
{
    ui->statusbar->showMessage(QStringLiteral("Epistasis batch complete: %1 pair(s)").arg(pairCount), 8000);
    updateArchiveUi();
}


void MainWindow::statisticsSelectionChanged()
{
    if (!ui || !experiment_ || !statistics_)
        return;
    const bool same = ui->statisticsNeuronACB->currentIndex() == ui->statisticsNeuronBCB->currentIndex();
    const bool ready = experiment_->isConnectomeLoaded() &&
                       experiment_->validateCausalCircuit().complete() &&
                       !experiment_->isRunning() && !statistics_->isRunning() &&
                       !(script_ && script_->isBusy()) && !same;
    ui->runStatisticsPB->setEnabled(ready);
}

void MainWindow::startStatistics()
{
    selectStatisticsPlot();
    if (!statistics_ || statistics_->isRunning() || !experiment_ || experiment_->isRunning() ||
        !experiment_->isConnectomeLoaded())
        return;

    const int aIndex = ui->statisticsNeuronACB->currentIndex();
    const int bIndex = ui->statisticsNeuronBCB->currentIndex();
    const std::vector<DrosofilaLive::CausalNeuronSpec>& specs = DrosofilaLive::validatedCausalCircuit();
    if (aIndex < 0 || bIndex < 0 || aIndex >= static_cast<int>(specs.size()) ||
        bIndex >= static_cast<int>(specs.size()) || aIndex == bIndex)
    {
        setIdleActivityMessage(QStringLiteral("Statistics ERROR: select two different causal neurons."));
        return;
    }

    const QString stimulusPath = defaultStimulusPath();
    if (!QFileInfo::exists(stimulusPath))
    {
        setIdleActivityMessage(QStringLiteral("Statistics ERROR: local Sugar stimulus file not found."));
        return;
    }

    DrosofilaLive::ExperimentParameters parameters = experimentParametersFromUi();
    parameters.ablatedRootIds.clear();
    const std::uint64_t targetRootId = 720575940660219265ULL;
    if (!containsRoot(parameters.watchedRootIds, targetRootId))
        parameters.watchedRootIds.append(targetRootId);

    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    lastStatistics_ = DrosofilaLive::StatisticalEpistasisResult();
    appendGlobalLogSection(QStringLiteral("Statistics"), QStringLiteral("Statistical validation running..."));
    beginActivity(QStringLiteral("Statistics"), QStringLiteral("Independent-seed epistasis validation"));
    setExperimentControlsEnabled(false);
    ui->stopStatisticsPB->setEnabled(true);

    if (!statistics_->runEpistasisValidation(parameters,
            specs[static_cast<std::size_t>(aIndex)].rootId,
            specs[static_cast<std::size_t>(bIndex)].rootId,
            ui->statisticsReplicatesSB->value()))
    {
        setExperimentControlsEnabled(true);
        finishActivity(QStringLiteral("Unable to start statistical validation."), false);
    }
}

void MainWindow::stopStatistics()
{
    if (statistics_ && statistics_->isRunning())
    {
        statistics_->requestStop();
        updateActivityDetail(-1, QStringLiteral("Stop requested; current epistasis replicate will finish first."));
        ui->stopStatisticsPB->setEnabled(false);
    }
}

void MainWindow::statisticsProgress(int percent, const QString& phase)
{
    updateScientificActivity(QStringLiteral("Statistics"), percent, phase);
}

QString MainWindow::statisticsSummaryText(const DrosofilaLive::StatisticalEpistasisResult& r) const
{
    if (!r.valid)
        return r.error.isEmpty() ? QStringLiteral("No valid statistical result.") : r.error;

    const auto row = [](const QString& name, const DrosofilaLive::StatisticalSummary& s) -> QString
    {
        if (!s.uncertaintyAvailable)
            return QStringLiteral("%1: mean %2 Hz | N %3 | CI n/a")
                .arg(name).arg(s.mean, 0, 'f', 3).arg(s.n);
        return QStringLiteral("%1: mean %2 Hz | std %3 | SEM %4 | 95% CI [%5, %6]")
            .arg(name)
            .arg(s.mean, 0, 'f', 3)
            .arg(s.stddev, 0, 'f', 3)
            .arg(s.sem, 0, 'f', 3)
            .arg(s.ci95Low, 0, 'f', 3)
            .arg(s.ci95High, 0, 'f', 3);
    };

    QStringList lines;
    lines << QStringLiteral("STATISTICAL EPISTASIS - TARGET/MN9")
          << QStringLiteral("Pair: %1 x %2").arg(r.typeA, r.typeB)
          << QStringLiteral("Replicates: %1/%2 | trials/replicate: %3 | seeds %4 .. %5")
                .arg(r.replicatesCompleted).arg(r.replicatesRequested).arg(r.trialsPerReplicate)
                .arg(r.baseSeed).arg(static_cast<quint64>(r.baseSeed) + static_cast<quint64>(r.replicatesCompleted - 1))
          << QStringLiteral("")
          << row(QStringLiteral("WT"), r.wildType)
          << row(QStringLiteral("A OFF"), r.aOff)
          << row(QStringLiteral("B OFF"), r.bOff)
          << row(QStringLiteral("A+B OFF"), r.doubleOff)
          << QStringLiteral("")
          << row(QStringLiteral("INTERACTION"), r.interaction)
          << QStringLiteral("Sign consistency: %1/%2 = %3% | + %4 / - %5 / zero %6")
                .arg(static_cast<int>(std::round(r.interaction.signConsistency * r.interaction.n)))
                .arg(r.interaction.n)
                .arg(100.0 * r.interaction.signConsistency, 0, 'f', 1)
                .arg(r.interaction.positiveCount).arg(r.interaction.negativeCount).arg(r.interaction.zeroCount)
          << QStringLiteral("")
          << QStringLiteral("95% CI uses two-sided Student-t across independent protocol replicates.")
          << QStringLiteral("The interval is an uncertainty estimate of this simulation protocol, not a biological population claim.");
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::storeStatisticalResult(const DrosofilaLive::StatisticalEpistasisResult& result)
{
    if (!result.valid) return;
    bool replaced = false;
    for (int i = 0; i < statisticalResults_.size(); ++i)
    {
        const DrosofilaLive::StatisticalEpistasisResult& old = statisticalResults_.at(i);
        if ((old.rootA == result.rootA && old.rootB == result.rootB) ||
            (old.rootA == result.rootB && old.rootB == result.rootA))
        {
            statisticalResults_[i] = result;
            replaced = true;
            break;
        }
    }
    if (!replaced) statisticalResults_.append(result);
    lastStatistics_ = result;
    if (statisticsPlot_) statisticsPlot_->setResult(result);
    rebuildStatisticsRanking();
}

void MainWindow::rebuildStatisticsRanking()
{
    if (!ui) return;
    QVector<DrosofilaLive::StatisticalEpistasisResult> rows = statisticalResults_;
    const int metric = ui->statisticsRankMetricCB->currentIndex();
    std::sort(rows.begin(), rows.end(), [metric](const DrosofilaLive::StatisticalEpistasisResult& a,
                                                  const DrosofilaLive::StatisticalEpistasisResult& b)
    {
        if (metric == 1)
            return a.interaction.signConsistency > b.interaction.signConsistency;
        if (metric == 2)
        {
            const double aw = a.interaction.ci95High - a.interaction.ci95Low;
            const double bw = b.interaction.ci95High - b.interaction.ci95Low;
            return aw < bw;
        }
        return std::fabs(a.interaction.mean) > std::fabs(b.interaction.mean);
    });

    ui->statisticsRankingTW->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
    {
        const DrosofilaLive::StatisticalEpistasisResult& v = rows.at(r);
        const QString values[8] = {
            QString::number(r + 1),
            QStringLiteral("%1 x %2").arg(v.typeA, v.typeB),
            QString::number(v.interaction.mean, 'f', 3),
            QString::number(v.interaction.stddev, 'f', 3),
            v.interaction.uncertaintyAvailable ? QString::number(v.interaction.ci95Low, 'f', 3) : QStringLiteral("n/a"),
            v.interaction.uncertaintyAvailable ? QString::number(v.interaction.ci95High, 'f', 3) : QStringLiteral("n/a"),
            QStringLiteral("%1%").arg(100.0 * v.interaction.signConsistency, 0, 'f', 1),
            QString::number(v.interaction.n)
        };
        for (int c = 0; c < 8; ++c)
        {
            QTableWidgetItem* item = new QTableWidgetItem(values[c]);
            if (c != 1) item->setTextAlignment(Qt::AlignCenter);
            ui->statisticsRankingTW->setItem(r, c, item);
        }
    }
    ui->statisticsRankingTW->resizeColumnsToContents();
}

void MainWindow::statisticsFinished()
{
    selectStatisticsPlot();
    if (!statistics_) return;
    lastStatistics_ = statistics_->result();
    setExperimentControlsEnabled(true);
    ui->stopStatisticsPB->setEnabled(false);

    if (!lastStatistics_.valid)
    {
        if (activityName_ == QStringLiteral("cvBasic script"))
            updateActivityDetail(0, QStringLiteral("DRO.STATS stopped/failed"));
        else
            finishActivity(QStringLiteral("Statistical validation stopped/failed."), false);
        appendGlobalLogSection(QStringLiteral("Statistics"), statisticsSummaryText(lastStatistics_));
        updateArchiveUi();
        return;
    }

    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    storeStatisticalResult(lastStatistics_);
    lastEpistasis_ = lastStatistics_.lastEpistasis;
    if (lastEpistasis_.valid) storeEpistasisResult(lastEpistasis_);
    if (activityName_ == QStringLiteral("cvBasic script"))
        updateActivityDetail(100, QStringLiteral("DRO.STATS complete"));
    else
        finishActivity(QStringLiteral("Statistical validation complete."), true);
    appendGlobalLogSection(QStringLiteral("Statistics"), statisticsSummaryText(lastStatistics_));
    ui->statusbar->showMessage(QStringLiteral("Validated %1 x %2: interaction %3 Hz, CI [%4, %5]")
        .arg(lastStatistics_.typeA, lastStatistics_.typeB)
        .arg(lastStatistics_.interaction.mean, 0, 'f', 2)
        .arg(lastStatistics_.interaction.ci95Low, 0, 'f', 2)
        .arg(lastStatistics_.interaction.ci95High, 0, 'f', 2), 10000);
    updateArchiveUi();
}

void MainWindow::clearStatistics()
{
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    statisticalResults_.clear();
    lastStatistics_ = DrosofilaLive::StatisticalEpistasisResult();
    ui->statisticsRankingTW->setRowCount(0);
    appendGlobalLog(QStringLiteral("[Statistics] results cleared."));
    if (statisticsPlot_) statisticsPlot_->clear();
    updateArchiveUi();
}

void MainWindow::statisticsRankChanged(int)
{
    rebuildStatisticsRanking();
}

void MainWindow::scriptStatisticsRankRequested()
{
    selectStatisticsPlot();
    rebuildStatisticsRanking();
    ui->controlTabs->setCurrentWidget(ui->statisticsTab);
    ui->statusbar->showMessage(QStringLiteral("Statistical epistasis ranking updated: %1 pair(s).")
                               .arg(statisticalResults_.size()), 6000);
}

DrosofilaLive::ExperimentArchiveBundle MainWindow::currentArchiveBundle() const
{
    DrosofilaLive::ExperimentArchiveBundle bundle;
    bundle.formatVersion = 3;
    bundle.applicationVersion = QStringLiteral("0.18-r1");
    bundle.connectomeVersion = QStringLiteral("FlyWire v783");
    bundle.createdUtc = archiveCreatedUtc_;
    bundle.sourcePath = archiveSourcePath_;
    bundle.experiment = lastExperiment_;
    bundle.comparison = lastComparison_;
    bundle.epistasis = epistasisResults_;
    bundle.statistics = statisticalResults_;
    bundle.explorer = lastCircuitExplore_;
    if (discovery_) bundle.discovery = discovery_->result();
    bundle.causalSurprise = lastCausalSurprise_;
    if (!archiveSourcePath_.isEmpty() && !reproductionArchive_.embeddedStimulus.isEmpty())
        bundle.embeddedStimulus = reproductionArchive_.embeddedStimulus;
    return bundle;
}

QString MainWindow::resolvedArchivePath(const QString& path) const
{
    return DrosofilaLiveRuntime::resolveArchivePath(path);
}

bool MainWindow::bestDiscoveryReplayComparison(DrosofilaLive::ComparisonResult* result, QString* label) const
{
    if (!result || !discovery_) return false;

    const DrosofilaLive::CausalDiscoveryResult discovery = discovery_->result();
    int bestIndex = -1;
    int bestRank = 0;
    for (int i = 0; i < discovery.entries.size(); ++i)
    {
        const DrosofilaLive::CausalDiscoveryEntry& entry = discovery.entries.at(i);
        const bool replayable = entry.valid && entry.comparison.valid &&
                                !entry.comparison.reference.frames.isEmpty() &&
                                !entry.comparison.perturbed.frames.isEmpty();
        if (!replayable) continue;

        const int rank = entry.causalRank > 0 ? entry.causalRank : (i + 1);
        if (bestIndex < 0 || rank < bestRank)
        {
            bestIndex = i;
            bestRank = rank;
        }
    }

    if (bestIndex < 0) return false;
    const DrosofilaLive::CausalDiscoveryEntry& best = discovery.entries.at(bestIndex);
    *result = best.comparison;
    if (label)
        *label = QStringLiteral("Discovery causal rank #%1: %2 [%3]")
            .arg(bestRank)
            .arg(best.candidate.type)
            .arg(static_cast<qulonglong>(best.candidate.rootId));
    return true;
}

bool MainWindow::bestDiscoveryReplayEpistasis(DrosofilaLive::EpistasisResult* result, QString* label) const
{
    if (!result || !discovery_) return false;

    const DrosofilaLive::CausalDiscoveryResult discovery = discovery_->result();
    int bestIndex = -1;
    int bestRank = 0;
    for (int i = 0; i < discovery.epistasisEntries.size(); ++i)
    {
        const DrosofilaLive::CausalDiscoveryEpistasisEntry& entry = discovery.epistasisEntries.at(i);
        const DrosofilaLive::EpistasisResult& epistasis = entry.lastEpistasis;
        const bool replayable = entry.valid && epistasis.valid &&
                                !epistasis.wildType.frames.isEmpty() &&
                                !epistasis.aOff.frames.isEmpty() &&
                                !epistasis.bOff.frames.isEmpty() &&
                                !epistasis.doubleOff.frames.isEmpty();
        if (!replayable) continue;

        const int rank = entry.interactionRank > 0 ? entry.interactionRank : (i + 1);
        if (bestIndex < 0 || rank < bestRank)
        {
            bestIndex = i;
            bestRank = rank;
        }
    }

    if (bestIndex < 0) return false;
    const DrosofilaLive::CausalDiscoveryEpistasisEntry& best = discovery.epistasisEntries.at(bestIndex);
    *result = best.lastEpistasis;
    if (label)
        *label = QStringLiteral("Discovery interaction rank #%1: %2 x %3")
            .arg(bestRank).arg(best.typeA, best.typeB);
    return true;
}

bool MainWindow::selectReplayNeuron(const DrosofilaLive::ExperimentResult& result)
{
    if (!ui || neuronIds_.empty() || result.watchedRootIds.isEmpty())
        return false;

    const std::uint64_t targetRootId = 720575940660219265ULL;
    std::uint64_t chosen = 0;
    if (containsRoot(result.watchedRootIds, targetRootId))
        chosen = targetRootId;

    if (!chosen)
    {
        for (int w = 0; w < result.watchedRootIds.size() && !chosen; ++w)
        {
            const std::uint64_t rootId = result.watchedRootIds.at(w);
            for (std::size_t i = 0; i < neuronIds_.size(); ++i)
                if (neuronIds_[i] == rootId) { chosen = rootId; break; }
        }
    }
    if (!chosen) return false;

    for (std::size_t i = 0; i < neuronIds_.size(); ++i)
    {
        if (neuronIds_[i] != chosen) continue;
        const QSignalBlocker blocker(ui->neuronCB);
        ui->neuronCB->setCurrentIndex(static_cast<int>(i));
        showSelectedNeuron();
        if (timeline_) timeline_->setNeuron(chosen, ui->neuronCB->currentText());
        return true;
    }
    return false;
}


void MainWindow::updateArchiveUi()
{
    if (!ui) return;
    const DrosofilaLive::ExperimentArchiveBundle bundle = currentArchiveBundle();
    const bool haveExperiment = lastExperiment_.valid && !lastExperiment_.frames.isEmpty();
    const bool haveLegacyComparison = lastComparison_.valid &&
                                      !lastComparison_.reference.frames.isEmpty() &&
                                      !lastComparison_.perturbed.frames.isEmpty();
    DrosofilaLive::ComparisonResult discoveryComparison;
    QString discoveryComparisonLabel;
    const bool haveDiscoveryComparison = bestDiscoveryReplayComparison(&discoveryComparison, &discoveryComparisonLabel);
    const bool haveComparison = haveLegacyComparison || haveDiscoveryComparison;

    const bool haveLegacyEpistasis = lastEpistasis_.valid &&
                                     !lastEpistasis_.wildType.frames.isEmpty() &&
                                     !lastEpistasis_.aOff.frames.isEmpty() &&
                                     !lastEpistasis_.bOff.frames.isEmpty() &&
                                     !lastEpistasis_.doubleOff.frames.isEmpty();
    DrosofilaLive::EpistasisResult discoveryEpistasis;
    QString discoveryEpistasisLabel;
    const bool haveDiscoveryEpistasis = bestDiscoveryReplayEpistasis(&discoveryEpistasis, &discoveryEpistasisLabel);
    const bool haveEpistasis = haveLegacyEpistasis || haveDiscoveryEpistasis;
    const bool haveSingle = haveExperiment || haveDiscoveryComparison || haveDiscoveryEpistasis;
    const bool busy = reproductionActive_ || (experiment_ && experiment_->isRunning()) || (script_ && script_->isBusy()) ||
                      (statistics_ && statistics_->isRunning()) || (discovery_ && discovery_->isRunning()) ||
                      circuitExplorerBusy_;

    ui->archivePathLE->setText(archiveSourcePath_);
    ui->archiveOpenPB->setEnabled(!busy);
    ui->archiveSavePB->setEnabled(bundle.hasAnyResult() && !busy);
    ui->archiveExportPB->setEnabled(bundle.hasAnyResult() && !busy);
    ui->archiveReplayModeCB->setEnabled(!busy && (haveSingle || haveComparison || haveEpistasis));
    // Replay availability is a property of the archive. The 3D circuit is checked when
    // Replay is pressed, so a loaded archive never appears falsely non-replayable.
    ui->archivePlayPB->setEnabled(!busy && (haveSingle || haveComparison || haveEpistasis));

    QString reproduceReason;
    const bool reproduceReady = reproductionArchiveReady(&reproduceReason);
    const bool preflightDone = !reproductionAnalysis_.preflight.isEmpty();
    ui->archivePreflightPB->setEnabled(!busy && reproduceReady);
    const bool reproduceStimulusAvailable = !reproductionArchive_.embeddedStimulus.isEmpty() || QFileInfo::exists(defaultStimulusPath());
    // v0.16.1-r1: REPRODUCE is a two-step workflow. A freshly loaded archive
    // must first pass through the visible Preflight action; do not silently
    // enable Reproduce just because its raw inputs happen to be present.
    ui->archiveReproducePB->setEnabled(!busy && reproduceReady && preflightDone &&
                                       experiment_ && experiment_->isConnectomeLoaded() && reproduceStimulusAvailable);
    ui->archiveReproduceStopPB->setEnabled(reproductionActive_);
    ui->archiveReproduceReportPB->setEnabled(!reproductionReportPath_.isEmpty() && QFileInfo::exists(reproductionReportPath_));

    const int mode = ui->archiveReplayModeCB->currentIndex();
    const bool currentModeAvailable = (mode == 0 && haveSingle) ||
                                      (mode >= 1 && mode <= 3 && haveComparison) ||
                                      (mode >= 4 && mode <= 7 && haveEpistasis);
    if (!currentModeAvailable)
    {
        if (haveSingle) ui->archiveReplayModeCB->setCurrentIndex(0);
        else if (haveComparison) ui->archiveReplayModeCB->setCurrentIndex(3);
        else if (haveEpistasis) ui->archiveReplayModeCB->setCurrentIndex(4);
    }

    QString replayInfo;
    if (haveLegacyEpistasis)
        replayInfo = QStringLiteral("Replay uses stored frames only. Last epistasis pair: %1 x %2.")
            .arg(lastEpistasis_.typeA, lastEpistasis_.typeB);
    else if (haveDiscoveryComparison && haveDiscoveryEpistasis)
        replayInfo = QStringLiteral("Discovery replay available: %1; %2.")
            .arg(discoveryComparisonLabel, discoveryEpistasisLabel);
    else if (haveDiscoveryEpistasis)
        replayInfo = QStringLiteral("Replay source available from %1.").arg(discoveryEpistasisLabel);
    else if (!haveLegacyComparison && haveDiscoveryComparison)
        replayInfo = QStringLiteral("Replay source available from %1.").arg(discoveryComparisonLabel);
    else
        replayInfo = QStringLiteral("Replay uses stored frames only and never runs the LIF engine.");

    if (!circuitLoaded_ && (haveSingle || haveComparison || haveEpistasis))
        replayInfo += QStringLiteral(" Load the validated 7-neuron circuit to visualize it in 3D.");
    ui->archiveReplayInfoLB->setText(replayInfo);

    if (bundle.hasAnyResult())
    {
        ui->archiveStatusLB->setText(archiveSourcePath_.isEmpty()
            ? QStringLiteral("Current results are in memory and can be saved to a .dro archive.")
            : QStringLiteral("Archive loaded/saved. Replay uses stored frames and does not run LIF."));
    }
    else
    {
        ui->archiveStatusLB->setText(QStringLiteral("No archive loaded. Results can be saved after a completed run."));
    }
}

bool MainWindow::saveArchiveToPath(const QString& requestedPath, bool fromScript)
{
    QString path = resolvedArchivePath(requestedPath);
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".dro"), Qt::CaseInsensitive)) path += QStringLiteral(".dro");

    DrosofilaLive::ExperimentArchiveBundle bundle = currentArchiveBundle();
    if (bundle.createdUtc.isEmpty())
        bundle.createdUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QString error;
    if (!DrosofilaLive::ExperimentArchive::save(path, bundle, &error))
    {
        ui->archiveStatusLB->setText(QStringLiteral("SAVE ERROR: %1").arg(error));
        appendGlobalLogSection(QStringLiteral("Archive SAVE ERROR"), error);
        ui->statusbar->showMessage(QStringLiteral("Archive save failed: %1").arg(error), 8000);
        if (fromScript) scriptOutputLine(QStringLiteral("[archive ERROR] %1").arg(error));
        return false;
    }

    archiveSourcePath_ = path;
    archiveCreatedUtc_ = bundle.createdUtc;
    DrosofilaLive::ExperimentArchiveBundle savedBaseline;
    QString baselineError;
    if (DrosofilaLive::ExperimentArchive::load(path, &savedBaseline, &baselineError))
    {
        reproductionArchive_ = savedBaseline;
        reproductionArchive_.sourcePath = path;
        reproductionAnalysis_ = DrosofilaLive::ReproductionAnalysis();
        reproductionReportPath_.clear();
    }
    ui->archiveStatusLB->setText(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
    appendGlobalLog(QStringLiteral("[Archive] saved %1").arg(path));
    ui->statusbar->showMessage(QStringLiteral("Experiment archive saved: %1").arg(path), 8000);
    if (fromScript) scriptOutputLine(QStringLiteral("[archive] saved %1").arg(path));
    updateArchiveUi();
    return true;
}

void MainWindow::applyArchiveBundle(const DrosofilaLive::ExperimentArchiveBundle& bundle, const QString& path)
{
    stopPlayback(true);
    lastExperiment_ = bundle.experiment;
    lastComparison_ = bundle.comparison;
    lastEpistasis_ = DrosofilaLive::EpistasisResult();

    const QVector<DrosofilaLive::EpistasisResult> loadedEpistasis = bundle.epistasis;
    epistasisResults_.clear();
    initializeEpistasisMatrix();
    for (int i = 0; i < loadedEpistasis.size(); ++i)
    {
        storeEpistasisResult(loadedEpistasis.at(i));
        lastEpistasis_ = loadedEpistasis.at(i);
    }

    statisticalResults_.clear();
    lastStatistics_ = DrosofilaLive::StatisticalEpistasisResult();
    if (statisticsPlot_) statisticsPlot_->clear();
    for (int i = 0; i < bundle.statistics.size(); ++i)
    {
        storeStatisticalResult(bundle.statistics.at(i));
        lastStatistics_ = bundle.statistics.at(i);
    }
    if (!lastEpistasis_.valid && lastStatistics_.valid)
        lastEpistasis_ = lastStatistics_.lastEpistasis;

    rebuildStatisticsRanking();

    // v3 scientific state: restore Explorer + Discovery without running LIF.
    lastCircuitExplore_ = bundle.explorer;
    if (lastCircuitExplore_.valid)
    {
        populateCircuitExplorerResult(lastCircuitExplore_);
        if (circuitGraph_) circuitGraph_->setResult(lastCircuitExplore_);
        ui->explorerRootLE->setText(lastCircuitExplore_.rootQuery.isEmpty()
            ? QString::number(static_cast<qulonglong>(lastCircuitExplore_.rootId)) : lastCircuitExplore_.rootQuery);
        ui->explorerDirectionCB->setCurrentIndex(lastCircuitExplore_.direction);
        ui->explorerDepthSB->setValue(lastCircuitExplore_.maxDepth);
        ui->explorerMinSynSB->setValue(lastCircuitExplore_.minimumSynapses);
        ui->explorerMaxNeuronsSB->setValue(lastCircuitExplore_.maximumNeurons);
        ui->explorerStatusLB->setText(QStringLiteral("Restored from archive: %1 candidate(s), %2 edge(s).")
            .arg(lastCircuitExplore_.candidates.size()).arg(lastCircuitExplore_.edges.size()));
        ui->explorerGraphInfoLB->setText(QStringLiteral("Archive root %1 [%2] | %3 candidate(s) | %4 displayed edge(s)")
            .arg(lastCircuitExplore_.rootType)
            .arg(static_cast<qulonglong>(lastCircuitExplore_.rootId))
            .arg(lastCircuitExplore_.candidates.size()).arg(lastCircuitExplore_.edges.size()));
    }
    else
    {
        ui->explorerResultsTW->setRowCount(0);
        if (circuitGraph_) circuitGraph_->clear();
        ui->explorerStatusLB->setText(QStringLiteral("Archive has no stored Circuit Explorer result."));
        ui->explorerGraphInfoLB->setText(QStringLiteral("No stored Explorer graph."));
    }

    if (discovery_)
    {
        discovery_->restoreResult(bundle.discovery);
        populateCausalDiscoveryResult(bundle.discovery);
        populateCausalDiscoveryEpistasisResult(bundle.discovery);
    }

    lastCausalSurprise_ = bundle.causalSurprise;
    causalSurpriseReportPath_.clear();
    if (lastCausalSurprise_.valid)
    {
        causalSurpriseReportPath_ = causalSurpriseReportFilePath();
        QString surpriseReportError;
        if (!DrosofilaLive::CausalStructureAnalyzer::writeHtmlReport(causalSurpriseReportPath_, lastCausalSurprise_, &surpriseReportError))
        {
            appendGlobalLog(QStringLiteral("[CAUSAL SURPRISE] restored report error: %1").arg(surpriseReportError));
            causalSurpriseReportPath_.clear();
        }
        populateCausalSurpriseResult(lastCausalSurprise_);
    }
    else
    {
        invalidateCausalSurprise(QStringLiteral("Archive has no stored Causal Surprise analysis. Press Analyze after validation."));
    }

    const DrosofilaLive::CausalDiscoveryProvenance provenance = bundle.discovery.provenance;
    if (provenance.valid)
    {
        if (provenance.selectionMode.compare(QStringLiteral("LAYERED"), Qt::CaseInsensitive) == 0)
            ui->discoverySelectionCB->setCurrentIndex(1);
        else if (!provenance.selectionMode.isEmpty())
            ui->discoverySelectionCB->setCurrentIndex(0);
        if (provenance.selectionTopN > 0) ui->discoveryTopSB->setValue(provenance.selectionTopN);
        if (bundle.discovery.screenReplicatesRequested > 0) ui->discoveryScreenReplicatesSB->setValue(bundle.discovery.screenReplicatesRequested);
        if (provenance.validationTopN > 0) ui->discoveryValidateTopSB->setValue(provenance.validationTopN);
        if (bundle.discovery.validationReplicatesRequested > 0) ui->discoveryReplicatesSB->setValue(bundle.discovery.validationReplicatesRequested);
        if (provenance.epistasisTopN > 0) ui->discoveryEpistasisTopSB->setValue(provenance.epistasisTopN);
        if (bundle.discovery.epistasisReplicatesRequested > 0) ui->discoveryEpistasisReplicatesSB->setValue(bundle.discovery.epistasisReplicatesRequested);

        const QString gate = provenance.epistasisGateMode.trimmed().toUpper();
        const QSignalBlocker blockCi(ui->discoveryRequireCiCB);
        const QSignalBlocker blockSign(ui->discoveryRequireSignCB);
        const QSignalBlocker blockMinSign(ui->discoveryMinSignDSB);
        ui->discoveryRequireCiCB->setChecked(gate == QStringLiteral("CI95") || gate == QStringLiteral("BOTH"));
        ui->discoveryRequireSignCB->setChecked(gate == QStringLiteral("SIGN") || gate == QStringLiteral("BOTH"));
        ui->discoveryMinSignDSB->setValue(100.0 * provenance.epistasisMinimumSignConsistency);

        const DrosofilaLive::ExperimentParameters* restoredParameters = 0;
        if (provenance.epistasisParametersValid) restoredParameters = &provenance.epistasisParameters;
        else if (provenance.validationParametersValid) restoredParameters = &provenance.validationParameters;
        else if (provenance.screenParametersValid) restoredParameters = &provenance.screenParameters;
        if (restoredParameters)
        {
            ui->durationSB->setValue(restoredParameters->durationMs);
            ui->sugarRateSB->setValue(restoredParameters->sugarRateHz);
            ui->trialsSB->setValue(restoredParameters->trials);
            ui->seedSB->setValue(static_cast<int>(restoredParameters->randomSeed));
        }
    }

    if (script_) script_->restoreScientificState(lastCircuitExplore_, bundle.discovery);

    if (!bundle.discovery.entries.isEmpty() || !bundle.discovery.epistasisEntries.isEmpty())
    {
        const QString origin = provenance.origin.isEmpty() ? QStringLiteral("unknown") : provenance.origin;
        ui->discoveryStatusLB->setText(QStringLiteral("Restored from .dro v%1: %2 screen hit(s), %3 epistasis pair(s) | provenance %4.")
            .arg(bundle.formatVersion).arg(bundle.discovery.entries.size()).arg(bundle.discovery.epistasisEntries.size()).arg(origin));
    }
    else
    {
        ui->discoveryStatusLB->setText(QStringLiteral("Archive has no stored Discovery state."));
    }

    archiveSourcePath_ = path;
    archiveCreatedUtc_ = bundle.createdUtc;
    reproductionArchive_ = bundle;
    reproductionArchive_.sourcePath = path;
    reproductionAnalysis_ = DrosofilaLive::ReproductionAnalysis();
    reproductionReportPath_.clear();

    if (timeline_)
    {
        if (lastComparison_.valid) timeline_->setComparison(lastComparison_);
        else if (lastExperiment_.valid) timeline_->setResult(lastExperiment_);
        else timeline_->clear();
    }
    updateTimelineSelection();
    appendGlobalLogSection(QStringLiteral("Archive loaded"),
                           QStringLiteral("%1\n%2")
                               .arg(QFileInfo(path).fileName(), DrosofilaLive::ExperimentArchive::summary(bundle)));
    setIdleActivityMessage(QStringLiteral("Archive loaded; stored results ready for replay."));
    setExperimentControlsEnabled(true);
    updateArchiveUi();
    if (reproductionArchiveReady())
        ui->archiveReproduceStatusLB->setText(QStringLiteral("Archive ready - run Preflight before Reproduce."));
}

bool MainWindow::loadArchiveFromPath(const QString& requestedPath, bool fromScript)
{
    const QString path = resolvedArchivePath(requestedPath);
    if (path.isEmpty()) return false;

    DrosofilaLive::ExperimentArchiveBundle bundle;
    QString error;
    if (!DrosofilaLive::ExperimentArchive::load(path, &bundle, &error))
    {
        ui->archiveStatusLB->setText(QStringLiteral("LOAD ERROR: %1").arg(error));
        appendGlobalLogSection(QStringLiteral("Archive LOAD ERROR"), error);
        ui->statusbar->showMessage(QStringLiteral("Archive load failed: %1").arg(error), 8000);
        if (fromScript) scriptOutputLine(QStringLiteral("[archive ERROR] %1").arg(error));
        return false;
    }

    applyArchiveBundle(bundle, path);
    ui->archiveStatusLB->setText(QStringLiteral("Loaded %1").arg(QFileInfo(path).fileName()));
    ui->statusbar->showMessage(QStringLiteral("Experiment archive loaded without running LIF."), 8000);
    if (fromScript) scriptOutputLine(QStringLiteral("[archive] loaded %1").arg(path));
    return true;
}

bool MainWindow::exportArchiveCsvToPath(const QString& requestedPath, bool fromScript)
{
    const QString path = DrosofilaLiveRuntime::resolveResultPath(requestedPath);
    if (path.isEmpty()) return false;

    QStringList written;
    QString error;
    if (!DrosofilaLive::ExperimentArchive::exportCsv(path, currentArchiveBundle(), &written, &error))
    {
        ui->archiveStatusLB->setText(QStringLiteral("CSV ERROR: %1").arg(error));
        appendGlobalLogSection(QStringLiteral("Archive CSV ERROR"), error);
        ui->statusbar->showMessage(QStringLiteral("CSV export failed: %1").arg(error), 8000);
        if (fromScript) scriptOutputLine(QStringLiteral("[archive ERROR] %1").arg(error));
        return false;
    }

    ui->archiveStatusLB->setText(QStringLiteral("CSV export complete: %1 file(s).").arg(written.size()));
    appendGlobalLog(QStringLiteral("[Archive] CSV export: %1").arg(written.join(QStringLiteral(" | "))));
    ui->statusbar->showMessage(QStringLiteral("CSV export complete: %1").arg(written.join(QStringLiteral(" | "))), 10000);
    if (fromScript) scriptOutputLine(QStringLiteral("[archive] CSV: %1").arg(written.join(QStringLiteral(" | "))));
    return true;
}


QVector<DrosofilaLive::ReproductionCheck> MainWindow::buildReproductionPreflight() const
{
    QVector<DrosofilaLive::ReproductionCheck> checks;
    const DrosofilaLive::ExperimentArchiveBundle& b = reproductionArchive_;

    DrosofilaLive::ReproductionCheck c;
    c.category = QStringLiteral("Archive");
    c.item = QStringLiteral("format/provenance");
    c.archived = QStringLiteral("v%1 / %2").arg(b.formatVersion).arg(b.discovery.provenance.valid ? QStringLiteral("present") : QStringLiteral("missing"));
    c.current = QStringLiteral("reader v1-v3 / REPRODUCE requires v3 provenance");
    c.required = true;
    c.match = b.formatVersion >= 3 && b.discovery.provenance.valid && b.discovery.provenance.screenParametersValid;
    checks.append(c);

    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("Application");
    c.item = QStringLiteral("version");
    c.archived = b.applicationVersion;
    c.current = QStringLiteral("0.18-r1");
    c.required = false;
    c.match = c.archived == c.current;
    checks.append(c);

    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("Connectome");
    c.item = QStringLiteral("declared version");
    c.archived = b.connectomeVersion;
    c.current = QStringLiteral("FlyWire v783");
    c.required = true;
    c.match = !c.archived.isEmpty() && c.archived == c.current;
    checks.append(c);

    const DrosofilaLive::CausalAnchorValidation anchors = experiment_ ? experiment_->validateCausalCircuit() : DrosofilaLive::CausalAnchorValidation();
    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("Connectome");
    c.item = QStringLiteral("validated causal anchors");
    c.archived = QStringLiteral("7/7 expected");
    c.current = experiment_ && experiment_->isConnectomeLoaded()
        ? QStringLiteral("%1/%2 IDs, %3 labels").arg(anchors.idsFound).arg(anchors.expected).arg(anchors.labelsMatched)
        : QStringLiteral("connectome not loaded");
    c.required = true;
    c.match = anchors.complete() && anchors.labelsMatched == anchors.expected;
    checks.append(c);

    const DrosofilaLive::ExperimentResult* archivedResult = representativeArchiveResult(b);
    const QString archivedSha = archivedResult ? archivedResult->stimulusSha256 : QString();
    const QString embeddedSha = b.embeddedStimulus.isEmpty() ? QString()
        : QString::fromLatin1(QCryptographicHash::hash(b.embeddedStimulus, QCryptographicHash::Sha256).toHex());
    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("Stimulus");
    c.item = QStringLiteral("embedded archive copy");
    c.archived = archivedSha.isEmpty() ? QStringLiteral("SHA unavailable") : archivedSha;
    c.current = b.embeddedStimulus.isEmpty() ? QStringLiteral("not embedded") : embeddedSha;
    c.required = !b.embeddedStimulus.isEmpty();
    c.match = !b.embeddedStimulus.isEmpty() && !archivedSha.isEmpty() && embeddedSha == archivedSha;
    checks.append(c);

    const QString stimulusPath = defaultStimulusPath();
    const QString currentSha = sha256File(stimulusPath);
    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("Stimulus");
    c.item = QStringLiteral("local Sugar fallback");
    c.archived = archivedSha.isEmpty() ? QStringLiteral("SHA unavailable") : archivedSha;
    c.current = currentSha.isEmpty() ? QStringLiteral("missing: %1").arg(stimulusPath) : currentSha;
    c.required = b.embeddedStimulus.isEmpty();
    c.match = !archivedSha.isEmpty() && archivedSha == currentSha;
    checks.append(c);

    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("LIF");
    c.item = QStringLiteral("validated model snapshot");
    c.archived = archivedResult ? lifFingerprint(archivedResult->lifModel) : QStringLiteral("unavailable");
    c.current = lifFingerprint(DrosofilaLive::SimulationController::validatedLifModelSnapshot());
    c.required = true;
    c.match = archivedResult && c.archived == c.current;
    checks.append(c);

    const DrosofilaLive::CausalDiscoveryProvenance& p = b.discovery.provenance;
    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("Protocol");
    c.item = QStringLiteral("Causal target");
    c.archived = b.discovery.targetRootId != 0
        ? QStringLiteral("%1 [%2]")
              .arg(b.discovery.targetType.isEmpty() ? QStringLiteral("unannotated") : b.discovery.targetType)
              .arg(static_cast<qulonglong>(b.discovery.targetRootId))
        : QStringLiteral("missing");
    c.current = QStringLiteral("CB0701/MN9 [720575940660219265]");
    c.required = true;
    c.match = b.discovery.targetRootId == 720575940660219265ULL;
    checks.append(c);

    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("Protocol");
    c.item = QStringLiteral("Explorer parameters");
    c.archived = p.valid
        ? QStringLiteral("root=%1 dir=%2 depth=%3 minSyn=%4 maxN=%5")
              .arg(p.explorerParameters.rootText).arg(p.explorerParameters.direction)
              .arg(p.explorerParameters.maxDepth).arg(p.explorerParameters.minimumSynapses).arg(p.explorerParameters.maximumNeurons)
        : QStringLiteral("missing");
    c.current = QStringLiteral("will replay archived values");
    c.required = true;
    c.match = p.valid && !p.explorerParameters.rootText.trimmed().isEmpty();
    checks.append(c);

    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("Protocol");
    c.item = QStringLiteral("Candidate selection");
    c.archived = QStringLiteral("%1 | top %2 | selected %3")
        .arg(p.selectionMode.isEmpty() ? QStringLiteral("missing") : p.selectionMode)
        .arg(p.selectionTopN)
        .arg(p.selectedCandidates);
    c.current = QStringLiteral("will reconstruct the same selection policy from reproduced Explorer output");
    c.required = true;
    c.match = p.selectionTopN > 0 &&
        (p.selectionMode.compare(QStringLiteral("GLOBAL"), Qt::CaseInsensitive) == 0 ||
         p.selectionMode.compare(QStringLiteral("LAYERED"), Qt::CaseInsensitive) == 0);
    checks.append(c);

    c = DrosofilaLive::ReproductionCheck();
    c.category = QStringLiteral("Protocol");
    c.item = QStringLiteral("Discovery phases");
    c.archived = QStringLiteral("screen=%1 x%2 | validation=%3 x%4 | epistasis roots=%5 x%6")
        .arg(b.discovery.entries.size()).arg(b.discovery.screenReplicatesRequested)
        .arg(b.discovery.validationRootIds.size()).arg(b.discovery.validationReplicatesRequested)
        .arg(b.discovery.epistasisRootIds.size()).arg(b.discovery.epistasisReplicatesRequested);
    c.current = QStringLiteral("will replay archived roots, seeds and replicate counts");
    c.required = true;
    c.match = !b.discovery.entries.isEmpty() && p.screenParametersValid &&
        b.discovery.screenReplicatesRequested > 0 &&
        (b.discovery.validationRootIds.isEmpty() ||
            (p.validationParametersValid && b.discovery.validationReplicatesRequested > 0)) &&
        (b.discovery.epistasisRootIds.size() < 2 ||
            (p.epistasisParametersValid && b.discovery.epistasisReplicatesRequested > 0));
    checks.append(c);

    return checks;
}

bool MainWindow::reproductionArchiveReady(QString* reason) const
{
    if (reproductionArchive_.formatVersion < 3)
    {
        if (reason) *reason = QStringLiteral("REPRODUCE requires a .dro v3 archive.");
        return false;
    }
    if (reproductionArchive_.discovery.entries.isEmpty())
    {
        if (reason) *reason = QStringLiteral("Archive contains no Discovery screening state.");
        return false;
    }
    const DrosofilaLive::CausalDiscoveryProvenance& p = reproductionArchive_.discovery.provenance;
    if (!p.valid || !p.screenParametersValid)
    {
        if (reason) *reason = QStringLiteral("Archive has no complete Discovery provenance.");
        return false;
    }
    if (reproductionArchive_.discovery.targetRootId != 720575940660219265ULL)
    {
        if (reason) *reason = QStringLiteral("Archive target is not the validated CB0701/MN9 target supported by this engine.");
        return false;
    }
    if (!reproductionArchive_.explorer.valid || p.explorerParameters.rootText.trimmed().isEmpty())
    {
        if (reason) *reason = QStringLiteral("Archive has no reproducible Explorer state/root query.");
        return false;
    }
    if (p.selectionTopN <= 0 ||
        (p.selectionMode.compare(QStringLiteral("GLOBAL"), Qt::CaseInsensitive) != 0 &&
         p.selectionMode.compare(QStringLiteral("LAYERED"), Qt::CaseInsensitive) != 0))
    {
        if (reason) *reason = QStringLiteral("Archive has no valid Discovery candidate-selection policy.");
        return false;
    }
    if (reproductionArchive_.discovery.screenReplicatesRequested <= 0)
    {
        if (reason) *reason = QStringLiteral("Archive has no valid screening replicate count.");
        return false;
    }
    if (!reproductionArchive_.discovery.validationRootIds.isEmpty() &&
        (!p.validationParametersValid || reproductionArchive_.discovery.validationReplicatesRequested <= 0))
    {
        if (reason) *reason = QStringLiteral("Archive validation provenance is incomplete.");
        return false;
    }
    if (reproductionArchive_.discovery.epistasisRootIds.size() >= 2 &&
        (!p.epistasisParametersValid || reproductionArchive_.discovery.epistasisReplicatesRequested <= 0))
    {
        if (reason) *reason = QStringLiteral("Archive epistasis provenance is incomplete.");
        return false;
    }
    return true;
}

void MainWindow::populateReproductionPreflight(const QVector<DrosofilaLive::ReproductionCheck>& checks)
{
    if (!ui || !ui->archiveReproduceTW) return;
    ui->archiveReproduceTW->setRowCount(0);
    for (int i = 0; i < checks.size(); ++i)
    {
        const DrosofilaLive::ReproductionCheck& c = checks.at(i);
        const int row = ui->archiveReproduceTW->rowCount();
        ui->archiveReproduceTW->insertRow(row);
        const QString values[8] = {
            c.category, c.item, QStringLiteral("Preflight"), c.archived, c.current,
            QStringLiteral("-"), QStringLiteral("-"),
            c.match ? QStringLiteral("MATCH") : (c.required ? QStringLiteral("MISMATCH") : QStringLiteral("INFO"))
        };
        for (int col = 0; col < 8; ++col)
        {
            QTableWidgetItem* item = new QTableWidgetItem(values[col]);
            if (col == 7)
            {
                if (c.match) item->setForeground(QBrush(QColor(32, 112, 66)));
                else if (c.required) item->setForeground(QBrush(QColor(174, 50, 50)));
                else item->setForeground(QBrush(QColor(166, 112, 24)));
            }
            ui->archiveReproduceTW->setItem(row, col, item);
        }
    }
    ui->archiveReproduceTW->resizeColumnsToContents();
    ui->archiveReproduceTW->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::populateReproductionMetrics(const QVector<DrosofilaLive::ReproductionMetric>& metrics)
{
    if (!ui || !ui->archiveReproduceTW) return;
    populateReproductionPreflight(reproductionAnalysis_.preflight);
    for (int i = 0; i < metrics.size(); ++i)
    {
        const DrosofilaLive::ReproductionMetric& m = metrics.at(i);
        const int row = ui->archiveReproduceTW->rowCount();
        ui->archiveReproduceTW->insertRow(row);
        const QString values[8] = {
            m.phase, m.subject, m.metric,
            QString::number(m.archived, 'g', 15),
            QString::number(m.reproduced, 'g', 15),
            QString::number(m.delta, 'g', 15),
            QString::number(m.tolerance, 'g', 6),
            m.match ? QStringLiteral("PASS") : QStringLiteral("FAIL")
        };
        for (int col = 0; col < 8; ++col)
        {
            QTableWidgetItem* item = new QTableWidgetItem(values[col]);
            if (col >= 3) item->setTextAlignment(Qt::AlignCenter);
            if (col == 7) item->setForeground(QBrush(m.match ? QColor(32, 112, 66) : QColor(174, 50, 50)));
            ui->archiveReproduceTW->setItem(row, col, item);
        }
    }
    ui->archiveReproduceTW->resizeColumnsToContents();
    ui->archiveReproduceTW->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::runReproductionPreflight()
{
    if (reproductionActive_) return;
    QString reason;
    if (!reproductionArchiveReady(&reason))
    {
        ui->archiveReproduceStatusLB->setText(reason);
        ui->archiveReproducePB->setEnabled(false);
        return;
    }

    reproductionAnalysis_ = DrosofilaLive::ReproductionAnalysis();
    reproductionAnalysis_.preflight = buildReproductionPreflight();
    reproductionAnalysis_.environmentExact = true;
    for (int i = 0; i < reproductionAnalysis_.preflight.size(); ++i)
        if (reproductionAnalysis_.preflight.at(i).required && !reproductionAnalysis_.preflight.at(i).match)
            reproductionAnalysis_.environmentExact = false;

    populateReproductionPreflight(reproductionAnalysis_.preflight);
    const int mismatches = reproductionAnalysis_.requiredPreflightMismatchCount();
    ui->archiveReproduceStatusLB->setText(mismatches == 0
        ? QStringLiteral("READY FOR EXACT REPRODUCTION - scientific environment matches the archive.")
        : QStringLiteral("NON-EXACT ENVIRONMENT - %1 required preflight mismatch(es). Reproduction may still be run as a comparison.").arg(mismatches));

    const bool stimulusAvailable = !reproductionArchive_.embeddedStimulus.isEmpty() || QFileInfo::exists(defaultStimulusPath());
    const bool runnable = experiment_ && experiment_->isConnectomeLoaded() && stimulusAvailable;
    ui->archiveReproducePB->setEnabled(runnable);
    appendGlobalLogSection(QStringLiteral("REPRODUCE preflight"),
        QStringLiteral("Archive: %1\nEnvironment: %2\nRequired mismatches: %3")
            .arg(reproductionArchive_.sourcePath)
            .arg(mismatches == 0 ? QStringLiteral("EXACT") : QStringLiteral("DIFFERENT"))
            .arg(mismatches));
}

QVector<DrosofilaLive::CausalDiscoveryCandidate> MainWindow::reproductionCandidates(const DrosofilaLive::CircuitExploreResult& explorer) const
{
    QVector<DrosofilaLive::CausalDiscoveryCandidate> candidates;
    const DrosofilaLive::CausalDiscoveryProvenance& p = reproductionArchive_.discovery.provenance;
    const int requested = std::max(1, p.selectionTopN);
    const bool layered = p.selectionMode.compare(QStringLiteral("LAYERED"), Qt::CaseInsensitive) == 0;
    QVector<std::uint64_t> seen;

    if (layered)
    {
        for (int depth = 1; depth <= explorer.maxDepth; ++depth)
        {
            int added = 0;
            for (int i = 0; i < explorer.candidates.size() && added < requested; ++i)
            {
                const DrosofilaLive::CircuitExploreCandidate& source = explorer.candidates.at(i);
                if (source.depth != depth || containsRoot(seen, source.rootId)) continue;
                DrosofilaLive::CausalDiscoveryCandidate c;
                c.rootId = source.rootId; c.type = source.primaryType; c.graphRank = i + 1;
                c.depth = source.depth; c.synapseStrength = source.synapseStrength; c.graphScore = source.score;
                candidates.append(c); seen.append(c.rootId); ++added;
            }
        }
    }
    else
    {
        const int n = std::min(requested, explorer.candidates.size());
        for (int i = 0; i < n; ++i)
        {
            const DrosofilaLive::CircuitExploreCandidate& source = explorer.candidates.at(i);
            DrosofilaLive::CausalDiscoveryCandidate c;
            c.rootId = source.rootId; c.type = source.primaryType; c.graphRank = i + 1;
            c.depth = source.depth; c.synapseStrength = source.synapseStrength; c.graphScore = source.score;
            candidates.append(c);
        }
    }
    return candidates;
}

bool MainWindow::prepareReproductionStimulus(QString* path, QString* error) const
{
    if (!path) return false;
    path->clear();
    const DrosofilaLive::ExperimentResult* archivedResult = representativeArchiveResult(reproductionArchive_);
    const QString archivedSha = archivedResult ? archivedResult->stimulusSha256 : QString();

    if (!reproductionArchive_.embeddedStimulus.isEmpty())
    {
        const QString embeddedSha = QString::fromLatin1(
            QCryptographicHash::hash(reproductionArchive_.embeddedStimulus, QCryptographicHash::Sha256).toHex());
        if (!archivedSha.isEmpty() && embeddedSha != archivedSha)
        {
            if (error) *error = QStringLiteral("Embedded stimulus SHA-256 does not match the archived run metadata.");
            return false;
        }
        const QString dir = QDir(DrosofilaLiveRuntime::workDir()).filePath(QStringLiteral("reproduce"));
        QDir().mkpath(dir);
        const QString fileName = QStringLiteral("stimulus_%1.txt")
            .arg((archivedSha.isEmpty() ? embeddedSha : archivedSha).left(16));
        const QString outPath = QDir(dir).filePath(fileName);
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            if (error) *error = QStringLiteral("Cannot materialize embedded archive stimulus to %1").arg(outPath);
            return false;
        }
        if (f.write(reproductionArchive_.embeddedStimulus) != reproductionArchive_.embeddedStimulus.size())
        {
            if (error) *error = QStringLiteral("Incomplete write while materializing archived stimulus.");
            return false;
        }
        f.close();
        *path = outPath;
        return true;
    }

    const QString local = defaultStimulusPath();
    if (!QFileInfo::exists(local))
    {
        if (error) *error = QStringLiteral("Archive has no embedded stimulus and the local Sugar file is missing: %1").arg(local);
        return false;
    }
    *path = local;
    return true;
}

QString MainWindow::reproductionReportFilePath() const
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString archiveBase = QFileInfo(reproductionArchive_.sourcePath).completeBaseName().isEmpty()
        ? QStringLiteral("archive") : QFileInfo(reproductionArchive_.sourcePath).completeBaseName();
    return QDir(DrosofilaLiveRuntime::reportsDir()).filePath(
        QStringLiteral("reproduce_%1_%2.html").arg(archiveBase, stamp));
}

void MainWindow::startReproduction()
{
    if (reproductionActive_ || !experiment_ || !experiment_->isConnectomeLoaded() ||
        experiment_->isRunning() || circuitExplorerBusy_ || (statistics_ && statistics_->isRunning()) ||
        (script_ && script_->isBusy()) || (discovery_ && discovery_->isRunning()))
        return;

    QString reason;
    if (!reproductionArchiveReady(&reason))
    {
        ui->archiveReproduceStatusLB->setText(reason);
        return;
    }
    QString stimulusError;
    if (!prepareReproductionStimulus(&reproductionStimulusPath_, &stimulusError))
    {
        ui->archiveReproduceStatusLB->setText(QStringLiteral("Cannot reproduce: %1").arg(stimulusError));
        return;
    }

    runReproductionPreflight();
    reproductionAnalysis_.startedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    reproductionAnalysis_.finishedUtc.clear();
    reproductionAnalysis_.metrics.clear();
    reproductionAnalysis_.reproducedExplorer = DrosofilaLive::CircuitExploreResult();
    reproductionAnalysis_.reproducedDiscovery = DrosofilaLive::CausalDiscoveryResult();
    reproductionReportPath_.clear();
    reproductionActive_ = true;
    reproductionStopRequested_ = false;
    reproductionPhase_ = ReproductionExplorer;
    causalDiscoveryGuard_ = true;
    stopPlayback(true);
    setExperimentControlsEnabled(false);
    ui->archiveReproducePB->setEnabled(false);
    ui->archivePreflightPB->setEnabled(false);
    ui->archiveReproduceStopPB->setEnabled(true);
    ui->archiveReproduceReportPB->setEnabled(false);
    ui->archiveReproduceStatusLB->setText(QStringLiteral("REPRODUCE 1/4 - rebuilding Circuit Explorer from archived parameters..."));
    beginActivity(QStringLiteral("REPRODUCE"), QStringLiteral("Rebuilding archived scientific protocol"));
    appendGlobalLogSection(QStringLiteral("REPRODUCE"),
        QStringLiteral("Starting exact protocol reconstruction from %1\nEnvironment: %2")
            .arg(reproductionArchive_.sourcePath)
            .arg(reproductionAnalysis_.environmentExact ? QStringLiteral("EXACT") : QStringLiteral("NON-EXACT")));

    circuitExplorerBusy_ = true;
    emit circuitExploreRequested(reproductionArchive_.discovery.provenance.explorerParameters);
}

void MainWindow::stopReproduction()
{
    if (!reproductionActive_) return;
    reproductionStopRequested_ = true;
    ui->archiveReproduceStopPB->setEnabled(false);
    ui->archiveReproduceStatusLB->setText(QStringLiteral("REPRODUCE stop requested; completing the current atomic operation..."));
    if (reproductionPhase_ == ReproductionScreen || reproductionPhase_ == ReproductionValidation || reproductionPhase_ == ReproductionEpistasis)
    {
        if (discovery_ && discovery_->isRunning()) discovery_->requestStop();
    }
}

void MainWindow::handleReproductionExplorerFinished(const DrosofilaLive::CircuitExploreResult& result)
{
    circuitExplorerBusy_ = false;
    if (!reproductionActive_ || reproductionPhase_ != ReproductionExplorer) return;
    reproductionAnalysis_.reproducedExplorer = result;
    if (!result.valid)
    {
        finishReproduction(false, QStringLiteral("Explorer reproduction failed: %1").arg(result.error));
        return;
    }

    const QVector<DrosofilaLive::ReproductionMetric> explorerMetrics =
        DrosofilaLive::ReproductionAnalyzer::compareExplorer(reproductionArchive_.explorer, result);
    for (int i = 0; i < explorerMetrics.size(); ++i) reproductionAnalysis_.metrics.append(explorerMetrics.at(i));
    populateReproductionMetrics(reproductionAnalysis_.metrics);

    if (reproductionStopRequested_)
    {
        finishReproduction(false, QStringLiteral("Stopped by user after Explorer."));
        return;
    }

    const QVector<DrosofilaLive::CausalDiscoveryCandidate> candidates = reproductionCandidates(result);
    if (candidates.isEmpty())
    {
        finishReproduction(false, QStringLiteral("Reproduced Explorer returned no candidates for the archived selection protocol."));
        return;
    }

    DrosofilaLive::ExperimentParameters parameters = reproductionArchive_.discovery.provenance.screenParameters;
    parameters.stimulusPath = reproductionStimulusPath_;
    parameters.ablatedRootIds.clear();
    const std::uint64_t targetId = reproductionArchive_.discovery.targetRootId;
    if (!containsRoot(parameters.watchedRootIds, targetId)) parameters.watchedRootIds.append(targetId);
    const int replicates = std::max(1, reproductionArchive_.discovery.screenReplicatesRequested);

    reproductionPhase_ = ReproductionScreen;
    ui->archiveReproduceStatusLB->setText(QStringLiteral("REPRODUCE 2/4 - screening %1 candidate(s), %2 replicate(s) each...")
        .arg(candidates.size()).arg(replicates));
    if (!discovery_->runScreen(parameters, candidates, replicates))
    {
        finishReproduction(false, QStringLiteral("Unable to start reproduced screening."));
        return;
    }
    DrosofilaLive::CausalDiscoveryProvenance provenance = reproductionArchive_.discovery.provenance;
    provenance.origin = QStringLiteral("REPRODUCE");
    provenance.startedUtc = reproductionAnalysis_.startedUtc;
    provenance.updatedUtc = reproductionAnalysis_.startedUtc;
    discovery_->setProvenance(provenance);
}

void MainWindow::handleReproductionDiscoveryFinished()
{
    if (!reproductionActive_ || !discovery_) return;
    const DrosofilaLive::CausalDiscoveryResult current = discovery_->result();
    if (current.stopped || reproductionStopRequested_)
    {
        finishReproduction(false, QStringLiteral("Reproduction stopped by user."));
        return;
    }
    if (!current.error.isEmpty() && !current.valid)
    {
        finishReproduction(false, current.error);
        return;
    }

    const DrosofilaLive::CausalDiscoveryProvenance& p = reproductionArchive_.discovery.provenance;
    if (reproductionPhase_ == ReproductionScreen)
    {
        if (!reproductionArchive_.discovery.validationRootIds.isEmpty() && p.validationParametersValid)
        {
            DrosofilaLive::ExperimentParameters parameters = p.validationParameters;
            parameters.stimulusPath = reproductionStimulusPath_;
            parameters.ablatedRootIds.clear();
            reproductionPhase_ = ReproductionValidation;
            ui->archiveReproduceStatusLB->setText(QStringLiteral("REPRODUCE 3/4 - validating archived hit set: %1 candidate(s) x %2 replicate(s)...")
                .arg(reproductionArchive_.discovery.validationRootIds.size())
                .arg(reproductionArchive_.discovery.validationReplicatesRequested));
            if (!discovery_->runValidation(parameters, reproductionArchive_.discovery.validationRootIds,
                                           std::max(1, reproductionArchive_.discovery.validationReplicatesRequested)))
            {
                finishReproduction(false, QStringLiteral("Unable to start reproduced validation; archived validation roots may not exist in the reproduced screen."));
            }
            return;
        }
        finishReproduction(true);
        return;
    }

    if (reproductionPhase_ == ReproductionValidation)
    {
        if (reproductionArchive_.discovery.epistasisRootIds.size() >= 2 && p.epistasisParametersValid)
        {
            DrosofilaLive::ExperimentParameters parameters = p.epistasisParameters;
            parameters.stimulusPath = reproductionStimulusPath_;
            parameters.ablatedRootIds.clear();
            reproductionPhase_ = ReproductionEpistasis;
            ui->archiveReproduceStatusLB->setText(QStringLiteral("REPRODUCE 4/4 - epistasis on %1 archived root(s), %2 replicate(s)/pair...")
                .arg(reproductionArchive_.discovery.epistasisRootIds.size())
                .arg(reproductionArchive_.discovery.epistasisReplicatesRequested));
            if (!discovery_->runEpistasis(parameters, reproductionArchive_.discovery.epistasisRootIds,
                                          std::max(1, reproductionArchive_.discovery.epistasisReplicatesRequested)))
            {
                finishReproduction(false, QStringLiteral("Unable to start reproduced epistasis."));
            }
            return;
        }
        finishReproduction(true);
        return;
    }

    if (reproductionPhase_ == ReproductionEpistasis)
    {
        finishReproduction(true);
        return;
    }
}

void MainWindow::finishReproduction(bool completed, const QString& error)
{
    if (!reproductionActive_) return;

    reproductionAnalysis_.finishedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    reproductionAnalysis_.error = error;
    if (completed && discovery_)
    {
        reproductionAnalysis_.reproducedDiscovery = discovery_->result();
        const QVector<DrosofilaLive::ReproductionMetric> discoveryMetrics =
            DrosofilaLive::ReproductionAnalyzer::compareDiscovery(reproductionArchive_.discovery,
                                                                   reproductionAnalysis_.reproducedDiscovery);
        for (int i = 0; i < discoveryMetrics.size(); ++i) reproductionAnalysis_.metrics.append(discoveryMetrics.at(i));
    }

    bool numericPass = completed && !reproductionAnalysis_.metrics.isEmpty();
    for (int i = 0; i < reproductionAnalysis_.metrics.size(); ++i)
        if (!reproductionAnalysis_.metrics.at(i).match) { numericPass = false; break; }
    reproductionAnalysis_.numericalPass = numericPass;
    reproductionAnalysis_.valid = completed;

    reproductionReportPath_ = reproductionReportFilePath();
    QString reportError;
    if (!DrosofilaLive::ReproductionAnalyzer::writeHtmlReport(reproductionReportPath_, reproductionArchive_, reproductionAnalysis_, &reportError))
    {
        appendGlobalLog(QStringLiteral("[REPRODUCE] report error: %1").arg(reportError));
        reproductionReportPath_.clear();
    }

    // Restore the immutable archived scientific state after using the shared
    // Discovery controller as a reproduction engine.
    if (discovery_)
    {
        discovery_->restoreResult(reproductionArchive_.discovery);
        populateCausalDiscoveryResult(reproductionArchive_.discovery);
        populateCausalDiscoveryEpistasisResult(reproductionArchive_.discovery);
    }
    lastCircuitExplore_ = reproductionArchive_.explorer;
    if (lastCircuitExplore_.valid)
    {
        populateCircuitExplorerResult(lastCircuitExplore_);
        if (circuitGraph_) circuitGraph_->setResult(lastCircuitExplore_);
    }
    if (script_) script_->restoreScientificState(lastCircuitExplore_, reproductionArchive_.discovery);

    reproductionActive_ = false;
    reproductionStopRequested_ = false;
    reproductionPhase_ = ReproductionIdle;
    causalDiscoveryGuard_ = false;
    circuitExplorerBusy_ = false;
    setExperimentControlsEnabled(true);
    ui->archiveReproduceStopPB->setEnabled(false);
    ui->archivePreflightPB->setEnabled(true);
    ui->archiveReproduceReportPB->setEnabled(!reproductionReportPath_.isEmpty());
    populateReproductionMetrics(reproductionAnalysis_.metrics);

    QString status;
    if (!completed)
        status = QStringLiteral("REPRODUCE INCOMPLETE - %1").arg(error.isEmpty() ? QStringLiteral("stopped/failed") : error);
    else if (reproductionAnalysis_.numericalPass && reproductionAnalysis_.environmentExact)
        status = QStringLiteral("REPRODUCIBILITY: PASS - exact environment and all archived metrics reproduced.");
    else if (reproductionAnalysis_.numericalPass)
        status = QStringLiteral("REPRODUCIBILITY: NUMERIC PASS / ENVIRONMENT DIFFERENT - values match, but preflight is not exact.");
    else
        status = QStringLiteral("REPRODUCIBILITY: FAIL - %1 numeric metric(s) differ beyond tolerance.").arg(reproductionAnalysis_.mismatchCount());
    if (!reproductionReportPath_.isEmpty()) status += QStringLiteral(" Report: %1").arg(reproductionReportPath_);
    ui->archiveReproduceStatusLB->setText(status);
    appendGlobalLogSection(QStringLiteral("REPRODUCE result"), status);
    finishActivity(status, completed && reproductionAnalysis_.numericalPass);
    updateArchiveUi();
}

void MainWindow::openReproductionReport()
{
    if (reproductionReportPath_.isEmpty() || !QFileInfo::exists(reproductionReportPath_)) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(reproductionReportPath_));
}

void MainWindow::openArchive()
{
    if ((experiment_ && experiment_->isRunning()) || (script_ && script_->isBusy()) ||
        (statistics_ && statistics_->isRunning()) || (discovery_ && discovery_->isRunning()) || circuitExplorerBusy_) return;
    const QString path = QFileDialog::getOpenFileName(this,
        QStringLiteral("Open drosofilaLive experiment archive"), DrosofilaLiveRuntime::archivesDir(),
        QStringLiteral("drosofilaLive archives (*.dro);;ZIP files (*.zip);;All files (*.*)"));
    if (!path.isEmpty()) loadArchiveFromPath(path, false);
}

void MainWindow::saveArchive()
{
    if ((experiment_ && experiment_->isRunning()) || (script_ && script_->isBusy()) ||
        (statistics_ && statistics_->isRunning()) || (discovery_ && discovery_->isRunning()) || circuitExplorerBusy_) return;
    const QString suggested = archiveSourcePath_.isEmpty()
        ? QDir(DrosofilaLiveRuntime::archivesDir()).filePath(QStringLiteral("experiment.dro")) : archiveSourcePath_;
    const QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("Save drosofilaLive experiment archive"), suggested,
        QStringLiteral("drosofilaLive archives (*.dro)"));
    if (!path.isEmpty()) saveArchiveToPath(path, false);
}

void MainWindow::exportArchiveCsv()
{
    if ((experiment_ && experiment_->isRunning()) || (script_ && script_->isBusy()) ||
        (statistics_ && statistics_->isRunning()) || (discovery_ && discovery_->isRunning()) || circuitExplorerBusy_) return;
    const QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("Export experiment CSV"), QDir(DrosofilaLiveRuntime::resultsDir()).filePath(QStringLiteral("experiment.csv")),
        QStringLiteral("CSV files (*.csv)"));
    if (!path.isEmpty()) exportArchiveCsvToPath(path, false);
}

void MainWindow::playArchive()
{
    selectTimelinePlot();
    if (playbackTimer_->isActive())
    {
        stopPlayback(false);
        ui->archiveStatusLB->setText(QStringLiteral("Replay stopped."));
        return;
    }
    if (!rendererReady() || !circuitLoaded_)
    {
        ui->archiveStatusLB->setText(QStringLiteral("Load the validated 7-neuron circuit before 3D replay."));
        return;
    }

    archiveReplaySingle_ = DrosofilaLive::ExperimentResult();
    const int mode = ui->archiveReplayModeCB->currentIndex();
    if (mode == 0)
    {
        // A Discovery-only .dro has no legacy lastExperiment_.  For "Single
        // Experiment" use the best stored reference run, or WT if only
        // epistasis survived. This keeps the mode useful for every replayable v3.
        if (lastExperiment_.valid && !lastExperiment_.frames.isEmpty())
        {
            archiveReplaySingle_ = lastExperiment_;
        }
        else
        {
            DrosofilaLive::ComparisonResult comparison;
            DrosofilaLive::EpistasisResult epistasis;
            if (bestDiscoveryReplayComparison(&comparison))
                archiveReplaySingle_ = comparison.reference;
            else if (bestDiscoveryReplayEpistasis(&epistasis))
                archiveReplaySingle_ = epistasis.wildType;
        }

        if (!archiveReplaySingle_.valid || archiveReplaySingle_.frames.isEmpty())
        {
            ui->archiveStatusLB->setText(QStringLiteral("No stored experiment frames available for replay."));
            return;
        }

        playbackMode_ = PlaybackSingle;
        playbackFrame_ = 0;
        if (timeline_)
        {
            timeline_->setResult(archiveReplaySingle_);
            selectReplayNeuron(archiveReplaySingle_);
            timeline_->setCursorTime(0.0);
        }
        resetExperimentVisualState();
        applyAblationVisualState(archiveReplaySingle_.ablatedRootIds);
        ui->timelineInfoLB->setText(QStringLiteral("%1 | stored single run | hover=readout | wheel/drag=zoom | middle=pan")
            .arg(ui->neuronCB->currentText()));
    }
    else if (mode >= 1 && mode <= 3)
    {
        const bool legacyAvailable = lastComparison_.valid &&
                                     !lastComparison_.reference.frames.isEmpty() &&
                                     !lastComparison_.perturbed.frames.isEmpty();
        if (legacyAvailable)
            archiveReplayComparison_ = lastComparison_;
        else if (!bestDiscoveryReplayComparison(&archiveReplayComparison_))
        {
            ui->archiveStatusLB->setText(QStringLiteral("No comparison frames available for replay."));
            return;
        }

        playbackMode_ = mode == 1 ? PlaybackComparisonReference :
                        mode == 2 ? PlaybackComparisonPerturbed : PlaybackComparisonDifference;
        playbackFrame_ = 0;
        if (timeline_)
        {
            timeline_->setComparison(archiveReplayComparison_);
            selectReplayNeuron(archiveReplayComparison_.reference);
            timeline_->setCursorTime(0.0);
        }
        resetExperimentVisualState();
        if (playbackMode_ != PlaybackComparisonReference)
            applyAblationVisualState(archiveReplayComparison_.perturbedRootIds);
        ui->timelineInfoLB->setText(QStringLiteral("%1 | stored causal comparison | hover=readout | wheel/drag=zoom | middle=pan")
            .arg(ui->neuronCB->currentText()));
    }
    else
    {
        const bool legacyAvailable = lastEpistasis_.valid &&
                                     !lastEpistasis_.wildType.frames.isEmpty() &&
                                     !lastEpistasis_.aOff.frames.isEmpty() &&
                                     !lastEpistasis_.bOff.frames.isEmpty() &&
                                     !lastEpistasis_.doubleOff.frames.isEmpty();
        if (legacyAvailable)
            archiveReplayEpistasis_ = lastEpistasis_;
        else if (!bestDiscoveryReplayEpistasis(&archiveReplayEpistasis_))
        {
            ui->archiveStatusLB->setText(QStringLiteral("No epistasis result available for replay."));
            return;
        }

        const DrosofilaLive::ExperimentResult* r = 0;
        if (mode == 4) { playbackMode_ = PlaybackEpistasisWildType; r = &archiveReplayEpistasis_.wildType; }
        else if (mode == 5) { playbackMode_ = PlaybackEpistasisAOff; r = &archiveReplayEpistasis_.aOff; }
        else if (mode == 6) { playbackMode_ = PlaybackEpistasisBOff; r = &archiveReplayEpistasis_.bOff; }
        else { playbackMode_ = PlaybackEpistasisDoubleOff; r = &archiveReplayEpistasis_.doubleOff; }

        if (!r || !r->valid || r->frames.isEmpty())
        {
            ui->archiveStatusLB->setText(QStringLiteral("Selected epistasis condition has no stored frames."));
            return;
        }
        playbackFrame_ = 0;
        if (timeline_)
        {
            timeline_->setResult(*r);
            selectReplayNeuron(*r);
            timeline_->setCursorTime(0.0);
        }
        resetExperimentVisualState();
        applyAblationVisualState(r->ablatedRootIds);
        ui->timelineInfoLB->setText(QStringLiteral("%1 | stored epistasis | hover=readout | wheel/drag=zoom | middle=pan")
            .arg(ui->neuronCB->currentText()));
    }

    ui->archivePlayPB->setText(QStringLiteral("Stop replay"));
    ui->archiveStatusLB->setText(QStringLiteral("Replaying stored temporal frames; LIF engine is idle."));
    playbackTimer_->start();
}

void MainWindow::scriptArchiveSave(const QString& path) { saveArchiveToPath(path, true); }
void MainWindow::scriptArchiveLoad(const QString& path) { loadArchiveFromPath(path, true); }
void MainWindow::scriptArchiveReplay() { playArchive(); }
void MainWindow::scriptArchiveExportCsv(const QString& path) { exportArchiveCsvToPath(path, true); }

QString MainWindow::comparisonSummaryText(const DrosofilaLive::ComparisonResult& result) const
{
    if (!result.valid)
        return result.error.isEmpty() ? QStringLiteral("No valid comparison result.") : result.error;

    QStringList lines;
    lines << QStringLiteral("CONTROLLED CAUSAL COMPARISON")
          << QStringLiteral("Perturbation: %1").arg(rootListText(result.perturbedRootIds))
          << QStringLiteral("Common seed: %1 | trials %2 | duration %3 ms | Sugar %4 @ %5 Hz")
                .arg(result.reference.randomSeed)
                .arg(result.reference.trialsCompleted)
                .arg(result.reference.durationMs, 0, 'f', 1)
                .arg(result.reference.stimulusName)
                .arg(result.reference.sugarRateHz, 0, 'f', 1)
          << QStringLiteral("")
          << QStringLiteral("TARGET/MN9")
          << QStringLiteral("Reference : %1 Hz").arg(result.reference.targetMeanHz, 0, 'f', 2)
          << QStringLiteral("Perturbed : %1 Hz").arg(result.perturbed.targetMeanHz, 0, 'f', 2)
          << QStringLiteral("Delta     : %1 Hz").arg(result.targetDeltaHz, 0, 'f', 2)
          << QStringLiteral("Ratio     : %1 x").arg(result.targetRatio, 0, 'f', 3);

    if (result.sustainedOnsetMs >= 0.0)
        lines << QStringLiteral("Sustained divergence onset: %1 ms").arg(result.sustainedOnsetMs, 0, 'f', 1);
    else
        lines << QStringLiteral("Sustained divergence onset: not detected");

    lines << QStringLiteral("Rule: |Delta TARGET rate| >= %1 Hz for %2 consecutive bins")
                .arg(result.sustainedThresholdHz, 0, 'f', 1)
                .arg(result.sustainedBins)
          << QStringLiteral("")
          << QStringLiteral("Watched-neuron mean rates:");

    for (int i = 0; i < result.reference.neurons.size(); ++i)
    {
        const DrosofilaLive::ExperimentNeuronSummary& rn = result.reference.neurons.at(i);
        double perturbedHz = 0.0;
        bool found = false;
        for (int j = 0; j < result.perturbed.neurons.size(); ++j)
        {
            if (result.perturbed.neurons.at(j).rootId == rn.rootId)
            {
                perturbedHz = result.perturbed.neurons.at(j).meanHz;
                found = true;
                break;
            }
        }
        if (!found)
            continue;
        lines << QStringLiteral("%1  ref %2 Hz | pert %3 Hz | delta %4 Hz")
                    .arg(rn.type, -8)
                    .arg(rn.meanHz, 7, 'f', 2)
                    .arg(perturbedHz, 7, 'f', 2)
                    .arg(perturbedHz - rn.meanHz, 7, 'f', 2);
    }

    return lines.join(QLatin1Char('\n'));
}

void MainWindow::comparisonFinished()
{
    if (causalDiscoveryGuard_) return;
    selectTimelinePlot();
    archiveSourcePath_.clear();
    archiveCreatedUtc_.clear();
    lastExperiment_ = DrosofilaLive::ExperimentResult();
    lastComparison_ = experiment_->comparisonResult();
    if (timeline_) timeline_->setComparison(lastComparison_);
    updateTimelineSelection();
    setExperimentControlsEnabled(true);

    if (!lastComparison_.valid)
    {
        if (activityName_ == QStringLiteral("cvBasic script"))
            updateActivityDetail(0, QStringLiteral("DRO.COMPARE failed: %1").arg(lastComparison_.error));
        else
            finishActivity(QStringLiteral("Comparison failed: %1").arg(lastComparison_.error), false);
        appendGlobalLogSection(QStringLiteral("Compare"), comparisonSummaryText(lastComparison_));
        ui->statusbar->showMessage(QStringLiteral("Causal comparison failed: %1").arg(lastComparison_.error), 8000);
        return;
    }

    if (activityName_ == QStringLiteral("cvBasic script"))
        updateActivityDetail(100, QStringLiteral("DRO.COMPARE complete"));
    else
        finishActivity(QStringLiteral("Controlled comparison complete."), true);
    appendGlobalLogSection(QStringLiteral("Compare"), comparisonSummaryText(lastComparison_));
    ui->playComparisonPB->setEnabled(circuitLoaded_);
    resetExperimentVisualState();
    ui->statusbar->showMessage(QStringLiteral("Compare complete: TARGET delta %1 Hz")
                               .arg(lastComparison_.targetDeltaHz, 0, 'f', 2), 8000);
    updateArchiveUi();
    DrosofilaLiveDiagnostics::log(QString("Comparison complete: ref=%1 pert=%2 delta=%3 onset=%4")
        .arg(lastComparison_.reference.targetMeanHz)
        .arg(lastComparison_.perturbed.targetMeanHz)
        .arg(lastComparison_.targetDeltaHz)
        .arg(lastComparison_.sustainedOnsetMs));
}

void MainWindow::playComparison()
{
    selectTimelinePlot();
    if (playbackTimer_->isActive())
    {
        stopPlayback(false);
        return;
    }

    if (!rendererReady() || !circuitLoaded_ || !lastComparison_.valid)
        return;

    const int mode = ui->comparisonPlaybackModeCB->currentIndex();
    playbackMode_ = mode == 0 ? PlaybackComparisonReference :
                    mode == 1 ? PlaybackComparisonPerturbed : PlaybackComparisonDifference;
    playbackFrame_ = 0;
    if (timeline_) timeline_->setCursorTime(0.0);
    resetExperimentVisualState();
    if (playbackMode_ == PlaybackComparisonPerturbed || playbackMode_ == PlaybackComparisonDifference)
        applyAblationVisualState(lastComparison_.perturbedRootIds);
    ui->playComparisonPB->setText(QStringLiteral("Stop comparison"));
    ui->statusbar->showMessage(QStringLiteral("Comparison playback started."));
    playbackTimer_->start();
}

void MainWindow::applyAbsoluteFrame(const DrosofilaLive::ExperimentFrame& frame,
                                    const QVector<std::uint64_t>& ablatedRootIds)
{
    // The old linear /200 Hz mapping made biologically useful 5-30 Hz activity
    // almost indistinguishable from the resting color. A soft exponential
    // compression keeps absolute meaning while making low/moderate rates visible.
    for (int i = 0; i < frame.neurons.size(); ++i)
    {
        const DrosofilaLive::ExperimentFrameNeuron& n = frame.neurons.at(i);
        const double rate = std::max(0.0, n.rateHz);
        const double visual = 1.0 - std::exp(-rate / 25.0);
        NeuronVisualState state;
        state.activity = static_cast<float>(std::max(0.04, std::min(1.0, 0.04 + 0.96 * visual)));
        state.spike = n.spiked;
        state.ablated = containsRoot(ablatedRootIds, n.rootId);
        renderer_->setNeuronState(n.rootId, state);
    }
    applyAblationVisualState(ablatedRootIds);
}

void MainWindow::applyDifferenceFrame(const DrosofilaLive::ExperimentFrame& reference,
                                      const DrosofilaLive::ExperimentFrame& perturbed,
                                      const QVector<std::uint64_t>& perturbedRootIds)
{
    for (std::vector<std::uint64_t>::const_iterator id = neuronIds_.begin(); id != neuronIds_.end(); ++id)
    {
        double rr = 0.0;
        double pr = 0.0;
        bool haveR = false;
        bool haveP = false;
        for (int i = 0; i < reference.neurons.size(); ++i)
            if (reference.neurons.at(i).rootId == *id) { rr = reference.neurons.at(i).rateHz; haveR = true; break; }
        for (int i = 0; i < perturbed.neurons.size(); ++i)
            if (perturbed.neurons.at(i).rootId == *id) { pr = perturbed.neurons.at(i).rateHz; haveP = true; break; }

        NeuronVisualState state;
        state.differenceMode = true;
        state.deltaActivity = static_cast<float>((haveR && haveP) ? std::tanh((pr - rr) / 25.0) : 0.0);
        state.activity = static_cast<float>(std::fabs(state.deltaActivity));
        state.ablated = containsRoot(perturbedRootIds, *id);
        renderer_->setNeuronState(*id, state);
    }
    applyAblationVisualState(perturbedRootIds);
}

void MainWindow::playbackTick()
{
    if (!rendererReady())
        return;

    if (playbackMode_ == PlaybackSingle)
    {
        const bool archiveReplay = ui && ui->archivePlayPB->text() == QStringLiteral("Stop replay");
        const DrosofilaLive::ExperimentResult* result = archiveReplay ? &archiveReplaySingle_ : &lastExperiment_;
        if (!result || !result->valid || playbackFrame_ >= result->frames.size())
        {
            const bool wasArchiveReplay = archiveReplay;
            stopPlayback(false);
            ui->statusbar->showMessage(QStringLiteral("Playback complete."), 4000);
            if (wasArchiveReplay) ui->archiveStatusLB->setText(QStringLiteral("Replay complete; no LIF run was performed."));
            return;
        }

        const DrosofilaLive::ExperimentFrame& frame = result->frames.at(playbackFrame_);
        applyAbsoluteFrame(frame, result->ablatedRootIds);
        renderer_->setTime(frame.endMs);
        renderer_->refresh();
        if (timeline_) timeline_->setCursorTime(frame.endMs);
        ui->timelineInfoLB->setText(QStringLiteral("%1 | cursor %2 ms | firing-rate bin %3 ms | hover readout")
            .arg(ui->neuronCB->currentText())
            .arg(frame.endMs, 0, 'f', 1)
            .arg(result->temporalBinMs, 0, 'f', 1));
        if (archiveReplay)
            ui->archiveStatusLB->setText(QStringLiteral("Stored replay: %1-%2 ms | LIF idle")
                .arg(frame.startMs, 0, 'f', 1).arg(frame.endMs, 0, 'f', 1));
        ++playbackFrame_;
        return;
    }

    if (playbackMode_ == PlaybackEpistasisWildType ||
        playbackMode_ == PlaybackEpistasisAOff ||
        playbackMode_ == PlaybackEpistasisBOff ||
        playbackMode_ == PlaybackEpistasisDoubleOff)
    {
        const bool archiveReplay = ui && ui->archivePlayPB->text() == QStringLiteral("Stop replay");
        const DrosofilaLive::EpistasisResult& epistasis = archiveReplay ? archiveReplayEpistasis_ : lastEpistasis_;
        const DrosofilaLive::ExperimentResult* result = 0;
        QString condition;
        if (playbackMode_ == PlaybackEpistasisWildType) { result = &epistasis.wildType; condition = QStringLiteral("WT"); }
        else if (playbackMode_ == PlaybackEpistasisAOff) { result = &epistasis.aOff; condition = QStringLiteral("A OFF"); }
        else if (playbackMode_ == PlaybackEpistasisBOff) { result = &epistasis.bOff; condition = QStringLiteral("B OFF"); }
        else { result = &epistasis.doubleOff; condition = QStringLiteral("A+B OFF"); }

        if (!result || !result->valid || playbackFrame_ >= result->frames.size())
        {
            const bool wasArchiveReplay = archiveReplay;
            stopPlayback(false);
            if (wasArchiveReplay) ui->archiveStatusLB->setText(QStringLiteral("Epistasis replay complete; no LIF run was performed."));
            return;
        }

        const DrosofilaLive::ExperimentFrame& frame = result->frames.at(playbackFrame_);
        applyAbsoluteFrame(frame, result->ablatedRootIds);
        renderer_->setTime(frame.endMs);
        renderer_->refresh();
        if (timeline_) timeline_->setCursorTime(frame.endMs);
        ui->timelineInfoLB->setText(QStringLiteral("%1 | epistasis %2 x %3 / %4 | cursor %5 ms")
            .arg(ui->neuronCB->currentText())
            .arg(epistasis.typeA)
            .arg(epistasis.typeB)
            .arg(condition)
            .arg(frame.endMs, 0, 'f', 1));
        ui->archiveStatusLB->setText(QStringLiteral("Stored epistasis %1 replay: %2-%3 ms | LIF idle")
            .arg(condition).arg(frame.startMs, 0, 'f', 1).arg(frame.endMs, 0, 'f', 1));
        ++playbackFrame_;
        return;
    }

    const bool archiveReplay = ui && ui->archivePlayPB->text() == QStringLiteral("Stop replay");
    const DrosofilaLive::ComparisonResult& comparison = archiveReplay ? archiveReplayComparison_ : lastComparison_;
    if (!comparison.valid)
    {
        stopPlayback(false);
        return;
    }

    const int commonFrames = std::min(comparison.reference.frames.size(),
                                      comparison.perturbed.frames.size());
    if (playbackFrame_ >= commonFrames)
    {
        const bool wasArchiveReplay = archiveReplay;
        stopPlayback(false);
        ui->statusbar->showMessage(QStringLiteral("Comparison playback complete."), 4000);
        if (wasArchiveReplay) ui->archiveStatusLB->setText(QStringLiteral("Replay complete; no LIF run was performed."));
        return;
    }

    const DrosofilaLive::ExperimentFrame& rf = comparison.reference.frames.at(playbackFrame_);
    const DrosofilaLive::ExperimentFrame& pf = comparison.perturbed.frames.at(playbackFrame_);
    if (playbackMode_ == PlaybackComparisonReference)
        applyAbsoluteFrame(rf, comparison.reference.ablatedRootIds);
    else if (playbackMode_ == PlaybackComparisonPerturbed)
        applyAbsoluteFrame(pf, comparison.perturbed.ablatedRootIds);
    else
        applyDifferenceFrame(rf, pf, comparison.perturbedRootIds);

    renderer_->setTime(rf.endMs);
    renderer_->refresh();
    if (timeline_) timeline_->setCursorTime(rf.endMs);

    const QString mode = playbackMode_ == PlaybackComparisonReference ? QStringLiteral("Reference") :
                         playbackMode_ == PlaybackComparisonPerturbed ? QStringLiteral("Perturbed") :
                         QStringLiteral("Difference");
    if (ui->archivePlayPB->text() == QStringLiteral("Stop replay"))
        ui->archiveStatusLB->setText(QStringLiteral("Stored %1 replay: %2-%3 ms | LIF idle")
            .arg(mode).arg(rf.startMs, 0, 'f', 1).arg(rf.endMs, 0, 'f', 1));
    ui->timelineInfoLB->setText(QStringLiteral("%1 | causal comparison cursor %2 ms")
        .arg(ui->neuronCB->currentText()).arg(rf.endMs, 0, 'f', 1));
    ++playbackFrame_;
}

void MainWindow::stopPlayback(bool resetVisualState)
{
    if (playbackTimer_)
        playbackTimer_->stop();
    playbackFrame_ = 0;
    if (ui)
    {
        ui->playSimulationPB->setText(QStringLiteral("Play result"));
        ui->playComparisonPB->setText(QStringLiteral("Play comparison"));
        ui->archivePlayPB->setText(QStringLiteral("Replay"));
    }
    if (resetVisualState)
    {
        if (timeline_) timeline_->setCursorTime(0.0);
        updateTimelineSelection();
        resetExperimentVisualState();
        applyAblationVisualState(ablatedRootIdsFromUi());
    }
}

void MainWindow::resetExperimentVisualState()
{
    if (!rendererReady())
        return;

    for (std::vector<std::uint64_t>::const_iterator it = neuronIds_.begin(); it != neuronIds_.end(); ++it)
        renderer_->setNeuronState(*it, NeuronVisualState());
    renderer_->setTime(0.0);
    renderer_->refresh();
}

void MainWindow::applyAblationVisualState(const QVector<std::uint64_t>& ablatedRootIds)
{
    if (!rendererReady())
        return;

    for (int i = 0; i < ablatedRootIds.size(); ++i)
    {
        NeuronVisualState state;
        state.activity = 0.0f;
        state.spike = false;
        state.ablated = true;
        state.selected = false;
        renderer_->setNeuronState(ablatedRootIds.at(i), state);
    }
    renderer_->refresh();
}


void MainWindow::openScript()
{
    if (script_ && script_->isBusy()) return;
    const QString path = QFileDialog::getOpenFileName(this,
        QStringLiteral("Open cvBasic script"), DrosofilaLiveRuntime::scriptsDir(),
        QStringLiteral("cvBasic scripts (*.bas *.txt);;All files (*.*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setIdleActivityMessage(QStringLiteral("Cannot open script %1").arg(path));
        return;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    ui->scriptEditorTE->setPlainText(stream.readAll());
    setIdleActivityMessage(QStringLiteral("Loaded script %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::saveScript()
{
    const QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("Save cvBasic script"), QDir(DrosofilaLiveRuntime::scriptsDir()).filePath(QStringLiteral("drosofila_experiment.bas")),
        QStringLiteral("cvBasic scripts (*.bas);;Text files (*.txt);;All files (*.*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        setIdleActivityMessage(QStringLiteral("Cannot save script %1").arg(path));
        return;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << ui->scriptEditorTE->toPlainText();
    setIdleActivityMessage(QStringLiteral("Saved script %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::checkScriptSyntax()
{
    if (!script_ || script_->isBusy()) return;
    QString error;
    int line = -1;
    if (script_->checkSyntax(ui->scriptEditorTE->toPlainText(), &error, &line))
    {
        setIdleActivityMessage(QStringLiteral("cvBasic syntax OK."));
        appendGlobalLog(QStringLiteral("[cvBasic] syntax OK"));
    }
    else
    {
        const QString where = line >= 0 ? QStringLiteral(" line %1").arg(line) : QString();
        setIdleActivityMessage(QStringLiteral("cvBasic syntax ERROR%1").arg(where));
        appendGlobalLog(QStringLiteral("[cvBasic ERROR%1] %2").arg(where, error));
    }
}

void MainWindow::runScript()
{
    if (!script_ || script_->isBusy() || !experiment_ || experiment_->isRunning() || circuitExplorerBusy_ ||
        (statistics_ && statistics_->isRunning()) || (discovery_ && discovery_->isRunning()))
        return;

    // Archive-only scripts (DRO.LOAD / DRO.SAVE / DRO.EXPORTCSV) do not
    // require a loaded connectome or stimulus. Commands that actually run LIF
    // validate those prerequisites inside ScriptBridge when they execute.
    const DrosofilaLive::ExperimentParameters base = experimentParametersFromUi();

    stopPlayback(true);
    beginActivity(QStringLiteral("cvBasic script"), QStringLiteral("Preparing command queue"));
    appendGlobalLog(QStringLiteral("\n=== SCRIPT RUN ==="));

    QString error;
    int line = -1;
    if (!script_->execute(ui->scriptEditorTE->toPlainText(), base, &error, &line))
    {
        const QString where = line >= 0 ? QStringLiteral(" line %1").arg(line) : QString();
        finishActivity(QStringLiteral("Script start ERROR%1: %2").arg(where, error), false);
        appendGlobalLog(QStringLiteral("[ERROR%1] %2").arg(where, error));
        updateScriptControls();
        return;
    }

    setExperimentControlsEnabled(false);
    updateScriptControls();
    ui->statusbar->showMessage(QStringLiteral("cvBasic script running..."));
}

void MainWindow::stopScript()
{
    if (!script_) return;
    script_->requestStop();
    updateScriptControls();
}

void MainWindow::clearGlobalLog()
{
    ui->globalLogTE->clear();
}

void MainWindow::scriptOutputLine(const QString& text)
{
    appendGlobalLog(text);
}

void MainWindow::scriptStatusChanged(const QString& text)
{
    if (activityName_ == QStringLiteral("cvBasic script"))
        updateActivityDetail(-1, text);
    else
        setIdleActivityMessage(text);
    ui->statusbar->showMessage(text);
    updateScriptControls();
}

void MainWindow::scriptQueueProgress(int completed, int total, const QString& text)
{
    const int percent = total > 0 ? static_cast<int>((100.0 * completed) / total + 0.5) : 0;
    updateActivityOverall(percent,
        text.isEmpty() ? QStringLiteral("Command %1/%2").arg(completed).arg(total) : text);
}

void MainWindow::scriptFinished(bool ok, const QString& message)
{
    finishActivity(message, ok);
    appendGlobalLog(QStringLiteral("[%1] %2")
        .arg(ok ? QStringLiteral("DONE") : QStringLiteral("STOP/ERROR"), message));
    setExperimentControlsEnabled(true);
    updateScriptControls();
    updateArchiveUi();
    ui->statusbar->showMessage(message, 8000);
}

void MainWindow::scriptDiscoveryGuardChanged(bool active)
{
    causalDiscoveryGuard_ = active;
}

void MainWindow::scriptPlaySingle()
{
    if (playbackTimer_->isActive()) stopPlayback(false);
    playExperiment();
}

void MainWindow::scriptPlayComparison(int mode)
{
    if (mode < 0 || mode > 2) return;
    if (playbackTimer_->isActive()) stopPlayback(false);
    ui->comparisonPlaybackModeCB->setCurrentIndex(mode);
    playComparison();
}
