#pragma once

#include <cstdint>
#include <vector>

namespace DrosofilaLive
{
struct CausalNeuronSpec
{
    const char* type;
    const char* role;
    std::uint64_t rootId;
    const char* fileName;
};

const std::vector<CausalNeuronSpec>& validatedCausalCircuit();
}
