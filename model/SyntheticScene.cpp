#include "SyntheticScene.h"

#include <cmath>

namespace
{
NeuronSkeleton buildNeuron(std::uint64_t id,
                           const char* name,
                           float originX,
                           float phase)
{
    NeuronSkeleton neuron;
    neuron.id = id;
    neuron.name = name;

    const int trunkNodes = 30;
    neuron.nodes.reserve(80);
    neuron.nodes.push_back(SkeletonNode(originX, -5.8f, 0.0f, -1));

    for (int i = 1; i < trunkNodes; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(trunkNodes - 1);
        const float x = originX + 0.38f * std::sin(phase + t * 3.1f);
        const float y = -5.8f + 11.6f * t;
        const float z = 0.55f * std::cos(phase * 0.7f + t * 4.0f);
        neuron.nodes.push_back(SkeletonNode(x, y, z, i - 1));
    }

    for (int attach = 4; attach < trunkNodes - 2; attach += 4)
    {
        const SkeletonNode root = neuron.nodes[attach];
        const float side = ((attach / 4) & 1) ? -1.0f : 1.0f;
        int parent = attach;
        const int branchLength = 5 + ((attach / 4) % 3);

        for (int b = 1; b <= branchLength; ++b)
        {
            const float a = phase + 0.52f * static_cast<float>(attach) + 0.23f * static_cast<float>(b);
            const float r = 0.34f * static_cast<float>(b);
            const float x = root.x + side * r * std::cos(a);
            const float y = root.y + 0.23f * static_cast<float>(b);
            const float z = root.z + r * std::sin(a);
            neuron.nodes.push_back(SkeletonNode(x, y, z, parent));
            parent = static_cast<int>(neuron.nodes.size()) - 1;
        }
    }

    return neuron;
}
}

namespace DrosofilaLive
{
std::vector<NeuronSkeleton> buildSyntheticScene()
{
    std::vector<NeuronSkeleton> neurons;
    neurons.reserve(3);
    neurons.push_back(buildNeuron(1001u, "Synthetic A", -4.5f, 0.15f));
    neurons.push_back(buildNeuron(1002u, "Synthetic B",  0.0f, 1.65f));
    neurons.push_back(buildNeuron(1003u, "Synthetic C",  4.5f, 3.10f));
    return neurons;
}
}
