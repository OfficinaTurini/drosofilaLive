#include "RuntimePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QtGlobal>

namespace
{
QString g_applicationDir;
QString g_workDir;
QString g_scriptsDir;
QString g_resultsDir;
QString g_reportsDir;
QString g_archivesDir;
QString g_logsDir;

QString cleanAbsoluteFrom(const QString& base, const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return QString();
    const QFileInfo info(trimmed);
    if (info.isAbsolute()) return QDir::cleanPath(trimmed);
    return QDir::cleanPath(QDir(base).absoluteFilePath(trimmed));
}

QString ensureApplicationDir()
{
    if (!g_applicationDir.isEmpty()) return g_applicationDir;
    const QString app = QCoreApplication::applicationDirPath();
    return app.isEmpty() ? QDir::currentPath() : QDir::cleanPath(app);
}
}

namespace DrosofilaLiveRuntime
{
bool initialize(QString* error)
{
    if (error) error->clear();

    g_applicationDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
    if (g_applicationDir.isEmpty())
        g_applicationDir = QDir::currentPath();

    QString requested = qEnvironmentVariable("DROSOFILA_WORKDIR").trimmed();
    if (requested.isEmpty())
    {
        g_workDir = QDir(g_applicationDir).filePath(QStringLiteral("work"));
    }
    else
    {
        const QFileInfo requestedInfo(requested);
        g_workDir = requestedInfo.isAbsolute()
            ? QDir::cleanPath(requested)
            : QDir::cleanPath(QDir(g_applicationDir).absoluteFilePath(requested));
    }

    g_workDir = QDir::cleanPath(g_workDir);
    g_scriptsDir = QDir(g_workDir).filePath(QStringLiteral("scripts"));
    g_resultsDir = QDir(g_workDir).filePath(QStringLiteral("results"));
    g_reportsDir = QDir(g_workDir).filePath(QStringLiteral("reports"));
    g_archivesDir = QDir(g_workDir).filePath(QStringLiteral("archives"));
    g_logsDir = QDir(g_workDir).filePath(QStringLiteral("logs"));

    const QStringList required = QStringList()
        << g_workDir << g_scriptsDir << g_resultsDir << g_reportsDir << g_archivesDir << g_logsDir;

    for (int i = 0; i < required.size(); ++i)
    {
        if (!QDir().mkpath(required.at(i)))
        {
            if (error) *error = QStringLiteral("Cannot create runtime directory: %1").arg(required.at(i));
            return false;
        }
    }

    if (!QDir::setCurrent(g_workDir))
    {
        if (error) *error = QStringLiteral("Cannot set runtime working directory: %1").arg(g_workDir);
        return false;
    }

    return true;
}

QString applicationDir() { return ensureApplicationDir(); }
QString workDir() { return g_workDir.isEmpty() ? QDir(applicationDir()).filePath(QStringLiteral("work")) : g_workDir; }
QString scriptsDir() { return g_scriptsDir.isEmpty() ? QDir(workDir()).filePath(QStringLiteral("scripts")) : g_scriptsDir; }
QString resultsDir() { return g_resultsDir.isEmpty() ? QDir(workDir()).filePath(QStringLiteral("results")) : g_resultsDir; }
QString reportsDir() { return g_reportsDir.isEmpty() ? QDir(workDir()).filePath(QStringLiteral("reports")) : g_reportsDir; }
QString archivesDir() { return g_archivesDir.isEmpty() ? QDir(workDir()).filePath(QStringLiteral("archives")) : g_archivesDir; }
QString logsDir() { return g_logsDir.isEmpty() ? QDir(workDir()).filePath(QStringLiteral("logs")) : g_logsDir; }

QString resolveResultPath(const QString& path) { return cleanAbsoluteFrom(resultsDir(), path); }
QString resolveReportPath(const QString& path) { return cleanAbsoluteFrom(reportsDir(), path); }
QString resolveArchivePath(const QString& path) { return cleanAbsoluteFrom(archivesDir(), path); }
QString resolveScriptPath(const QString& path) { return cleanAbsoluteFrom(scriptsDir(), path); }
}
