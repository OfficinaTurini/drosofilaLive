#include "TimelinePlotController.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_zoomer.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_magnifier.h>
#include <qwt_picker.h>

#include <QColor>
#include <QPen>
#include <QPointF>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace DrosofilaLive
{
TimelinePlotController::TimelinePlotController(QwtPlot* plot)
    : plot_(plot),
      grid_(0),
      referenceCurve_(0),
      perturbedCurve_(0),
      deltaCurve_(0),
      cursorMarker_(0),
      onsetMarker_(0),
      zoomer_(0),
      panner_(0),
      magnifier_(0),
      rootId_(0)
{
    if (!plot_)
        return;

    plot_->setTitle(QStringLiteral("Temporal activity"));
    plot_->setCanvasBackground(QColor(248, 249, 251));
    plot_->setAxisTitle(QwtPlot::xBottom, QStringLiteral("Biological time (ms)"));
    plot_->setAxisTitle(QwtPlot::yLeft, QStringLiteral("Firing rate (Hz)"));
    plot_->setAxisTitle(QwtPlot::yRight, QStringLiteral("Delta rate (Hz)"));
    plot_->enableAxis(QwtPlot::yRight, false);
    plot_->setAxisScale(QwtPlot::xBottom, 0.0, 1000.0);
    plot_->setAxisScale(QwtPlot::yLeft, 0.0, 10.0);
    if (plot_->canvas())
        plot_->canvas()->setMouseTracking(true);

    grid_ = new QwtPlotGrid();
    grid_->enableX(true);
    grid_->enableY(true);
    grid_->setMajorPen(QPen(QColor(210, 215, 222), 0, Qt::DotLine));
    grid_->attach(plot_);

    referenceCurve_ = new QwtPlotCurve(QStringLiteral("Reference"));
    referenceCurve_->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    referenceCurve_->setPen(QPen(QColor(31, 119, 180), 2));
    referenceCurve_->attach(plot_);

    perturbedCurve_ = new QwtPlotCurve(QStringLiteral("Perturbed"));
    perturbedCurve_->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    perturbedCurve_->setPen(QPen(QColor(230, 126, 34), 2));
    perturbedCurve_->attach(plot_);

    deltaCurve_ = new QwtPlotCurve(QStringLiteral("Delta"));
    deltaCurve_->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    QPen deltaPen(QColor(39, 174, 96), 2);
    deltaPen.setStyle(Qt::DashLine);
    deltaCurve_->setPen(deltaPen);
    deltaCurve_->setYAxis(QwtPlot::yRight);
    deltaCurve_->attach(plot_);

    cursorMarker_ = new QwtPlotMarker();
    cursorMarker_->setLineStyle(QwtPlotMarker::VLine);
    cursorMarker_->setLinePen(QPen(QColor(55, 65, 81), 1));
    cursorMarker_->setXValue(0.0);
    cursorMarker_->attach(plot_);

    onsetMarker_ = new QwtPlotMarker();
    onsetMarker_->setLineStyle(QwtPlotMarker::VLine);
    QPen onsetPen(QColor(142, 68, 173), 2);
    onsetPen.setStyle(Qt::DashLine);
    onsetMarker_->setLinePen(onsetPen);
    onsetMarker_->setVisible(false);
    onsetMarker_->attach(plot_);

    if (plot_->canvas())
    {
        // QwtPlotZoomer also provides the always-on coordinate tracker.
        // Left drag: rectangle zoom. Right click: zoom out. Ctrl+right: zoom home
        // with the default QwtPlotZoomer mouse patterns.
        zoomer_ = new QwtPlotZoomer(plot_->canvas());
        zoomer_->setRubberBand(QwtPicker::RectRubberBand);
        zoomer_->setRubberBandPen(QPen(QColor(52, 73, 94), 1, Qt::DashLine));
        zoomer_->setTrackerMode(QwtPicker::AlwaysOn);
        zoomer_->setTrackerPen(QPen(QColor(33, 47, 61)));

        // Middle-button panning and wheel magnification make the plot usable
        // without adding more GUI chrome.
        panner_ = new QwtPlotPanner(plot_->canvas());
        panner_->setMouseButton(Qt::MiddleButton);

        magnifier_ = new QwtPlotMagnifier(plot_->canvas());
    }

    plot_->replot();
    if (zoomer_) zoomer_->setZoomBase();
}

TimelinePlotController::~TimelinePlotController()
{
    delete magnifier_;
    magnifier_ = 0;
    delete panner_;
    panner_ = 0;
    delete zoomer_;
    zoomer_ = 0;

    if (onsetMarker_)
    {
        onsetMarker_->detach();
        delete onsetMarker_;
    }
    if (cursorMarker_)
    {
        cursorMarker_->detach();
        delete cursorMarker_;
    }
    if (deltaCurve_)
    {
        deltaCurve_->detach();
        delete deltaCurve_;
    }
    if (perturbedCurve_)
    {
        perturbedCurve_->detach();
        delete perturbedCurve_;
    }
    if (referenceCurve_)
    {
        referenceCurve_->detach();
        delete referenceCurve_;
    }
    if (grid_)
    {
        grid_->detach();
        delete grid_;
    }
}

void TimelinePlotController::clear()
{
    result_ = ExperimentResult();
    comparison_ = ComparisonResult();
    rootId_ = 0;
    label_.clear();
    if (!plot_)
        return;

    QVector<QPointF> empty;
    referenceCurve_->setSamples(empty);
    perturbedCurve_->setSamples(empty);
    deltaCurve_->setSamples(empty);
    cursorMarker_->setXValue(0.0);
    onsetMarker_->setVisible(false);
    plot_->enableAxis(QwtPlot::yRight, false);
    plot_->setTitle(QStringLiteral("Temporal activity"));
    plot_->setAxisScale(QwtPlot::xBottom, 0.0, 1000.0);
    plot_->setAxisScale(QwtPlot::yLeft, 0.0, 10.0);
    plot_->replot();
    if (zoomer_) zoomer_->setZoomBase();
}

void TimelinePlotController::setResult(const ExperimentResult& result)
{
    comparison_ = ComparisonResult();
    result_ = result;
    rebuild();
}

void TimelinePlotController::setComparison(const ComparisonResult& comparison)
{
    result_ = ExperimentResult();
    comparison_ = comparison;
    rebuild();
}

void TimelinePlotController::setNeuron(std::uint64_t rootId, const QString& label)
{
    rootId_ = rootId;
    label_ = label;
    rebuild();
}

void TimelinePlotController::setCursorTime(double ms)
{
    if (!plot_ || !cursorMarker_)
        return;

    double maxTime = 1000.0;
    if (hasComparison())
        maxTime = std::max(1.0, comparison_.reference.durationMs);
    else if (hasResult())
        maxTime = std::max(1.0, result_.durationMs);

    const double clamped = std::max(0.0, std::min(maxTime, ms));
    cursorMarker_->setXValue(clamped);
    plot_->replot();
}

bool TimelinePlotController::hasResult() const
{
    return result_.valid && !result_.frames.isEmpty();
}

bool TimelinePlotController::hasComparison() const
{
    return comparison_.valid && comparison_.reference.valid && comparison_.perturbed.valid &&
           !comparison_.reference.frames.isEmpty() && !comparison_.perturbed.frames.isEmpty();
}

bool TimelinePlotController::hasNeuron(std::uint64_t rootId) const
{
    if (!rootId)
        return false;

    const QVector<std::uint64_t>* roots = 0;
    if (hasComparison())
        roots = &comparison_.reference.watchedRootIds;
    else if (hasResult())
        roots = &result_.watchedRootIds;
    else
        return false;

    for (int i = 0; i < roots->size(); ++i)
        if (roots->at(i) == rootId)
            return true;
    return false;
}

bool TimelinePlotController::frameRateForRoot(const ExperimentFrame& frame, std::uint64_t rootId, double* rate)
{
    for (int i = 0; i < frame.neurons.size(); ++i)
    {
        if (frame.neurons.at(i).rootId == rootId)
        {
            if (rate) *rate = frame.neurons.at(i).rateHz;
            return true;
        }
    }
    return false;
}

void TimelinePlotController::rebuild()
{
    if (!plot_ || !referenceCurve_ || !perturbedCurve_ || !deltaCurve_)
        return;

    QVector<QPointF> referenceSamples;
    QVector<QPointF> perturbedSamples;
    QVector<QPointF> deltaSamples;
    double maxRate = 0.0;
    double maxAbsDelta = 0.0;

    if (hasComparison() && rootId_ && hasNeuron(rootId_))
    {
        const int frames = std::min(comparison_.reference.frames.size(), comparison_.perturbed.frames.size());
        referenceSamples.reserve(frames);
        perturbedSamples.reserve(frames);
        deltaSamples.reserve(frames);
        for (int f = 0; f < frames; ++f)
        {
            const ExperimentFrame& rf = comparison_.reference.frames.at(f);
            const ExperimentFrame& pf = comparison_.perturbed.frames.at(f);
            double rr = 0.0;
            double pr = 0.0;
            if (!frameRateForRoot(rf, rootId_, &rr) || !frameRateForRoot(pf, rootId_, &pr))
                continue;

            const double x = 0.5 * (rf.startMs + rf.endMs);
            referenceSamples.append(QPointF(x, rr));
            perturbedSamples.append(QPointF(x, pr));
            deltaSamples.append(QPointF(x, pr - rr));
            maxRate = std::max(maxRate, std::max(rr, pr));
            maxAbsDelta = std::max(maxAbsDelta, std::fabs(pr - rr));
        }
    }
    else if (hasResult() && rootId_ && hasNeuron(rootId_))
    {
        referenceSamples.reserve(result_.frames.size());
        for (int f = 0; f < result_.frames.size(); ++f)
        {
            const ExperimentFrame& frame = result_.frames.at(f);
            double rate = 0.0;
            if (!frameRateForRoot(frame, rootId_, &rate))
                continue;
            const double x = 0.5 * (frame.startMs + frame.endMs);
            referenceSamples.append(QPointF(x, rate));
            maxRate = std::max(maxRate, rate);
        }
    }

    referenceCurve_->setSamples(referenceSamples);
    perturbedCurve_->setSamples(perturbedSamples);
    deltaCurve_->setSamples(deltaSamples);
    perturbedCurve_->setVisible(hasComparison());
    deltaCurve_->setVisible(hasComparison());
    plot_->enableAxis(QwtPlot::yRight, hasComparison());

    double duration = 1000.0;
    if (hasComparison())
        duration = comparison_.reference.durationMs;
    else if (hasResult())
        duration = result_.durationMs;
    plot_->setAxisScale(QwtPlot::xBottom, 0.0, std::max(1.0, duration));
    plot_->setAxisScale(QwtPlot::yLeft, 0.0, std::max(10.0, maxRate * 1.15));
    if (hasComparison())
    {
        const double d = std::max(10.0, maxAbsDelta * 1.20);
        plot_->setAxisScale(QwtPlot::yRight, -d, d);
    }

    if ((hasResult() || hasComparison()) && rootId_)
    {
        const QString prefix = hasComparison() ? QStringLiteral("Causal comparison") : QStringLiteral("Temporal activity");
        plot_->setTitle(label_.isEmpty() ? prefix : QStringLiteral("%1 - %2").arg(prefix).arg(label_));
    }
    else
        plot_->setTitle(QStringLiteral("Temporal activity"));

    if (onsetMarker_)
    {
        const bool showOnset = hasComparison() && comparison_.sustainedOnsetMs >= 0.0;
        onsetMarker_->setVisible(showOnset);
        if (showOnset)
            onsetMarker_->setXValue(comparison_.sustainedOnsetMs);
    }

    if (cursorMarker_)
        cursorMarker_->setXValue(0.0);
    plot_->replot();
    if (zoomer_) zoomer_->setZoomBase();
}
}
