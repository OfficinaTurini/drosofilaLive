#include "CausalCircuit.h"

namespace DrosofilaLive
{
const std::vector<CausalNeuronSpec>& validatedCausalCircuit()
{
    static const std::vector<CausalNeuronSpec> circuit =
    {
        { "DNge031", "dynamic brake",       720575940640589171ULL, "DNge031_720575940640589171.ngskel" },
        { "CB0393",  "driver / sustained",  720575940632047890ULL, "CB0393_720575940632047890.ngskel" },
        { "CB0177",  "ignition driver",     720575940630579574ULL, "CB0177_720575940630579574.ngskel" },
        { "CB0248",  "early modulator",     720575940622695448ULL, "CB0248_720575940622695448.ngskel" },
        { "CB0366",  "initial modulator",   720575940614763666ULL, "CB0366_720575940614763666.ngskel" },
        { "DNge062", "late/output branch",  720575940627099338ULL, "DNge062_720575940627099338.ngskel" },
        { "CB0701",  "TARGET/MN9",          720575940660219265ULL, "CB0701_720575940660219265.ngskel" }
    };
    return circuit;
}
}
