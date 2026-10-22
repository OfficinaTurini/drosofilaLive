#include "StatisticsPlotController.h"

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
StatisticsPlotController::StatisticsPlotController(QwtPlot* plot)
    : plot_(plot), grid_(0), interactionCurve_(0), zeroMarker_(0), meanMarker_(0),
      ciLowMarker_(0), ciHighMarker_(0), zoomer_(0), panner_(0), magnifier_(0)
{
    if (!plot_) return;

    plot_->setTitle(QStringLiteral("Epistasis replicate distribution"));
    plot_->setCanvasBackground(QColor(248, 249, 251));
    plot_->setAxisTitle(QwtPlot::xBottom, QStringLiteral("Replicate"));
    plot_->setAxisTitle(QwtPlot::yLeft, QStringLiteral("Interaction (Hz)"));
    plot_->setAxisScale(QwtPlot::xBottom, 1.0, 2.0);
    plot_->setAxisScale(QwtPlot::yLeft, -10.0, 10.0);
    if (plot_->canvas())
        plot_->canvas()->setMouseTracking(true);

    grid_ = new QwtPlotGrid();
    grid_->enableX(true);
    grid_->enableY(true);
    grid_->setMajorPen(QPen(QColor(210, 215, 222), 0, Qt::DotLine));
    grid_->attach(plot_);

    interactionCurve_ = new QwtPlotCurve(QStringLiteral("Interaction per replicate"));
    interactionCurve_->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    interactionCurve_->setPen(QPen(QColor(31, 119, 180), 2));
    interactionCurve_->attach(plot_);

    zeroMarker_ = new QwtPlotMarker();
    zeroMarker_->setLineStyle(QwtPlotMarker::HLine);
    zeroMarker_->setYValue(0.0);
    zeroMarker_->setLinePen(QPen(QColor(90, 98, 108), 1));
    zeroMarker_->attach(plot_);

    meanMarker_ = new QwtPlotMarker();
    meanMarker_->setLineStyle(QwtPlotMarker::HLine);
    meanMarker_->setLinePen(QPen(QColor(230, 126, 34), 2));
    meanMarker_->setVisible(false);
    meanMarker_->attach(plot_);

    ciLowMarker_ = new QwtPlotMarker();
    ciLowMarker_->setLineStyle(QwtPlotMarker::HLine);
    QPen ciPen(QColor(39, 174, 96), 2);
    ciPen.setStyle(Qt::DashLine);
    ciLowMarker_->setLinePen(ciPen);
    ciLowMarker_->setVisible(false);
    ciLowMarker_->attach(plot_);

    ciHighMarker_ = new QwtPlotMarker();
    ciHighMarker_->setLineStyle(QwtPlotMarker::HLine);
    ciHighMarker_->setLinePen(ciPen);
    ciHighMarker_->setVisible(false);
    ciHighMarker_->attach(plot_);

    if (plot_->canvas())
    {
        zoomer_ = new QwtPlotZoomer(plot_->canvas());
        zoomer_->setRubberBand(QwtPicker::RectRubberBand);
        zoomer_->setRubberBandPen(QPen(QColor(52, 73, 94), 1, Qt::DashLine));
        zoomer_->setTrackerMode(QwtPicker::AlwaysOn);
        zoomer_->setTrackerPen(QPen(QColor(33, 47, 61)));

        panner_ = new QwtPlotPanner(plot_->canvas());
        panner_->setMouseButton(Qt::MiddleButton);

        magnifier_ = new QwtPlotMagnifier(plot_->canvas());
    }

    plot_->replot();
    if (zoomer_) zoomer_->setZoomBase();
}

StatisticsPlotController::~StatisticsPlotController()
{
    delete magnifier_;
    magnifier_ = 0;
    delete panner_;
    panner_ = 0;
    delete zoomer_;
    zoomer_ = 0;

    if (ciHighMarker_) { ciHighMarker_->detach(); delete ciHighMarker_; }
    if (ciLowMarker_) { ciLowMarker_->detach(); delete ciLowMarker_; }
    if (meanMarker_) { meanMarker_->detach(); delete meanMarker_; }
    if (zeroMarker_) { zeroMarker_->detach(); delete zeroMarker_; }
    if (interactionCurve_) { interactionCurve_->detach(); delete interactionCurve_; }
    if (grid_) { grid_->detach(); delete grid_; }
}

void StatisticsPlotController::clear()
{
    if (!plot_) return;
    interactionCurve_->setSamples(QVector<QPointF>());
    meanMarker_->setVisible(false);
    ciLowMarker_->setVisible(false);
    ciHighMarker_->setVisible(false);
    plot_->setTitle(QStringLiteral("Epistasis replicate distribution"));
    plot_->setAxisScale(QwtPlot::xBottom, 1.0, 2.0);
    plot_->setAxisScale(QwtPlot::yLeft, -10.0, 10.0);
    plot_->replot();
    if (zoomer_) zoomer_->setZoomBase();
}

void StatisticsPlotController::setResult(const StatisticalEpistasisResult& result)
{
    if (!plot_ || !result.valid || result.replicates.isEmpty())
    {
        clear();
        return;
    }

    QVector<QPointF> samples;
    samples.reserve(result.replicates.size());
    double minY = 0.0;
    double maxY = 0.0;
    for (int i = 0; i < result.replicates.size(); ++i)
    {
        const double y = result.replicates.at(i).interactionHz;
        samples.append(QPointF(i + 1, y));
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    interactionCurve_->setSamples(samples);

    meanMarker_->setYValue(result.interaction.mean);
    meanMarker_->setVisible(true);
    minY = std::min(minY, result.interaction.mean);
    maxY = std::max(maxY, result.interaction.mean);

    const bool ci = result.interaction.uncertaintyAvailable;
    ciLowMarker_->setVisible(ci);
    ciHighMarker_->setVisible(ci);
    if (ci)
    {
        ciLowMarker_->setYValue(result.interaction.ci95Low);
        ciHighMarker_->setYValue(result.interaction.ci95High);
        minY = std::min(minY, result.interaction.ci95Low);
        maxY = std::max(maxY, result.interaction.ci95High);
    }

    const double span = std::max(5.0, maxY - minY);
    const double pad = 0.15 * span;
    plot_->setAxisScale(QwtPlot::xBottom, 1.0, std::max(2, result.replicates.size()));
    plot_->setAxisScale(QwtPlot::yLeft, minY - pad, maxY + pad);
    plot_->setTitle(QStringLiteral("%1 x %2 - interaction replicates")
                        .arg(result.typeA, result.typeB));
    plot_->replot();
    if (zoomer_) zoomer_->setZoomBase();
}
}
