#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

class Connectome;

class StimulusSet
{
public:
    bool load(const QString &path,
              const Connectome &connectome,
              QString *errorMessage = nullptr);

    void clear();

    QString path() const { return m_path; }
    QString name() const { return m_name; }
    QString source() const { return m_source; }

    const QVector<quint64> &rootIds() const { return m_rootIds; }
    const QVector<quint32> &indices() const { return m_indices; }
    const QVector<quint64> &missingRootIds() const { return m_missingRootIds; }

private:
    QString m_path;
    QString m_name;
    QString m_source;
    QVector<quint64> m_rootIds;
    QVector<quint32> m_indices;
    QVector<quint64> m_missingRootIds;
};
