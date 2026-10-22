#pragma once

#include "CircuitExplorer.h"

#include <QObject>
#include <QHash>

#include <cstdint>

class QEvent;
class QGraphicsItem;
class QGraphicsScene;
class QGraphicsView;

namespace DrosofilaLive
{

class CircuitGraphController : public QObject
{
    Q_OBJECT

public:
    explicit CircuitGraphController(QGraphicsView* view, QObject* parent = 0);
    ~CircuitGraphController() override;

    void clear();
    void setResult(const CircuitExploreResult& result);
    void selectRoot(std::uint64_t rootId);

public slots:
    void zoomIn();
    void zoomOut();
    void zoom100();
    void fitAll();

signals:
    void neuronSelected(qulonglong rootId);
    void zoomChanged(int percent);

private slots:
    void sceneSelectionChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyZoom(double factor);
    void publishZoom();
    QString displayName(std::uint64_t rootId, const CircuitExploreResult& result) const;

    QGraphicsView* view_;
    QGraphicsScene* scene_;
    QHash<QGraphicsItem*, std::uint64_t> rootByItem_;
    QHash<quint64, QGraphicsItem*> itemByRoot_;
};

}
