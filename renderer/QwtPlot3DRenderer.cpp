#include "QwtPlot3DRenderer.h"
#include "diagnostics.h"

#include "qwt3d_color.h"
#include "qwt3d_coordsys.h"
#include "qwt3d_surfaceplot.h"

#include <QFont>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
struct V3
{
    double x, y, z;
    V3() : x(0.0), y(0.0), z(0.0) {}
    V3(double xx, double yy, double zz) : x(xx), y(yy), z(zz) {}
};

V3 sub(const V3& a, const V3& b)
{
    return V3(a.x - b.x, a.y - b.y, a.z - b.z);
}

V3 mul(const V3& a, double s)
{
    return V3(a.x * s, a.y * s, a.z * s);
}

V3 add(const V3& a, const V3& b)
{
    return V3(a.x + b.x, a.y + b.y, a.z + b.z);
}

double dot(const V3& a, const V3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

V3 cross(const V3& a, const V3& b)
{
    return V3(a.y * b.z - a.z * b.y,
              a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}

V3 normalized(const V3& v)
{
    const double l = std::sqrt(dot(v, v));
    if (l < 1e-12)
        return V3(0.0, 0.0, 1.0);
    return mul(v, 1.0 / l);
}

struct OwnedPoint
{
    V3 p;
    std::uint64_t neuronId;
};

void appendTube(Qwt3D::TripleField& vertices,
                Qwt3D::CellField& cells,
                std::vector<OwnedPoint>& ownedPoints,
                std::uint64_t neuronId,
                const V3& p0,
                const V3& p1,
                double radius,
                unsigned sides)
{
    sides = std::max(3u, sides);
    const V3 axis = normalized(sub(p1, p0));
    const V3 helper = (std::fabs(axis.z) < 0.85) ? V3(0.0, 0.0, 1.0) : V3(0.0, 1.0, 0.0);
    const V3 u = normalized(cross(axis, helper));
    const V3 v = normalized(cross(axis, u));

    const unsigned base0 = static_cast<unsigned>(vertices.size());
    for (unsigned i = 0; i < sides; ++i)
    {
        const double a = 2.0 * Qwt3D::PI * static_cast<double>(i) / static_cast<double>(sides);
        const V3 radial = add(mul(u, std::cos(a)), mul(v, std::sin(a)));
        const V3 q = add(p0, mul(radial, radius));
        vertices.push_back(Qwt3D::Triple(q.x, q.y, q.z));
        ownedPoints.push_back(OwnedPoint{ q, neuronId });
    }

    const unsigned base1 = static_cast<unsigned>(vertices.size());
    for (unsigned i = 0; i < sides; ++i)
    {
        const double a = 2.0 * Qwt3D::PI * static_cast<double>(i) / static_cast<double>(sides);
        const V3 radial = add(mul(u, std::cos(a)), mul(v, std::sin(a)));
        const V3 q = add(p1, mul(radial, radius));
        vertices.push_back(Qwt3D::Triple(q.x, q.y, q.z));
        ownedPoints.push_back(OwnedPoint{ q, neuronId });
    }

    for (unsigned i = 0; i < sides; ++i)
    {
        const unsigned n = (i + 1u) % sides;
        Qwt3D::Cell cell;
        cell.push_back(base0 + i);
        cell.push_back(base0 + n);
        cell.push_back(base1 + n);
        cell.push_back(base1 + i);
        cells.push_back(cell);
    }
}
}

class QwtPlot3DRenderer::NeuronColor : public Qwt3D::Color
{
public:
    explicit NeuronColor(const QwtPlot3DRenderer* owner)
        : owner_(owner)
    {
    }

    Qwt3D::RGBA operator()(double x, double y, double z) const override
    {
        return owner_->colorForPoint(x, y, z);
    }

private:
    const QwtPlot3DRenderer* owner_;
};

namespace
{
class ShutdownColor : public Qwt3D::Color
{
public:
    Qwt3D::RGBA operator()(double, double, double) const override
    {
        return Qwt3D::RGBA(0.18, 0.22, 0.28, 1.0);
    }
};
}

QwtPlot3DRenderer::QwtPlot3DRenderer(Qwt3D::SurfacePlot* plot)
    : plot_(plot), color_(0), timeMs_(0.0),
      surfaceStyle_(0), shadingStyle_(0), coordinateStyle_(0),
      smoothMesh_(true), gridVisible_(false), meshLineWidth_(0.8),
      orthographic_(false), colorPalette_(0), dataLoaded_(false)
{
    if (!plot_)
        return;

    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer: constructor begin");
    // Safe-start: keep QwtPlot3D's own default StandardColor until real data
    // exists. The custom per-neuron color functor is installed by setSkeleton().
    // The static morphology palette is deliberately separate from replay colors:
    // activity/spike/ablation/difference keep invariant semantics across themes.
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer: applying morphology palette");
    plot_->setTitle("drosofilaLive - renderer ready");
    plot_->setTitleFont("Segoe UI", 11, QFont::Normal);

    Qwt3D::CoordinateSystem* coords = plot_->coordinates();
    coords->setNumberFont("Segoe UI", 8);
    coords->setLabelFont("Segoe UI", 9, QFont::DemiBold);
    coords->setLineWidth(1.0);
    coords->setLineSmooth(true);
    applyColorPalette();

    // Soft, high-ambient lighting keeps thin neurites readable instead of
    // turning back-facing polygons almost black.
    plot_->setMaterialComponent(GL_AMBIENT, 0.46, 0.46, 0.46, 1.0);
    plot_->setMaterialComponent(GL_DIFFUSE, 0.78, 0.78, 0.78, 1.0);
    plot_->setMaterialComponent(GL_SPECULAR, 0.14, 0.14, 0.14, 1.0);
    plot_->setShininess(8.0);
    plot_->setLightComponent(GL_AMBIENT, 0.42, 0.42, 0.42, 1.0);
    plot_->setLightComponent(GL_DIFFUSE, 0.82, 0.82, 0.82, 1.0);
    plot_->setLightComponent(GL_SPECULAR, 0.16, 0.16, 0.16, 1.0);
    plot_->illuminate(0);
    plot_->setLightShift(0.0, 0.0, 3000.0);
    plot_->setLightRotation(28.0, -32.0, 18.0);

    plot_->enableMouse(true);
    plot_->enableKeyboard(true);
    plot_->setKeySpeed(15, 20, 20);
    applyPresentationOptions();
    // Start from a deterministic empty scene.  Without an initial paint some
    // QwtPlot3D/QGLWidget builds can briefly expose an uncleared back buffer.
    plot_->setPlotStyle(Qwt3D::NOPLOT);
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer: resetView");
    resetView();
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer: constructor complete");
}

QwtPlot3DRenderer::~QwtPlot3DRenderer()
{
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::~QwtPlot3DRenderer: begin");
    // prepareForShutdown() is normally called by MainWindow first.  Never
    // delete plot_: QObject ownership belongs to the UI hierarchy.
    color_ = 0;
    plot_ = 0;
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::~QwtPlot3DRenderer: end");
}

void QwtPlot3DRenderer::prepareForShutdown()
{
    if (!plot_)
        return;

    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::prepareForShutdown: begin");

    // QwtPlot3D keeps the active Color object as part of the plot state.
    // NeuronColor calls back into this renderer, therefore it MUST NOT remain
    // installed after renderer_ has been destroyed.  Replace it with a small
    // self-contained color functor and then forget the plot pointer.
    if (color_)
    {
        plot_->setDataColor(new ShutdownColor());
        color_ = 0;
    }

    plot_->setPlotStyle(Qwt3D::NOPLOT);
    plot_->setTitle(QString());

    // Do not call updateGL() during teardown: the native GL context may already
    // be transitioning as the top-level window closes.
    plot_ = 0;
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::prepareForShutdown: detached");
}

void QwtPlot3DRenderer::clear()
{
    dataLoaded_ = false;
    bounds_.clear();
    vertexOwners_.clear();
    states_.clear();
    if (plot_)
    {
        plot_->setPlotStyle(Qwt3D::NOPLOT);
        plot_->setTitle("drosofilaLive - no scene");
        plot_->updateGL();
    }
}

void QwtPlot3DRenderer::setSkeleton(const std::vector<NeuronSkeleton>& neurons)
{
    if (!plot_)
        return;

    Qwt3D::TripleField vertices;
    Qwt3D::CellField cells;
    std::vector<OwnedPoint> ownedPoints;
    bounds_.clear();
    vertexOwners_.clear();
    states_.clear();

    std::size_t estimatedSegments = 0;
    for (std::vector<NeuronSkeleton>::const_iterator n = neurons.begin(); n != neurons.end(); ++n)
        estimatedSegments += n->nodes.size();
    vertices.reserve(estimatedSegments * 8u);
    cells.reserve(estimatedSegments * 4u);
    ownedPoints.reserve(estimatedSegments * 8u);

    const double tubeRadius = 0.070;
    const unsigned tubeSides = 4u;

    for (std::vector<NeuronSkeleton>::const_iterator n = neurons.begin(); n != neurons.end(); ++n)
    {
        if (n->nodes.empty())
            continue;

        NeuronBounds b;
        b.id = n->id;
        b.minX = b.minY = b.minZ = std::numeric_limits<double>::max();
        b.maxX = b.maxY = b.maxZ = -std::numeric_limits<double>::max();

        for (std::size_t i = 0; i < n->nodes.size(); ++i)
        {
            const SkeletonNode& node = n->nodes[i];
            b.minX = std::min(b.minX, static_cast<double>(node.x) - tubeRadius);
            b.minY = std::min(b.minY, static_cast<double>(node.y) - tubeRadius);
            b.minZ = std::min(b.minZ, static_cast<double>(node.z) - tubeRadius);
            b.maxX = std::max(b.maxX, static_cast<double>(node.x) + tubeRadius);
            b.maxY = std::max(b.maxY, static_cast<double>(node.y) + tubeRadius);
            b.maxZ = std::max(b.maxZ, static_cast<double>(node.z) + tubeRadius);

            if (node.parent < 0 || node.parent >= static_cast<int>(n->nodes.size()))
                continue;

            const SkeletonNode& parent = n->nodes[static_cast<std::size_t>(node.parent)];
            appendTube(vertices, cells, ownedPoints, n->id,
                       V3(parent.x, parent.y, parent.z),
                       V3(node.x, node.y, node.z),
                       tubeRadius, tubeSides);
        }

        b.cx = 0.5 * (b.minX + b.maxX);
        b.cy = 0.5 * (b.minY + b.maxY);
        b.cz = 0.5 * (b.minZ + b.maxZ);
        bounds_.push_back(b);
        states_[n->id] = NeuronVisualState();
    }

    for (std::vector<OwnedPoint>::const_iterator p = ownedPoints.begin(); p != ownedPoints.end(); ++p)
        vertexOwners_[vertexKey(p->p.x, p->p.y, p->p.z)] = p->neuronId;

    if (vertices.empty() || cells.empty())
    {
        clear();
        return;
    }

    DrosofilaLiveDiagnostics::log(QString("QwtPlot3DRenderer::setSkeleton: mesh ready vertices=%1 cells=%2")
        .arg(static_cast<qulonglong>(vertices.size()))
        .arg(static_cast<qulonglong>(cells.size())));

    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::setSkeleton: makeCurrent");
    plot_->makeCurrent();
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::setSkeleton: loadFromData begin");
    plot_->loadFromData(vertices, cells);
    dataLoaded_ = true;
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::setSkeleton: loadFromData returned");

    if (!color_)
        color_ = new NeuronColor(this);
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::setSkeleton: setDataColor begin");
    plot_->setDataColor(color_);
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::setSkeleton: setDataColor returned");

    // Reapply the user's 3D visual preferences after loading data.  Default is
    // FILLED (no edge overlay): on very thin tubes a dark FILLEDMESH edge can
    // visually dominate the polygon and look like a black halo.
    applyPresentationOptions();
    plot_->setTitle("drosofilaLive - connectome circuit");
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::setSkeleton: updateData begin");
    plot_->updateData();
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::setSkeleton: updateData returned");
    plot_->updateNormals();
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::setSkeleton: updateNormals returned");
    plot_->updateGL();
    DrosofilaLiveDiagnostics::log("QwtPlot3DRenderer::setSkeleton: complete");
}

void QwtPlot3DRenderer::setNeuronState(std::uint64_t neuronId, const NeuronVisualState& state)
{
    states_[neuronId] = state;
}

void QwtPlot3DRenderer::setTime(double ms)
{
    timeMs_ = ms;
    (void)timeMs_;
}

void QwtPlot3DRenderer::setLighting(bool enabled)
{
    if (!plot_)
        return;
    plot_->enableLighting(enabled);
    plot_->updateGL();
}

namespace
{
Qwt3D::RGBA mixColor(const Qwt3D::RGBA& a, const Qwt3D::RGBA& b, double t)
{
    t = std::max(0.0, std::min(1.0, t));
    return Qwt3D::RGBA(a.r + (b.r - a.r) * t,
                       a.g + (b.g - a.g) * t,
                       a.b + (b.b - a.b) * t,
                       1.0);
}
}

Qwt3D::RGBA QwtPlot3DRenderer::structuralColor(std::uint64_t neuronId) const
{
    switch (colorPalette_)
    {
    case 1: // Ice morphology
        return Qwt3D::RGBA(0.42, 0.76, 0.90, 1.0);
    case 2: // Warm morphology
        return Qwt3D::RGBA(0.88, 0.68, 0.40, 1.0);
    case 3: // Light laboratory
        return Qwt3D::RGBA(0.16, 0.36, 0.58, 1.0);
    default: // Neuron identity: restrained categorical colors, deterministic by root ID.
        break;
    }

    static const double c[8][3] = {
        {0.30, 0.63, 0.86}, // blue
        {0.30, 0.72, 0.55}, // green
        {0.86, 0.58, 0.28}, // amber
        {0.67, 0.48, 0.80}, // violet
        {0.28, 0.70, 0.76}, // cyan
        {0.82, 0.45, 0.49}, // coral
        {0.64, 0.67, 0.35}, // olive
        {0.52, 0.58, 0.78}  // indigo
    };
    int i = 0;
    for (std::size_t k = 0; k < bounds_.size(); ++k)
    {
        if (bounds_[k].id == neuronId)
        {
            i = static_cast<int>(k % 8u);
            break;
        }
    }
    return Qwt3D::RGBA(c[i][0], c[i][1], c[i][2], 1.0);
}

void QwtPlot3DRenderer::applyColorPalette()
{
    if (!plot_)
        return;

    Qwt3D::CoordinateSystem* coords = plot_->coordinates();
    if (colorPalette_ == 3)
    {
        // Light background for screenshots/print. Keep every auxiliary Qwt3D
        // color explicit so no black/default artifacts leak into the scene.
        plot_->setBackgroundColor(Qwt3D::RGBA(0.94, 0.955, 0.965, 1.0));
        plot_->setMeshColor(Qwt3D::RGBA(0.38, 0.47, 0.55, 1.0));
        plot_->setTitleColor(Qwt3D::RGBA(0.12, 0.18, 0.24, 1.0));
        coords->setAxesColor(Qwt3D::RGBA(0.28, 0.36, 0.43, 1.0));
        coords->setNumberColor(Qwt3D::RGBA(0.24, 0.30, 0.36, 1.0));
        coords->setLabelColor(Qwt3D::RGBA(0.12, 0.18, 0.24, 1.0));
        coords->setGridLinesColor(Qwt3D::RGBA(0.72, 0.77, 0.81, 1.0));
    }
    else if (colorPalette_ == 2)
    {
        plot_->setBackgroundColor(Qwt3D::RGBA(0.075, 0.065, 0.060, 1.0));
        plot_->setMeshColor(Qwt3D::RGBA(0.58, 0.49, 0.38, 1.0));
        plot_->setTitleColor(Qwt3D::RGBA(0.95, 0.90, 0.82, 1.0));
        coords->setAxesColor(Qwt3D::RGBA(0.61, 0.53, 0.44, 1.0));
        coords->setNumberColor(Qwt3D::RGBA(0.82, 0.76, 0.67, 1.0));
        coords->setLabelColor(Qwt3D::RGBA(0.94, 0.89, 0.81, 1.0));
        coords->setGridLinesColor(Qwt3D::RGBA(0.30, 0.26, 0.22, 1.0));
    }
    else if (colorPalette_ == 1)
    {
        plot_->setBackgroundColor(Qwt3D::RGBA(0.035, 0.060, 0.085, 1.0));
        plot_->setMeshColor(Qwt3D::RGBA(0.38, 0.58, 0.69, 1.0));
        plot_->setTitleColor(Qwt3D::RGBA(0.88, 0.95, 0.98, 1.0));
        coords->setAxesColor(Qwt3D::RGBA(0.40, 0.61, 0.72, 1.0));
        coords->setNumberColor(Qwt3D::RGBA(0.70, 0.82, 0.88, 1.0));
        coords->setLabelColor(Qwt3D::RGBA(0.87, 0.94, 0.97, 1.0));
        coords->setGridLinesColor(Qwt3D::RGBA(0.18, 0.31, 0.39, 1.0));
    }
    else
    {
        // Neuron identity default: neutral dark slate so categorical neuron
        // colors carry structure without competing with the background.
        plot_->setBackgroundColor(Qwt3D::RGBA(0.050, 0.065, 0.080, 1.0));
        plot_->setMeshColor(Qwt3D::RGBA(0.43, 0.50, 0.56, 1.0));
        plot_->setTitleColor(Qwt3D::RGBA(0.90, 0.93, 0.95, 1.0));
        coords->setAxesColor(Qwt3D::RGBA(0.43, 0.55, 0.64, 1.0));
        coords->setNumberColor(Qwt3D::RGBA(0.70, 0.77, 0.82, 1.0));
        coords->setLabelColor(Qwt3D::RGBA(0.87, 0.91, 0.94, 1.0));
        coords->setGridLinesColor(Qwt3D::RGBA(0.22, 0.29, 0.34, 1.0));
    }
}

void QwtPlot3DRenderer::applyPresentationOptions()
{
    if (!plot_)
        return;

    switch (surfaceStyle_)
    {
    case 1: plot_->setPlotStyle(Qwt3D::FILLEDMESH); break;
    case 2: plot_->setPlotStyle(Qwt3D::WIREFRAME); break;
    case 3: plot_->setPlotStyle(Qwt3D::HIDDENLINE); break;
    default: plot_->setPlotStyle(Qwt3D::FILLED); break;
    }

    plot_->setShading(shadingStyle_ == 1 ? Qwt3D::FLAT : Qwt3D::GOURAUD);
    plot_->setSmoothMesh(smoothMesh_);
    plot_->setMeshLineWidth(meshLineWidth_);
    plot_->setOrtho(orthographic_);

    Qwt3D::COORDSTYLE cs = Qwt3D::FRAME;
    if (coordinateStyle_ == 1) cs = Qwt3D::BOX;
    else if (coordinateStyle_ == 2) cs = Qwt3D::NOCOORD;
    plot_->setCoordinateStyle(cs);

    Qwt3D::CoordinateSystem* coords = plot_->coordinates();
    if (gridVisible_ && cs != Qwt3D::NOCOORD)
        coords->setGridLines(true, false, Qwt3D::FLOOR | Qwt3D::BACK | Qwt3D::LEFT);
    else
        coords->setGridLines(false, false, Qwt3D::NOSIDEGRID);
}

void QwtPlot3DRenderer::setSurfaceStyle(int style)
{
    surfaceStyle_ = std::max(0, std::min(3, style));
    if (plot_) { applyPresentationOptions(); if (dataLoaded_) plot_->updateData(); plot_->updateGL(); }
}

void QwtPlot3DRenderer::setShadingStyle(int style)
{
    shadingStyle_ = (style == 1) ? 1 : 0;
    if (plot_) { applyPresentationOptions(); if (dataLoaded_) plot_->updateData(); plot_->updateGL(); }
}

void QwtPlot3DRenderer::setCoordinateStyle(int style)
{
    coordinateStyle_ = std::max(0, std::min(2, style));
    if (plot_) { applyPresentationOptions(); plot_->updateGL(); }
}

void QwtPlot3DRenderer::setSmoothMesh(bool enabled)
{
    smoothMesh_ = enabled;
    if (plot_) { applyPresentationOptions(); if (dataLoaded_) plot_->updateData(); plot_->updateGL(); }
}

void QwtPlot3DRenderer::setGridVisible(bool enabled)
{
    gridVisible_ = enabled;
    if (plot_) { applyPresentationOptions(); plot_->updateGL(); }
}

void QwtPlot3DRenderer::setMeshLineWidth(double width)
{
    meshLineWidth_ = std::max(0.2, std::min(4.0, width));
    if (plot_) { applyPresentationOptions(); if (dataLoaded_) plot_->updateData(); plot_->updateGL(); }
}

void QwtPlot3DRenderer::setOrthographic(bool enabled)
{
    orthographic_ = enabled;
    if (plot_) { applyPresentationOptions(); plot_->updateGL(); }
}

void QwtPlot3DRenderer::setColorPalette(int palette)
{
    colorPalette_ = std::max(0, std::min(3, palette));
    if (!plot_) return;
    applyColorPalette();
    if (dataLoaded_) plot_->updateData();
    plot_->updateGL();
}

void QwtPlot3DRenderer::resetView()
{
    if (!plot_)
        return;
    plot_->setRotation(24.0, 0.0, 16.0);
    plot_->setViewportShift(0.0, 0.0);
    plot_->setScale(1.0, 1.0, 1.0);
    plot_->setZoom(0.90);
    plot_->updateGL();
}

void QwtPlot3DRenderer::refresh()
{
    if (!plot_)
        return;
    plot_->updateData();
    plot_->updateGL();
}

std::size_t QwtPlot3DRenderer::VertexKeyHash::operator()(const VertexKey& key) const
{
    const std::size_t h1 = std::hash<std::int64_t>()(key.x);
    const std::size_t h2 = std::hash<std::int64_t>()(key.y);
    const std::size_t h3 = std::hash<std::int64_t>()(key.z);
    return h1 ^ (h2 + 0x9e3779b9u + (h1 << 6) + (h1 >> 2))
              ^ (h3 + 0x9e3779b9u + (h2 << 6) + (h2 >> 2));
}

QwtPlot3DRenderer::VertexKey QwtPlot3DRenderer::vertexKey(double x, double y, double z)
{
    const double q = 1000000.0;
    VertexKey key;
    key.x = static_cast<std::int64_t>(std::llround(x * q));
    key.y = static_cast<std::int64_t>(std::llround(y * q));
    key.z = static_cast<std::int64_t>(std::llround(z * q));
    return key;
}

const QwtPlot3DRenderer::NeuronBounds* QwtPlot3DRenderer::nearestBounds(double x, double y, double z) const
{
    const NeuronBounds* best = 0;
    double bestDistance = std::numeric_limits<double>::max();

    for (std::vector<NeuronBounds>::const_iterator it = bounds_.begin(); it != bounds_.end(); ++it)
    {
        const bool inside = (x >= it->minX && x <= it->maxX &&
                             y >= it->minY && y <= it->maxY &&
                             z >= it->minZ && z <= it->maxZ);
        const double dx = x - it->cx;
        const double dy = y - it->cy;
        const double dz = z - it->cz;
        const double d2 = dx * dx + dy * dy + dz * dz;

        if (inside && d2 < bestDistance)
        {
            best = &(*it);
            bestDistance = d2;
        }
    }

    if (best)
        return best;

    for (std::vector<NeuronBounds>::const_iterator it = bounds_.begin(); it != bounds_.end(); ++it)
    {
        const double dx = x - it->cx;
        const double dy = y - it->cy;
        const double dz = z - it->cz;
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestDistance)
        {
            best = &(*it);
            bestDistance = d2;
        }
    }

    return best;
}

