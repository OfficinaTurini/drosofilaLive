#pragma once

#include "NeuronSkeleton.h"

#include <vector>

namespace DrosofilaLive
{
struct SkeletonSceneTransformStats
{
    double minX, minY, minZ;
    double maxX, maxY, maxZ;
    double spanX, spanY, spanZ;
    double scale;

    SkeletonSceneTransformStats();
};

bool normalizeSkeletonScene(std::vector<NeuronSkeleton>& neurons,
                            SkeletonSceneTransformStats& stats,
                            double targetDisplaySpan = 18.0);
}
