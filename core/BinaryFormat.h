#pragma once

#include <QtGlobal>

/*
 * Drosofila Project binary format v1
 *
 * The Python builder writes little-endian files. The current project target
 * is Windows/x64 (MSVC2019), which is little-endian.
 */
#if Q_BYTE_ORDER != Q_LITTLE_ENDIAN
#error "Drosofila binary v1 currently requires a little-endian target."
#endif

namespace DrosofilaBinary
{
static const quint32 Version = 1;

#pragma pack(push, 1)

struct EdgeFileHeader
{
    char    magic[8];          // "DROEDGE\0"
    quint32 version;           // 1
    quint32 headerSize;        // sizeof(EdgeFileHeader) = 64
    quint32 neuronCount;
    quint32 reserved0;
    quint64 edgeCount;
    quint64 synapseCount;
    quint64 rowOffsetOffset;   // uint32[N+1]
    quint64 targetOffset;      // uint32[E]
    quint64 weightOffset;      // int16[E], signed Connectivity
};

struct NeuronFileHeader
{
    char    magic[8];          // "DRONEUR\0"
    quint32 version;           // 1
    quint32 headerSize;        // sizeof(NeuronFileHeader) = 128
    quint32 neuronCount;
    quint32 reserved0;

    quint64 rootIdOffset;      // uint64[N]
    quint64 signOffset;        // int8[N]  (+1/-1/0)
    quint64 fanInOffset;       // uint32[N]
    quint64 fanOutOffset;      // uint32[N]
    quint64 synInOffset;       // uint32[N]
    quint64 synOutOffset;      // uint32[N]

    quint8  reserved[56];
};

#pragma pack(pop)

static_assert(sizeof(EdgeFileHeader) == 64, "Unexpected EdgeFileHeader size");
static_assert(sizeof(NeuronFileHeader) == 128, "Unexpected NeuronFileHeader size");
}
