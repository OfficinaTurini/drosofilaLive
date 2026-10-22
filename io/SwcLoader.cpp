#include "SwcLoader.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace
{
struct RawSwcNode
{
    qint64 id;
    qint64 parentId;
    double x;
    double y;
    double z;
};

void parseMetaLine(const QString& line, SwcLoadStats& stats)
{
    const QString prefix = QStringLiteral("# Meta:");
    if (!line.startsWith(prefix))
        return;

    const QByteArray json = line.mid(prefix.size()).trimmed().toUtf8();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return; // Metadata is useful but not required for a valid SWC.

    const QJsonObject obj = doc.object();
    stats.metadataId = obj.value(QStringLiteral("id")).toString();
    stats.units = obj.value(QStringLiteral("units")).toString();
}
}

namespace DrosofilaLive
{
bool loadSwcFile(const QString& filePath,
                 std::uint64_t neuronId,
                 const QString& neuronName,
                 NeuronSkeleton& neuron,
                 SwcLoadStats& stats,
                 QString& error,
                 double targetDisplaySpan)
{
    neuron = NeuronSkeleton();
    stats = SwcLoadStats();
    error.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        error = QStringLiteral("Cannot open SWC file: %1").arg(file.errorString());
        return false;
    }

    std::vector<RawSwcNode> raw;
    raw.reserve(20000);

    QTextStream stream(&file);
    unsigned lineNumber = 0;
    while (!stream.atEnd())
    {
        ++lineNumber;
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('#')))
        {
            parseMetaLine(line, stats);
            continue;
        }

        const QStringList fields = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 7)
        {
            error = QStringLiteral("Invalid SWC at line %1: expected at least 7 columns.").arg(lineNumber);
            return false;
        }

        bool okId = false, okX = false, okY = false, okZ = false, okParent = false;
        RawSwcNode n;
        n.id = fields.at(0).toLongLong(&okId);
        n.x = fields.at(2).toDouble(&okX);
        n.y = fields.at(3).toDouble(&okY);
        n.z = fields.at(4).toDouble(&okZ);
        n.parentId = fields.at(6).toLongLong(&okParent);

        if (!okId || !okX || !okY || !okZ || !okParent ||
            !std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z))
        {
            error = QStringLiteral("Invalid numeric field in SWC at line %1.").arg(lineNumber);
            return false;
        }
        raw.push_back(n);
    }

    if (raw.size() < 2)
    {
        error = QStringLiteral("SWC contains fewer than two valid nodes.");
        return false;
    }

    std::unordered_map<qint64, int> indexById;
    indexById.reserve(raw.size() * 2u);
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        if (indexById.find(raw[i].id) != indexById.end())
        {
            error = QStringLiteral("Duplicate SWC node id %1.").arg(raw[i].id);
            return false;
        }
        indexById[raw[i].id] = static_cast<int>(i);
    }

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double minZ = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();

    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        minX = std::min(minX, raw[i].x); minY = std::min(minY, raw[i].y); minZ = std::min(minZ, raw[i].z);
        maxX = std::max(maxX, raw[i].x); maxY = std::max(maxY, raw[i].y); maxZ = std::max(maxZ, raw[i].z);
    }

    stats.minX = minX; stats.minY = minY; stats.minZ = minZ;
    stats.maxX = maxX; stats.maxY = maxY; stats.maxZ = maxZ;
    stats.originalSpanX = maxX - minX;
    stats.originalSpanY = maxY - minY;
    stats.originalSpanZ = maxZ - minZ;
    const double maxSpan = std::max(stats.originalSpanX, std::max(stats.originalSpanY, stats.originalSpanZ));

    if (!(maxSpan > 0.0) || !std::isfinite(maxSpan))
    {
        error = QStringLiteral("SWC has degenerate coordinates.");
        return false;
    }

    if (!(targetDisplaySpan > 0.0))
        targetDisplaySpan = 18.0;
    stats.displayScale = targetDisplaySpan / maxSpan;

    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);
    const double cz = 0.5 * (minZ + maxZ);

    neuron.id = neuronId;
    neuron.name = neuronName.toStdString();
    neuron.nodes.resize(raw.size());

    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        int parentIndex = -1;
        if (raw[i].parentId >= 0)
        {
            const std::unordered_map<qint64, int>::const_iterator p = indexById.find(raw[i].parentId);
            if (p == indexById.end())
            {
                error = QStringLiteral("SWC node %1 references missing parent %2.")
                            .arg(raw[i].id).arg(raw[i].parentId);
                neuron = NeuronSkeleton();
                return false;
            }
            parentIndex = p->second;
        }

        if (parentIndex < 0) ++stats.roots; else ++stats.segments;
        neuron.nodes[i] = SkeletonNode(
            static_cast<float>((raw[i].x - cx) * stats.displayScale),
            static_cast<float>((raw[i].y - cy) * stats.displayScale),
            static_cast<float>((raw[i].z - cz) * stats.displayScale),
            parentIndex);
    }

    stats.nodes = static_cast<unsigned>(neuron.nodes.size());
    return true;
}
}
