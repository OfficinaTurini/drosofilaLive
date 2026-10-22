#include "Connectome.h"

#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include <cstring>

namespace
{
bool magicEquals(const char *actual, const char *expected)
{
    return std::memcmp(actual, expected, 8) == 0;
}
}

Connectome::Connectome()
    : m_neuronMap(nullptr)
    , m_edgeMap(nullptr)
    , m_neuronMapSize(0)
    , m_edgeMapSize(0)
    , m_neuronHeader(nullptr)
    , m_edgeHeader(nullptr)
    , m_rootIds(nullptr)
    , m_signs(nullptr)
    , m_fanIn(nullptr)
    , m_fanOut(nullptr)
    , m_synIn(nullptr)
    , m_synOut(nullptr)
    , m_rowOffset(nullptr)
    , m_targets(nullptr)
    , m_weights(nullptr)
    , m_loaded(false)
{
}

Connectome::~Connectome()
{
    close();
}

bool Connectome::checkRange(quint64 offset, quint64 bytes, quint64 fileBytes) const
{
    if (offset > fileBytes)
        return false;
    if (bytes > fileBytes - offset)
        return false;
    return true;
}

bool Connectome::load(const QString &dataDirectory, QString *errorMessage)
{
    close();

    const QDir dir(dataDirectory);
    if (!dir.exists())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Directory non esistente: %1").arg(dataDirectory);
        return false;
    }

    if (!loadNeuronFile(dir.filePath(QStringLiteral("neurons.bin")), errorMessage))
    {
        close();
        return false;
    }

    if (!loadEdgeFile(dir.filePath(QStringLiteral("edges.bin")), errorMessage))
    {
        close();
        return false;
    }

    if (m_neuronHeader->neuronCount != m_edgeHeader->neuronCount)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("neurons.bin ed edges.bin hanno N differenti.");
        close();
        return false;
    }

    const quint32 n = m_neuronHeader->neuronCount;
    m_indexByRoot.reserve(static_cast<int>(n * 1.25));
    for (quint32 i = 0; i < n; ++i)
        m_indexByRoot.insert(m_rootIds[i], i);

    m_annotations.resize(static_cast<int>(n));
    QString annotationWarning;
    loadAnnotations(dir.filePath(QStringLiteral("annotations.tsv")), &annotationWarning);

    m_loaded = true;

    if (errorMessage)
        *errorMessage = annotationWarning;

    return true;
}

bool Connectome::loadNeuronFile(const QString &path, QString *errorMessage)
{
    m_neuronFile.setFileName(path);
    if (!m_neuronFile.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Impossibile aprire %1").arg(path);
        return false;
    }

    m_neuronMapSize = m_neuronFile.size();
    if (m_neuronMapSize < static_cast<qint64>(sizeof(DrosofilaBinary::NeuronFileHeader)))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("neurons.bin troppo piccolo.");
        return false;
    }

    m_neuronMap = m_neuronFile.map(0, m_neuronMapSize);
    if (!m_neuronMap)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("QFile::map fallita su neurons.bin.");
        return false;
    }

    m_neuronHeader =
        reinterpret_cast<const DrosofilaBinary::NeuronFileHeader *>(m_neuronMap);

    if (!magicEquals(m_neuronHeader->magic, "DRONEUR\0"))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Magic errata in neurons.bin.");
        return false;
    }

    if (m_neuronHeader->version != DrosofilaBinary::Version ||
        m_neuronHeader->headerSize != sizeof(DrosofilaBinary::NeuronFileHeader))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Versione/formato neurons.bin non supportato.");
        return false;
    }

    const quint64 n = m_neuronHeader->neuronCount;
    const quint64 fileBytes = static_cast<quint64>(m_neuronMapSize);

    if (!checkRange(m_neuronHeader->rootIdOffset, n * sizeof(quint64), fileBytes) ||
        !checkRange(m_neuronHeader->signOffset, n * sizeof(qint8), fileBytes) ||
        !checkRange(m_neuronHeader->fanInOffset, n * sizeof(quint32), fileBytes) ||
        !checkRange(m_neuronHeader->fanOutOffset, n * sizeof(quint32), fileBytes) ||
        !checkRange(m_neuronHeader->synInOffset, n * sizeof(quint32), fileBytes) ||
        !checkRange(m_neuronHeader->synOutOffset, n * sizeof(quint32), fileBytes))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Offset non validi in neurons.bin.");
        return false;
    }

    m_rootIds = reinterpret_cast<const quint64 *>(m_neuronMap + m_neuronHeader->rootIdOffset);
    m_signs = reinterpret_cast<const qint8 *>(m_neuronMap + m_neuronHeader->signOffset);
    m_fanIn = reinterpret_cast<const quint32 *>(m_neuronMap + m_neuronHeader->fanInOffset);
    m_fanOut = reinterpret_cast<const quint32 *>(m_neuronMap + m_neuronHeader->fanOutOffset);
    m_synIn = reinterpret_cast<const quint32 *>(m_neuronMap + m_neuronHeader->synInOffset);
    m_synOut = reinterpret_cast<const quint32 *>(m_neuronMap + m_neuronHeader->synOutOffset);

    return true;
}

