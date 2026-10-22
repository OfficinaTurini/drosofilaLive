#pragma once

#include <QString>

namespace DrosofilaLiveRuntime
{
    // Initializes the deterministic runtime layout. By default the work root is
    // <applicationDir>/work. Set DROSOFILA_WORKDIR to override it; relative
    // overrides are resolved from the application directory.
    bool initialize(QString* error = 0);

    QString applicationDir();
    QString workDir();
    QString scriptsDir();
    QString resultsDir();
    QString reportsDir();
    QString archivesDir();
    QString logsDir();

    QString resolveResultPath(const QString& path);
    QString resolveReportPath(const QString& path);
    QString resolveArchivePath(const QString& path);
    QString resolveScriptPath(const QString& path);
}