Qwt3D::RGBA QwtPlot3DRenderer::colorForPoint(double x, double y, double z) const
{
    std::uint64_t ownerId = 0;
    const std::unordered_map<VertexKey, std::uint64_t, VertexKeyHash>::const_iterator owner =
        vertexOwners_.find(vertexKey(x, y, z));
    if (owner != vertexOwners_.end())
        ownerId = owner->second;
    else
    {
        // Fallback for QwtPlot3D versions that query the color functor at a
        // generated/interpolated point rather than at one of our mesh vertices.
        const NeuronBounds* b = nearestBounds(x, y, z);
        if (b) ownerId = b->id;
    }

    if (!ownerId)
        return structuralColor(0);

    std::unordered_map<std::uint64_t, NeuronVisualState>::const_iterator it = states_.find(ownerId);
    const NeuronVisualState state = (it != states_.end()) ? it->second : NeuronVisualState();

    if (state.ablated)
        return Qwt3D::RGBA(0.38, 0.40, 0.43, 1.0);
    if (state.differenceMode)
    {
        const double d = std::max(-1.0, std::min(1.0, static_cast<double>(state.deltaActivity)));
        const double m = std::fabs(d);
        const double neutralR = 0.46;
        const double neutralG = 0.48;
        const double neutralB = 0.52;
        if (d >= 0.0)
            return Qwt3D::RGBA(neutralR + (1.0 - neutralR) * m,
                               neutralG * (1.0 - m),
                               neutralB * (1.0 - m), 1.0);
        return Qwt3D::RGBA(neutralR * (1.0 - m),
                           neutralG * (1.0 - 0.35 * m),
                           neutralB + (1.0 - neutralB) * m, 1.0);
    }
    if (state.spike)
        return Qwt3D::RGBA(1.00, 0.98, 0.72, 1.0);
    if (state.selected)
        return Qwt3D::RGBA(0.88, 0.42, 0.96, 1.0);

    const Qwt3D::RGBA base = structuralColor(ownerId);
    const double a = std::max(0.0, std::min(1.0, static_cast<double>(state.activity)));
    if (a <= 0.001)
        return base;

    // Replay is an overlay, not another palette. Preserve neuron identity at
    // low activity, then progressively warm it toward amber/yellow. This keeps
    // the structure readable while activity remains visually unambiguous.
    const Qwt3D::RGBA amber(0.98, 0.58, 0.16, 1.0);
    const Qwt3D::RGBA hot(1.00, 0.94, 0.38, 1.0);
    const Qwt3D::RGBA active = mixColor(amber, hot, std::max(0.0, (a - 0.55) / 0.45));
    const double overlay = 0.10 + 0.90 * a;
    return mixColor(base, active, overlay);
}
