#pragma once

#include "../model/NeuronSkeleton.h"

#include <QString>
#include <cstdint>

struct SwcLoadStats
{
    unsigned nodes;
    unsigned segments;
    unsigned roots;
    double minX, minY, minZ;
    double maxX, maxY, maxZ;
    double originalSpanX;
    double originalSpanY;
    double originalSpanZ;
    double displayScale;
    QString metadataId;
    QString units;

    SwcLoadStats()
        : nodes(0), segments(0), roots(0),
          minX(0.0), minY(0.0), minZ(0.0),
          maxX(0.0), maxY(0.0), maxZ(0.0),
          originalSpanX(0.0), originalSpanY(0.0), originalSpanZ(0.0),
          displayScale(1.0)
    {
    }
};

namespace DrosofilaLive
{
// Standard SWC: id type x y z radius parent_id.
// Parent ids are resolved in a second pass, therefore the file may list
// children before parents (the real CB0701 file does this).
//
// The scientific coordinates are never modified on disk. The NeuronSkeleton
// returned here is centered and isotropically scaled only for display.
bool loadSwcFile(const QString& filePath,
                 std::uint64_t neuronId,
                 const QString& neuronName,
                 NeuronSkeleton& neuron,
                 SwcLoadStats& stats,
                 QString& error,
                 double targetDisplaySpan = 18.0);
}
