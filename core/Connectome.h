#pragma once

#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "BinaryFormat.h"

class Connectome
{
public:
    struct Annotation
    {
        QString primaryType;
        QString additionalTypes;
    };

    Connectome();
    ~Connectome();

    Connectome(const Connectome &) = delete;
    Connectome &operator=(const Connectome &) = delete;

    bool load(const QString &dataDirectory, QString *errorMessage = nullptr);
    void close();

    bool isLoaded() const { return m_loaded; }

    quint32 neuronCount() const;
    quint64 edgeCount() const;
    quint64 synapseCount() const;

    quint64 rootId(quint32 index) const;
    qint8 sign(quint32 index) const;

    quint32 fanIn(quint32 index) const;
    quint32 fanOut(quint32 index) const;
    quint32 synapsesIn(quint32 index) const;
    quint32 synapsesOut(quint32 index) const;

    bool indexFromRootId(quint64 rootId, quint32 *index) const;

    quint64 edgeBegin(quint32 sourceIndex) const;
    quint64 edgeEnd(quint32 sourceIndex) const;
    quint32 edgeTarget(quint64 edgeIndex) const;
    qint16 edgeWeight(quint64 edgeIndex) const;

    Annotation annotation(quint32 index) const;

    double mappedMiB() const;

private:
    bool loadNeuronFile(const QString &path, QString *errorMessage);
    bool loadEdgeFile(const QString &path, QString *errorMessage);
    bool loadAnnotations(const QString &path, QString *errorMessage);

    bool checkRange(quint64 offset, quint64 bytes, quint64 fileBytes) const;

private:
    QFile m_neuronFile;
    QFile m_edgeFile;

    uchar *m_neuronMap;
    uchar *m_edgeMap;

    qint64 m_neuronMapSize;
    qint64 m_edgeMapSize;

    const DrosofilaBinary::NeuronFileHeader *m_neuronHeader;
    const DrosofilaBinary::EdgeFileHeader *m_edgeHeader;

    const quint64 *m_rootIds;
    const qint8 *m_signs;
    const quint32 *m_fanIn;
    const quint32 *m_fanOut;
    const quint32 *m_synIn;
    const quint32 *m_synOut;

    const quint32 *m_rowOffset;
    const quint32 *m_targets;
    const qint16 *m_weights;

    QHash<quint64, quint32> m_indexByRoot;
    QVector<Annotation> m_annotations;

    bool m_loaded;
};
