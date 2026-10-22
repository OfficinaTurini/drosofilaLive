#pragma once

#include "INeuronRenderer3D.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Qwt3D
{
    class SurfacePlot;
    struct RGBA;
}

class QwtPlot3DRenderer : public INeuronRenderer3D
{
public:
    explicit QwtPlot3DRenderer(Qwt3D::SurfacePlot* plot);
    ~QwtPlot3DRenderer() override;

    void clear() override;
    void setSkeleton(const std::vector<NeuronSkeleton>& neurons) override;
    void setNeuronState(std::uint64_t neuronId, const NeuronVisualState& state) override;
    void setTime(double ms) override;
    void setLighting(bool enabled) override;
    void resetView() override;
    void refresh() override;
    void setSurfaceStyle(int style) override;
    void setShadingStyle(int style) override;
    void setCoordinateStyle(int style) override;
    void setSmoothMesh(bool enabled) override;
    void setGridVisible(bool enabled) override;
    void setMeshLineWidth(double width) override;
    void setOrthographic(bool enabled) override;
    void setColorPalette(int palette) override;

    // Detach renderer-owned callbacks from QwtPlot3D before MainWindow teardown.
    // The SurfacePlot itself remains owned by the Qt widget hierarchy.
    void prepareForShutdown() override;

private:
    struct VertexKey
    {
        std::int64_t x;
        std::int64_t y;
        std::int64_t z;

        bool operator==(const VertexKey& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct VertexKeyHash
    {
        std::size_t operator()(const VertexKey& key) const;
    };

    struct NeuronBounds
    {
        std::uint64_t id;
        double minX, minY, minZ;
        double maxX, maxY, maxZ;
        double cx, cy, cz;
    };

    class NeuronColor;

    static VertexKey vertexKey(double x, double y, double z);
    Qwt3D::RGBA colorForPoint(double x, double y, double z) const;
    const NeuronBounds* nearestBounds(double x, double y, double z) const;
    void applyPresentationOptions();
    void applyColorPalette();
    Qwt3D::RGBA structuralColor(std::uint64_t neuronId) const;

    Qwt3D::SurfacePlot* plot_;
    NeuronColor* color_;
    std::vector<NeuronBounds> bounds_;
    std::unordered_map<VertexKey, std::uint64_t, VertexKeyHash> vertexOwners_;
    std::unordered_map<std::uint64_t, NeuronVisualState> states_;
    double timeMs_;
    int surfaceStyle_;
    int shadingStyle_;
    int coordinateStyle_;
    bool smoothMesh_;
    bool gridVisible_;
    double meshLineWidth_;
    bool orthographic_;
    int colorPalette_;
    bool dataLoaded_;
};
