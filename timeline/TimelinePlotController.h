#pragma once

#include "../experiment/ExperimentData.h"

#include <QString>
#include <cstdint>

class QwtPlot;
class QwtPlotCurve;
class QwtPlotGrid;
class QwtPlotMarker;
class QwtPlotZoomer;
class QwtPlotPanner;
class QwtPlotMagnifier;

namespace DrosofilaLive
{
class TimelinePlotController
{
public:
    explicit TimelinePlotController(QwtPlot* plot);
    ~TimelinePlotController();

    void clear();
    void setResult(const ExperimentResult& result);
    void setComparison(const ComparisonResult& comparison);
    void setNeuron(std::uint64_t rootId, const QString& label);
    void setCursorTime(double ms);

    bool hasResult() const;
    bool hasComparison() const;
    bool hasNeuron(std::uint64_t rootId) const;

private:
    void rebuild();
    static bool frameRateForRoot(const ExperimentFrame& frame, std::uint64_t rootId, double* rate);

    QwtPlot* plot_;
    QwtPlotGrid* grid_;
    QwtPlotCurve* referenceCurve_;
    QwtPlotCurve* perturbedCurve_;
    QwtPlotCurve* deltaCurve_;
    QwtPlotMarker* cursorMarker_;
    QwtPlotMarker* onsetMarker_;
    QwtPlotZoomer* zoomer_;
    QwtPlotPanner* panner_;
    QwtPlotMagnifier* magnifier_;
    ExperimentResult result_;
    ComparisonResult comparison_;
    std::uint64_t rootId_;
    QString label_;
};
}
