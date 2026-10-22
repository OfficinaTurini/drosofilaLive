#pragma once

#include "SkeletonNode.h"

#include <cstdint>
#include <string>
#include <vector>

struct NeuronSkeleton
{
    std::uint64_t id;
    std::string name;
    std::vector<SkeletonNode> nodes;

    NeuronSkeleton()
        : id(0)
    {
    }
};
