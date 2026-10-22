#pragma once

#include "CausalStructureAnalysis.h"

#include <QObject>
#include <QVector>
#include <QPoint>
#include <QPointF>

class QwtPlot;
class QwtPlotCurve;
class QwtPlotGrid;
class QwtPlotMarker;
class QwtPlotZoomer;
class QwtPlotPanner;
class QwtPlotMagnifier;

namespace DrosofilaLive
{
class CausalSurprisePlotController : public QObject
{
    Q_OBJECT
public:
    explicit CausalSurprisePlotController(QwtPlot* plot, QObject* parent = 0);
    ~CausalSurprisePlotController() override;

    void clear();
    void setResult(const CausalStructureAnalysisResult& result);
    void selectRoot(std::uint64_t rootId);

signals:
    void rootSelected(qulonglong rootId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void appendPoint(QwtPlotCurve* curve, double x, double y, std::uint64_t rootId,
                     QVector<QPointF>* samples, QVector<std::uint64_t>* roots);
    int nearestPoint(const QPoint& canvasPos, double maximumPixels) const;

    QwtPlot* plot_;
    QwtPlotGrid* grid_;
    QwtPlotCurve* surpriseCurve_;
    QwtPlotCurve* strongCurve_;
    QwtPlotCurve* overestimateCurve_;
    QwtPlotCurve* weakCurve_;
    QwtPlotCurve* nonsignificantCurve_;
    QwtPlotCurve* selectedCurve_;
    QwtPlotMarker* verticalMarker_;
    QwtPlotMarker* horizontalMarker_;
    QwtPlotZoomer* zoomer_;
    QwtPlotPanner* panner_;
    QwtPlotMagnifier* magnifier_;
    QVector<QPointF> points_;
    QVector<std::uint64_t> roots_;
};
}
