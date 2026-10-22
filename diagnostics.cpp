#include "diagnostics.h"
#include "core/RuntimePaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QtGlobal>
#include <cstdlib>

namespace
{
QMutex g_logMutex;
QString g_logPath;

void writeLine(const QString& text)
{
    QMutexLocker locker(&g_logMutex);

    if (g_logPath.isEmpty())
        g_logPath = QDir(DrosofilaLiveRuntime::logsDir()).filePath("drosofilaLive_startup.log");

    QFile file(g_logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
        << "  " << text << "\n";
    out.flush();
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    const char* prefix = "QT";
    switch (type)
    {
    case QtDebugMsg:    prefix = "QT DEBUG"; break;
    case QtInfoMsg:     prefix = "QT INFO"; break;
    case QtWarningMsg:  prefix = "QT WARNING"; break;
    case QtCriticalMsg: prefix = "QT CRITICAL"; break;
    case QtFatalMsg:    prefix = "QT FATAL"; break;
    }

    writeLine(QString::fromLatin1(prefix) + ": " + msg);

    if (type == QtFatalMsg)
        abort();
}
}

namespace DrosofilaLiveDiagnostics
{
void install()
{
    g_logPath = QDir(DrosofilaLiveRuntime::logsDir()).filePath("drosofilaLive_startup.log");

    QFile file(g_logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&file);
        out << "drosofilaLive startup diagnostics\n";
        out << "================================\n";
        out.flush();
    }

    qInstallMessageHandler(qtMessageHandler);
    writeLine("diagnostic handler installed");
}

void log(const QString& message)
{
    writeLine(message);
}

QString logPath()
{
    return g_logPath;
}
}
