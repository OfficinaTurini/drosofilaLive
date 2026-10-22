#pragma once

#include "../experiment/ExperimentData.h"

class QwtPlot;
class QwtPlotCurve;
class QwtPlotGrid;
class QwtPlotMarker;
class QwtPlotZoomer;
class QwtPlotPanner;
class QwtPlotMagnifier;

namespace DrosofilaLive
{
class StatisticsPlotController
{
public:
    explicit StatisticsPlotController(QwtPlot* plot);
    ~StatisticsPlotController();

    void clear();
    void setResult(const StatisticalEpistasisResult& result);

private:
    QwtPlot* plot_;
    QwtPlotGrid* grid_;
    QwtPlotCurve* interactionCurve_;
    QwtPlotMarker* zeroMarker_;
    QwtPlotMarker* meanMarker_;
    QwtPlotMarker* ciLowMarker_;
    QwtPlotMarker* ciHighMarker_;
    QwtPlotZoomer* zoomer_;
    QwtPlotPanner* panner_;
    QwtPlotMagnifier* magnifier_;
};
}
