#pragma once

#include <QString>

namespace DrosofilaLiveDiagnostics
{
    void install();
    void log(const QString& message);
    QString logPath();
}