bool Connectome::loadEdgeFile(const QString &path, QString *errorMessage)
{
    m_edgeFile.setFileName(path);
    if (!m_edgeFile.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Impossibile aprire %1").arg(path);
        return false;
    }

    m_edgeMapSize = m_edgeFile.size();
    if (m_edgeMapSize < static_cast<qint64>(sizeof(DrosofilaBinary::EdgeFileHeader)))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("edges.bin troppo piccolo.");
        return false;
    }

    m_edgeMap = m_edgeFile.map(0, m_edgeMapSize);
    if (!m_edgeMap)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("QFile::map fallita su edges.bin.");
        return false;
    }

    m_edgeHeader =
        reinterpret_cast<const DrosofilaBinary::EdgeFileHeader *>(m_edgeMap);

    if (!magicEquals(m_edgeHeader->magic, "DROEDGE\0"))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Magic errata in edges.bin.");
        return false;
    }

    if (m_edgeHeader->version != DrosofilaBinary::Version ||
        m_edgeHeader->headerSize != sizeof(DrosofilaBinary::EdgeFileHeader))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Versione/formato edges.bin non supportato.");
        return false;
    }

    const quint64 n = m_edgeHeader->neuronCount;
    const quint64 e = m_edgeHeader->edgeCount;
    const quint64 fileBytes = static_cast<quint64>(m_edgeMapSize);

    if (!checkRange(m_edgeHeader->rowOffsetOffset, (n + 1) * sizeof(quint32), fileBytes) ||
        !checkRange(m_edgeHeader->targetOffset, e * sizeof(quint32), fileBytes) ||
        !checkRange(m_edgeHeader->weightOffset, e * sizeof(qint16), fileBytes))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Offset non validi in edges.bin.");
        return false;
    }

    m_rowOffset =
        reinterpret_cast<const quint32 *>(m_edgeMap + m_edgeHeader->rowOffsetOffset);
    m_targets =
        reinterpret_cast<const quint32 *>(m_edgeMap + m_edgeHeader->targetOffset);
    m_weights =
        reinterpret_cast<const qint16 *>(m_edgeMap + m_edgeHeader->weightOffset);

    if (m_rowOffset[0] != 0 ||
        static_cast<quint64>(m_rowOffset[m_edgeHeader->neuronCount]) != e)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("CSR row_offset non coerente.");
        return false;
    }

    return true;
}

bool Connectome::loadAnnotations(const QString &path, QString *errorMessage)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Nota: annotations.tsv non trovato; il grafo e' comunque utilizzabile.");
        return false;
    }

    QTextStream ts(&f);
    ts.setCodec("UTF-8");

    bool first = true;
    quint32 loaded = 0;

    while (!ts.atEnd())
    {
        const QString line = ts.readLine();

        if (first)
        {
            first = false;
            continue;
        }

        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() < 4)
            continue;

        bool ok = false;
        const uint idx = parts.at(0).toUInt(&ok);
        if (!ok || idx >= static_cast<uint>(m_annotations.size()))
            continue;

        Annotation &a = m_annotations[static_cast<int>(idx)];
        a.primaryType = parts.at(2);
        a.additionalTypes = parts.at(3);
        ++loaded;
    }

    if (errorMessage)
        *errorMessage = QStringLiteral("Annotazioni caricate: %1").arg(loaded);

    return true;
}

