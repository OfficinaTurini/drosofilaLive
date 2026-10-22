#pragma once

#include "../model/NeuronSkeleton.h"
#include "../model/NeuronVisualState.h"

#include <cstdint>
#include <vector>

class INeuronRenderer3D
{
public:
    virtual ~INeuronRenderer3D() {}

    virtual void clear() = 0;
    virtual void setSkeleton(const std::vector<NeuronSkeleton>& neurons) = 0;
    virtual void setNeuronState(std::uint64_t neuronId, const NeuronVisualState& state) = 0;
    virtual void setTime(double ms) = 0;
    virtual void setLighting(bool enabled) = 0;
    virtual void resetView() = 0;
    virtual void refresh() = 0;

    // Optional visual controls.  Values are backend-neutral UI indices:
    // surface: 0=filled, 1=filled+mesh, 2=wireframe, 3=hidden line
    // shading: 0=Gouraud, 1=flat
    // coordinates: 0=frame, 1=box, 2=none
    virtual void setSurfaceStyle(int) {}
    virtual void setShadingStyle(int) {}
    virtual void setCoordinateStyle(int) {}
    virtual void setSmoothMesh(bool) {}
    virtual void setGridVisible(bool) {}
    virtual void setMeshLineWidth(double) {}
    virtual void setOrthographic(bool) {}
    // Static morphology palette. Replay overlays keep fixed scientific semantics.
    // 0=neuron identity, 1=ice, 2=warm, 3=light laboratory.
    virtual void setColorPalette(int) {}

    // Optional shutdown hook for backends that keep callbacks or helper
    // objects inside a GUI widget owned by Qt.  Default is intentionally
    // a no-op so alternate/future renderers are not forced to implement it.
    virtual void prepareForShutdown() {}
};
