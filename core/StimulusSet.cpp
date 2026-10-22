#include "StimulusSet.h"
#include "Connectome.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

void StimulusSet::clear()
{
    m_path.clear();
    m_name.clear();
    m_source.clear();
    m_rootIds.clear();
    m_indices.clear();
    m_missingRootIds.clear();
}

bool StimulusSet::load(const QString &path,
                       const Connectome &connectome,
                       QString *errorMessage)
{
    clear();

    if (!connectome.isLoaded())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Connectoma non caricato.");
        return false;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Impossibile aprire stimulus set: %1").arg(path);
        return false;
    }

    m_path = QFileInfo(path).absoluteFilePath();
    m_name = QFileInfo(path).completeBaseName();

    QTextStream ts(&f);
    ts.setCodec("UTF-8");

    QSet<quint64> seen;
    int lineNumber = 0;

    while (!ts.atEnd())
    {
        ++lineNumber;
        QString line = ts.readLine().trimmed();

        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        if (line.startsWith(QStringLiteral("name="), Qt::CaseInsensitive))
        {
            m_name = line.mid(line.indexOf(QLatin1Char('=')) + 1).trimmed();
            continue;
        }

        if (line.startsWith(QStringLiteral("source="), Qt::CaseInsensitive))
        {
            m_source = line.mid(line.indexOf(QLatin1Char('=')) + 1).trimmed();
            continue;
        }

        if (line.startsWith(QStringLiteral("root_id="), Qt::CaseInsensitive))
            line = line.mid(line.indexOf(QLatin1Char('=')) + 1).trimmed();

        bool ok = false;
        const quint64 rootId = line.toULongLong(&ok);
        if (!ok)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("Stimulus set non valido, riga %1: %2")
                                    .arg(lineNumber)
                                    .arg(line);
            clear();
            return false;
        }

        if (seen.contains(rootId))
            continue;
        seen.insert(rootId);
        m_rootIds.append(rootId);

        quint32 index = 0;
        if (connectome.indexFromRootId(rootId, &index))
            m_indices.append(index);
        else
            m_missingRootIds.append(rootId);
    }

    if (m_rootIds.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Stimulus set vuoto: %1").arg(path);
        clear();
        return false;
    }

    if (m_indices.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Nessun root_id dello stimulus set esiste nel connectoma caricato.");
        return false;
    }

    if (errorMessage)
    {
        *errorMessage = QStringLiteral("%1: %2/%3 risolti, %4 mancanti")
                            .arg(m_name)
                            .arg(m_indices.size())
                            .arg(m_rootIds.size())
                            .arg(m_missingRootIds.size());
    }

    return true;
}
