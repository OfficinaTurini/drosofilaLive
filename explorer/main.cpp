#include "mainwindow.h"
#include "diagnostics.h"
#include "core/RuntimePaths.h"

#include <QtWidgets/QApplication>
#include <QDir>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Officina Turini");
    QApplication::setOrganizationDomain("officinaturini.local");
    QApplication::setApplicationName("drosofilaLive");
    QApplication::setApplicationVersion("0.18-r1");

    QString runtimeError;
    const bool runtimeOk = DrosofilaLiveRuntime::initialize(&runtimeError);
    DrosofilaLiveDiagnostics::install();
    DrosofilaLiveDiagnostics::log("main: QApplication constructed");
    DrosofilaLiveDiagnostics::log(QString("main: applicationDir = %1").arg(DrosofilaLiveRuntime::applicationDir()));
    DrosofilaLiveDiagnostics::log(QString("main: workDir = %1").arg(DrosofilaLiveRuntime::workDir()));
    DrosofilaLiveDiagnostics::log(QString("main: cwd = %1").arg(QDir::currentPath()));
    if (!runtimeOk)
        DrosofilaLiveDiagnostics::log(QString("main: RUNTIME PATH ERROR: %1").arg(runtimeError));

    int rc = 0;
    {
        DrosofilaLiveDiagnostics::log("main: before MainWindow construction");
        MainWindow window;
        DrosofilaLiveDiagnostics::log("main: MainWindow constructed");

        window.show();
        DrosofilaLiveDiagnostics::log("main: window.show() returned; entering event loop");

        rc = app.exec();
        DrosofilaLiveDiagnostics::log(QString("main: event loop exited rc=%1; destroying MainWindow").arg(rc));
    }
    DrosofilaLiveDiagnostics::log("main: MainWindow destroyed cleanly; QApplication teardown next");
    return rc;
}
