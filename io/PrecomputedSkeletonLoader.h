#pragma once

#include "model/NeuronSkeleton.h"

#include <QString>

#include <cstdint>

namespace DrosofilaLive
{
struct PrecomputedSkeletonStats
{
    std::uint32_t vertices;
    std::uint32_t sourceEdges;
    std::uint32_t treeSegments;
    std::uint32_t components;
    std::uint32_t droppedCycleEdges;
    double minX, minY, minZ;
    double maxX, maxY, maxZ;
    double spanX, spanY, spanZ;

    PrecomputedSkeletonStats();
};

bool loadPrecomputedSkeleton(const QString& filePath,
                             std::uint64_t expectedRootId,
                             const QString& displayName,
                             NeuronSkeleton& out,
                             PrecomputedSkeletonStats& stats,
                             QString& error);
}
