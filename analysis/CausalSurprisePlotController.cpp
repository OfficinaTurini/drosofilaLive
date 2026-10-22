#include "CausalSurprisePlotController.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_zoomer.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_magnifier.h>
#include <qwt_picker.h>
#include <qwt_symbol.h>

#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QMouseEvent>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QWidget>

#include <cmath>
#include <limits>

namespace DrosofilaLive
{
namespace
{
QwtPlotCurve* makeScatter(QwtPlot* plot, const QString& title, const QColor& color, QwtSymbol::Style style, int size)
{
    QwtPlotCurve* c = new QwtPlotCurve(title);
    c->setStyle(QwtPlotCurve::NoCurve);
    c->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    c->setSymbol(new QwtSymbol(style, QBrush(color), QPen(color.darker(125), 1), QSize(size, size)));
    c->attach(plot);
    return c;
}
}

CausalSurprisePlotController::CausalSurprisePlotController(QwtPlot* plot, QObject* parent)
    : QObject(parent), plot_(plot), grid_(0), surpriseCurve_(0), strongCurve_(0),
      overestimateCurve_(0), weakCurve_(0), nonsignificantCurve_(0), selectedCurve_(0),
      verticalMarker_(0), horizontalMarker_(0), zoomer_(0), panner_(0), magnifier_(0)
{
    if (!plot_) return;
    plot_->setTitle(QStringLiteral("Graph structure vs validated causal effect"));
    plot_->setCanvasBackground(QColor(250, 250, 249));
    plot_->setAxisTitle(QwtPlot::xBottom, QStringLiteral("Structural percentile"));
    plot_->setAxisTitle(QwtPlot::yLeft, QStringLiteral("Causal percentile |validation delta|"));
    plot_->setAxisScale(QwtPlot::xBottom, 0.0, 1.0);
    plot_->setAxisScale(QwtPlot::yLeft, 0.0, 1.0);

    grid_ = new QwtPlotGrid();
    grid_->enableX(true); grid_->enableY(true);
    grid_->setMajorPen(QPen(QColor(220, 223, 228), 0, Qt::DotLine));
    grid_->attach(plot_);

    surpriseCurve_ = makeScatter(plot_, QStringLiteral("Causal surprise"), QColor(217,119,6), QwtSymbol::Ellipse, 9);
    strongCurve_ = makeScatter(plot_, QStringLiteral("Expected strong"), QColor(37,99,235), QwtSymbol::Ellipse, 8);
    overestimateCurve_ = makeScatter(plot_, QStringLiteral("Structural overestimate"), QColor(147,51,234), QwtSymbol::Ellipse, 8);
    weakCurve_ = makeScatter(plot_, QStringLiteral("Expected weak"), QColor(100,116,139), QwtSymbol::Ellipse, 8);
    nonsignificantCurve_ = makeScatter(plot_, QStringLiteral("Not gate-passed"), QColor(148,163,184), QwtSymbol::XCross, 8);
    selectedCurve_ = new QwtPlotCurve(QStringLiteral("Selected"));
    selectedCurve_->setStyle(QwtPlotCurve::NoCurve);
    selectedCurve_->setSymbol(new QwtSymbol(QwtSymbol::Ellipse, QBrush(Qt::NoBrush), QPen(QColor(15,23,42), 2), QSize(15,15)));
    selectedCurve_->attach(plot_);

    verticalMarker_ = new QwtPlotMarker();
    verticalMarker_->setLineStyle(QwtPlotMarker::VLine);
    verticalMarker_->setXValue(0.5);
    verticalMarker_->setLinePen(QPen(QColor(120,128,140), 1, Qt::DashLine));
    verticalMarker_->attach(plot_);
    horizontalMarker_ = new QwtPlotMarker();
    horizontalMarker_->setLineStyle(QwtPlotMarker::HLine);
    horizontalMarker_->setYValue(0.5);
    horizontalMarker_->setLinePen(QPen(QColor(120,128,140), 1, Qt::DashLine));
    horizontalMarker_->attach(plot_);

    if (plot_->canvas())
    {
        plot_->canvas()->setMouseTracking(true);
        plot_->canvas()->installEventFilter(this);
        zoomer_ = new QwtPlotZoomer(plot_->canvas());
        zoomer_->setRubberBand(QwtPicker::RectRubberBand);
        zoomer_->setRubberBandPen(QPen(QColor(51,65,85), 1, Qt::DashLine));
        zoomer_->setTrackerMode(QwtPicker::AlwaysOn);
        zoomer_->setTrackerPen(QPen(QColor(30,41,59)));
        panner_ = new QwtPlotPanner(plot_->canvas());
        panner_->setMouseButton(Qt::MiddleButton);
        magnifier_ = new QwtPlotMagnifier(plot_->canvas());
    }
    clear();
}

CausalSurprisePlotController::~CausalSurprisePlotController()
{
    if (plot_ && plot_->canvas()) plot_->canvas()->removeEventFilter(this);
    delete magnifier_; magnifier_ = 0;
    delete panner_; panner_ = 0;
    delete zoomer_; zoomer_ = 0;
    if (horizontalMarker_) { horizontalMarker_->detach(); delete horizontalMarker_; }
    if (verticalMarker_) { verticalMarker_->detach(); delete verticalMarker_; }
    if (selectedCurve_) { selectedCurve_->detach(); delete selectedCurve_; }
    if (nonsignificantCurve_) { nonsignificantCurve_->detach(); delete nonsignificantCurve_; }
    if (weakCurve_) { weakCurve_->detach(); delete weakCurve_; }
    if (overestimateCurve_) { overestimateCurve_->detach(); delete overestimateCurve_; }
    if (strongCurve_) { strongCurve_->detach(); delete strongCurve_; }
    if (surpriseCurve_) { surpriseCurve_->detach(); delete surpriseCurve_; }
    if (grid_) { grid_->detach(); delete grid_; }
}

void CausalSurprisePlotController::clear()
{
    points_.clear(); roots_.clear();
    const QVector<QPointF> empty;
    if (surpriseCurve_) surpriseCurve_->setSamples(empty);
    if (strongCurve_) strongCurve_->setSamples(empty);
    if (overestimateCurve_) overestimateCurve_->setSamples(empty);
    if (weakCurve_) weakCurve_->setSamples(empty);
    if (nonsignificantCurve_) nonsignificantCurve_->setSamples(empty);
    if (selectedCurve_) selectedCurve_->setSamples(empty);
    if (plot_)
    {
        plot_->setAxisScale(QwtPlot::xBottom, 0.0, 1.0);
        plot_->setAxisScale(QwtPlot::yLeft, 0.0, 1.0);
        plot_->replot();
        if (zoomer_) zoomer_->setZoomBase();
    }
}

void CausalSurprisePlotController::appendPoint(QwtPlotCurve*, double, double, std::uint64_t,
                                               QVector<QPointF>*, QVector<std::uint64_t>*)
{
    // Kept as a named extension point for future per-neuron labels.  The current
    // implementation batches samples by scientific category in setResult().
}

void CausalSurprisePlotController::setResult(const CausalStructureAnalysisResult& r)
{
    clear();
    if (!plot_ || !r.valid) return;

    QVector<QPointF> surprise, strong, over, weak, nonsig;
    points_.reserve(r.entries.size()); roots_.reserve(r.entries.size());
    for (int i = 0; i < r.entries.size(); ++i)
    {
        const CausalSurpriseEntry& e = r.entries.at(i);
        const QPointF p(e.structuralPercentile, e.causalPercentile);
        points_.append(p); roots_.append(e.rootId);
        if (!e.gatePassed) nonsig.append(p);
        else if (e.quadrant == QStringLiteral("CAUSAL SURPRISE")) surprise.append(p);
        else if (e.quadrant == QStringLiteral("EXPECTED STRONG")) strong.append(p);
        else if (e.quadrant == QStringLiteral("STRUCTURAL OVERESTIMATE")) over.append(p);
        else weak.append(p);
    }
    surpriseCurve_->setSamples(surprise);
    strongCurve_->setSamples(strong);
    overestimateCurve_->setSamples(over);
    weakCurve_->setSamples(weak);
    nonsignificantCurve_->setSamples(nonsig);
    selectedCurve_->setSamples(QVector<QPointF>());
    plot_->setAxisScale(QwtPlot::xBottom, -0.03, 1.03);
    plot_->setAxisScale(QwtPlot::yLeft, -0.03, 1.03);
    plot_->setTitle(QStringLiteral("Graph structure vs causality - %1 validated, %2 gate-passed")
                    .arg(r.validationRows).arg(r.gatePassedRows));
    plot_->replot();
    if (zoomer_) zoomer_->setZoomBase();
}

void CausalSurprisePlotController::selectRoot(std::uint64_t rootId)
{
    if (!selectedCurve_) return;
    for (int i = 0; i < roots_.size(); ++i)
    {
        if (roots_.at(i) == rootId)
        {
            QVector<QPointF> one; one.append(points_.at(i));
            selectedCurve_->setSamples(one);
            if (plot_) plot_->replot();
            return;
        }
    }
    selectedCurve_->setSamples(QVector<QPointF>());
    if (plot_) plot_->replot();
}

int CausalSurprisePlotController::nearestPoint(const QPoint& canvasPos, double maximumPixels) const
{
    if (!plot_ || points_.isEmpty()) return -1;
    int best = -1;
    double bestD2 = maximumPixels * maximumPixels;
    for (int i = 0; i < points_.size(); ++i)
    {
        const double px = plot_->transform(QwtPlot::xBottom, points_.at(i).x());
        const double py = plot_->transform(QwtPlot::yLeft, points_.at(i).y());
        const double dx = px - canvasPos.x();
        const double dy = py - canvasPos.y();
        const double d2 = dx*dx + dy*dy;
        if (d2 <= bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

bool CausalSurprisePlotController::eventFilter(QObject* watched, QEvent* event)
{
    if (plot_ && watched == plot_->canvas() && event && event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            const int index = nearestPoint(mouse->pos(), 12.0);
            if (index >= 0 && index < roots_.size())
                emit rootSelected(static_cast<qulonglong>(roots_.at(index)));
        }
    }
    return QObject::eventFilter(watched, event);
}

}
