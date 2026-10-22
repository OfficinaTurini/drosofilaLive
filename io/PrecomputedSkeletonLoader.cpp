#include "PrecomputedSkeletonLoader.h"

#include <QFile>
#include <QByteArray>
#include <QIODevice>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <vector>

namespace
{
bool readU32(const QByteArray& data, int offset, std::uint32_t& value)
{
    if (offset < 0 || offset + 4 > data.size())
        return false;
    value = qFromLittleEndian<std::uint32_t>(reinterpret_cast<const uchar*>(data.constData() + offset));
    return true;
}

bool readF32(const QByteArray& data, int offset, float& value)
{
    std::uint32_t bits = 0;
    if (!readU32(data, offset, bits))
        return false;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(static_cast<double>(value));
}
}

namespace DrosofilaLive
{
PrecomputedSkeletonStats::PrecomputedSkeletonStats()
    : vertices(0), sourceEdges(0), treeSegments(0), components(0), droppedCycleEdges(0),
      minX(0.0), minY(0.0), minZ(0.0), maxX(0.0), maxY(0.0), maxZ(0.0),
      spanX(0.0), spanY(0.0), spanZ(0.0)
{
}

bool loadPrecomputedSkeleton(const QString& filePath,
                             std::uint64_t expectedRootId,
                             const QString& displayName,
                             NeuronSkeleton& out,
                             PrecomputedSkeletonStats& stats,
                             QString& error)
{
    out = NeuronSkeleton();
    stats = PrecomputedSkeletonStats();
    error.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();
    if (data.size() < 8)
    {
        error = QStringLiteral("File too small for Neuroglancer skeleton header: %1").arg(filePath);
        return false;
    }

    std::uint32_t nv = 0, ne = 0;
    if (!readU32(data, 0, nv) || !readU32(data, 4, ne) || nv == 0)
    {
        error = QStringLiteral("Invalid Neuroglancer skeleton header in %1").arg(filePath);
        return false;
    }

    const quint64 vertexBytes = static_cast<quint64>(nv) * 3ULL * 4ULL;
    const quint64 edgeBytes = static_cast<quint64>(ne) * 2ULL * 4ULL;
    const quint64 minimumSize = 8ULL + vertexBytes + edgeBytes;
    if (minimumSize > static_cast<quint64>(std::numeric_limits<int>::max()) ||
        static_cast<quint64>(data.size()) < minimumSize)
    {
        error = QStringLiteral("Truncated skeleton %1: header requires %2 bytes, file has %3")
                    .arg(filePath)
                    .arg(minimumSize)
                    .arg(data.size());
        return false;
    }

    std::vector<SkeletonNode> nodes(static_cast<std::size_t>(nv));
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double minZ = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();

    int offset = 8;
    for (std::uint32_t i = 0; i < nv; ++i)
    {
        float xNm = 0.0f, yNm = 0.0f, zNm = 0.0f;
        if (!readF32(data, offset + 0, xNm) ||
            !readF32(data, offset + 4, yNm) ||
            !readF32(data, offset + 8, zNm))
        {
            error = QStringLiteral("Invalid vertex %1 in %2").arg(i).arg(filePath);
            return false;
        }
        offset += 12;

        // FlyWire materialization-783 high-resolution skeleton coordinates are in nm.
        // Store the common anatomical coordinate system internally in micrometres.
        const double x = static_cast<double>(xNm) / 1000.0;
        const double y = static_cast<double>(yNm) / 1000.0;
        const double z = static_cast<double>(zNm) / 1000.0;
        nodes[static_cast<std::size_t>(i)] = SkeletonNode(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), -1);

        minX = std::min(minX, x); minY = std::min(minY, y); minZ = std::min(minZ, z);
        maxX = std::max(maxX, x); maxY = std::max(maxY, y); maxZ = std::max(maxZ, z);
    }

    std::vector<std::vector<std::uint32_t> > adjacency(static_cast<std::size_t>(nv));
    for (std::uint32_t e = 0; e < ne; ++e)
    {
        std::uint32_t a = 0, b = 0;
        if (!readU32(data, offset + 0, a) || !readU32(data, offset + 4, b))
        {
            error = QStringLiteral("Invalid edge %1 in %2").arg(e).arg(filePath);
            return false;
        }
        offset += 8;
        if (a >= nv || b >= nv)
        {
            error = QStringLiteral("Edge %1 references vertex %2/%3 outside [0,%4) in %5")
                        .arg(e).arg(a).arg(b).arg(nv).arg(filePath);
            return false;
        }
        if (a == b)
            continue;
        adjacency[static_cast<std::size_t>(a)].push_back(b);
        adjacency[static_cast<std::size_t>(b)].push_back(a);
    }

    // Precomputed skeleton edges are undirected. Orient each connected component
    // into a parent tree for the renderer. Cycle/back edges are intentionally dropped.
    std::vector<unsigned char> visited(static_cast<std::size_t>(nv), 0);
    std::uint32_t components = 0;
    std::uint32_t treeEdges = 0;
    for (std::uint32_t root = 0; root < nv; ++root)
    {
        if (visited[static_cast<std::size_t>(root)])
            continue;

        ++components;
        visited[static_cast<std::size_t>(root)] = 1;
        nodes[static_cast<std::size_t>(root)].parent = -1;
        std::queue<std::uint32_t> q;
        q.push(root);

        while (!q.empty())
        {
            const std::uint32_t u = q.front();
            q.pop();
            const std::vector<std::uint32_t>& nbrs = adjacency[static_cast<std::size_t>(u)];
            for (std::size_t j = 0; j < nbrs.size(); ++j)
            {
                const std::uint32_t v = nbrs[j];
                if (!visited[static_cast<std::size_t>(v)])
                {
                    visited[static_cast<std::size_t>(v)] = 1;
                    nodes[static_cast<std::size_t>(v)].parent = static_cast<int>(u);
                    ++treeEdges;
                    q.push(v);
                }
            }
        }
    }

    out.id = expectedRootId;
    out.name = displayName.toStdString();
    out.nodes.swap(nodes);

    stats.vertices = nv;
    stats.sourceEdges = ne;
    stats.treeSegments = treeEdges;
    stats.components = components;
    stats.droppedCycleEdges = (ne >= treeEdges) ? (ne - treeEdges) : 0u;
    stats.minX = minX; stats.minY = minY; stats.minZ = minZ;
    stats.maxX = maxX; stats.maxY = maxY; stats.maxZ = maxZ;
    stats.spanX = maxX - minX; stats.spanY = maxY - minY; stats.spanZ = maxZ - minZ;
    return true;
}
}
