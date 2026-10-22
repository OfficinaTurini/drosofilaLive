#include "SkeletonSceneTransform.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DrosofilaLive
{
SkeletonSceneTransformStats::SkeletonSceneTransformStats()
    : minX(0.0), minY(0.0), minZ(0.0), maxX(0.0), maxY(0.0), maxZ(0.0),
      spanX(0.0), spanY(0.0), spanZ(0.0), scale(1.0)
{
}

bool normalizeSkeletonScene(std::vector<NeuronSkeleton>& neurons,
                            SkeletonSceneTransformStats& stats,
                            double targetDisplaySpan)
{
    stats = SkeletonSceneTransformStats();
    if (neurons.empty() || targetDisplaySpan <= 0.0)
        return false;

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double minZ = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();
    std::size_t points = 0;

    for (std::vector<NeuronSkeleton>::const_iterator n = neurons.begin(); n != neurons.end(); ++n)
    {
        for (std::vector<SkeletonNode>::const_iterator p = n->nodes.begin(); p != n->nodes.end(); ++p)
        {
            minX = std::min(minX, static_cast<double>(p->x));
            minY = std::min(minY, static_cast<double>(p->y));
            minZ = std::min(minZ, static_cast<double>(p->z));
            maxX = std::max(maxX, static_cast<double>(p->x));
            maxY = std::max(maxY, static_cast<double>(p->y));
            maxZ = std::max(maxZ, static_cast<double>(p->z));
            ++points;
        }
    }
    if (!points)
        return false;

    const double spanX = maxX - minX;
    const double spanY = maxY - minY;
    const double spanZ = maxZ - minZ;
    const double largest = std::max(spanX, std::max(spanY, spanZ));
    if (!std::isfinite(largest) || largest <= 1e-12)
        return false;

    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);
    const double cz = 0.5 * (minZ + maxZ);
    const double scale = targetDisplaySpan / largest;

    for (std::vector<NeuronSkeleton>::iterator n = neurons.begin(); n != neurons.end(); ++n)
    {
        for (std::vector<SkeletonNode>::iterator p = n->nodes.begin(); p != n->nodes.end(); ++p)
        {
            p->x = static_cast<float>((static_cast<double>(p->x) - cx) * scale);
            p->y = static_cast<float>((static_cast<double>(p->y) - cy) * scale);
            p->z = static_cast<float>((static_cast<double>(p->z) - cz) * scale);
        }
    }

    stats.minX = minX; stats.minY = minY; stats.minZ = minZ;
    stats.maxX = maxX; stats.maxY = maxY; stats.maxZ = maxZ;
    stats.spanX = spanX; stats.spanY = spanY; stats.spanZ = spanZ;
    stats.scale = scale;
    return true;
}
}