void Connectome::close()
{
    m_loaded = false;

    m_indexByRoot.clear();
    m_annotations.clear();

    m_rootIds = nullptr;
    m_signs = nullptr;
    m_fanIn = nullptr;
    m_fanOut = nullptr;
    m_synIn = nullptr;
    m_synOut = nullptr;
    m_rowOffset = nullptr;
    m_targets = nullptr;
    m_weights = nullptr;

    m_neuronHeader = nullptr;
    m_edgeHeader = nullptr;

    if (m_neuronMap)
    {
        m_neuronFile.unmap(m_neuronMap);
        m_neuronMap = nullptr;
    }

    if (m_edgeMap)
    {
        m_edgeFile.unmap(m_edgeMap);
        m_edgeMap = nullptr;
    }

    m_neuronMapSize = 0;
    m_edgeMapSize = 0;

    if (m_neuronFile.isOpen())
        m_neuronFile.close();
    if (m_edgeFile.isOpen())
        m_edgeFile.close();
}

quint32 Connectome::neuronCount() const
{
    return m_loaded ? m_neuronHeader->neuronCount : 0;
}

quint64 Connectome::edgeCount() const
{
    return m_loaded ? m_edgeHeader->edgeCount : 0;
}

quint64 Connectome::synapseCount() const
{
    return m_loaded ? m_edgeHeader->synapseCount : 0;
}

quint64 Connectome::rootId(quint32 index) const
{
    if (!m_loaded || index >= neuronCount())
        return 0;
    return m_rootIds[index];
}

qint8 Connectome::sign(quint32 index) const
{
    if (!m_loaded || index >= neuronCount())
        return 0;
    return m_signs[index];
}

quint32 Connectome::fanIn(quint32 index) const
{
    if (!m_loaded || index >= neuronCount())
        return 0;
    return m_fanIn[index];
}

quint32 Connectome::fanOut(quint32 index) const
{
    if (!m_loaded || index >= neuronCount())
        return 0;
    return m_fanOut[index];
}

quint32 Connectome::synapsesIn(quint32 index) const
{
    if (!m_loaded || index >= neuronCount())
        return 0;
    return m_synIn[index];
}

quint32 Connectome::synapsesOut(quint32 index) const
{
    if (!m_loaded || index >= neuronCount())
        return 0;
    return m_synOut[index];
}

bool Connectome::indexFromRootId(quint64 id, quint32 *index) const
{
    if (!m_loaded)
        return false;

    const auto it = m_indexByRoot.constFind(id);
    if (it == m_indexByRoot.constEnd())
        return false;

    if (index)
        *index = it.value();

    return true;
}

quint64 Connectome::edgeBegin(quint32 sourceIndex) const
{
    if (!m_loaded || sourceIndex >= neuronCount())
        return 0;
    return m_rowOffset[sourceIndex];
}

quint64 Connectome::edgeEnd(quint32 sourceIndex) const
{
    if (!m_loaded || sourceIndex >= neuronCount())
        return 0;
    return m_rowOffset[sourceIndex + 1];
}

quint32 Connectome::edgeTarget(quint64 edgeIndex) const
{
    if (!m_loaded || edgeIndex >= edgeCount())
        return 0;
    return m_targets[edgeIndex];
}

qint16 Connectome::edgeWeight(quint64 edgeIndex) const
{
    if (!m_loaded || edgeIndex >= edgeCount())
        return 0;
    return m_weights[edgeIndex];
}

Connectome::Annotation Connectome::annotation(quint32 index) const
{
    if (!m_loaded || index >= static_cast<quint32>(m_annotations.size()))
        return Annotation();
    return m_annotations.at(static_cast<int>(index));
}

double Connectome::mappedMiB() const
{
    return (static_cast<double>(m_neuronMapSize) +
            static_cast<double>(m_edgeMapSize)) / (1024.0 * 1024.0);
}
