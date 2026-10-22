#include "CircuitGraphController.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtGlobal>
#include <QPen>
#include <QPainter>
#include <QPointF>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace DrosofilaLive
{
namespace
{
QString shortRoot(std::uint64_t rootId)
{
    const QString s = QString::number(static_cast<qulonglong>(rootId));
    return s.size() > 8 ? QStringLiteral("...") + s.right(8) : s;
}
}

CircuitGraphController::CircuitGraphController(QGraphicsView* view, QObject* parent)
    : QObject(parent)
    , view_(view)
    , scene_(new QGraphicsScene(this))
{
    if (view_)
    {
        view_->setScene(scene_);
        view_->setRenderHint(QPainter::Antialiasing, true);
        view_->setDragMode(QGraphicsView::ScrollHandDrag);
        view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        view_->setResizeAnchor(QGraphicsView::AnchorViewCenter);
        view_->viewport()->installEventFilter(this);
    }
    connect(scene_, SIGNAL(selectionChanged()), this, SLOT(sceneSelectionChanged()));
    publishZoom();
}

CircuitGraphController::~CircuitGraphController() = default;

void CircuitGraphController::clear()
{
    rootByItem_.clear();
    itemByRoot_.clear();
    if (scene_)
        scene_->clear();
}

QString CircuitGraphController::displayName(std::uint64_t rootId, const CircuitExploreResult& result) const
{
    if (rootId == result.rootId)
        return result.rootType.isEmpty() ? shortRoot(rootId) : result.rootType;
    for (int i = 0; i < result.candidates.size(); ++i)
    {
        const CircuitExploreCandidate& c = result.candidates.at(i);
        if (c.rootId == rootId)
            return c.primaryType.isEmpty() ? shortRoot(rootId) : c.primaryType;
    }
    return shortRoot(rootId);
}

void CircuitGraphController::setResult(const CircuitExploreResult& result)
{
    clear();
    if (!scene_ || !view_ || !result.valid)
        return;

    QHash<std::uint64_t, QPointF> positions;
    QHash<int, QVector<const CircuitExploreCandidate*> > upstreamByDepth;
    QHash<int, QVector<const CircuitExploreCandidate*> > downstreamByDepth;
    QVector<const CircuitExploreCandidate*> bothSide;

    for (int i = 0; i < result.candidates.size(); ++i)
    {
        const CircuitExploreCandidate* c = &result.candidates.at(i);
        if (c->upstream && c->downstream)
            bothSide.append(c);
        else if (c->upstream)
            upstreamByDepth[c->depth].append(c);
        else
            downstreamByDepth[c->depth].append(c);
    }

    const double xStep = 190.0;
    const double yStep = 58.0;
    positions.insert(result.rootId, QPointF(0.0, 0.0));

    const auto placeColumn = [&](const QHash<int, QVector<const CircuitExploreCandidate*> >& byDepth, double sign)
    {
        for (QHash<int, QVector<const CircuitExploreCandidate*> >::const_iterator it = byDepth.constBegin(); it != byDepth.constEnd(); ++it)
        {
            QVector<const CircuitExploreCandidate*> list = it.value();
            std::sort(list.begin(), list.end(), [](const CircuitExploreCandidate* a, const CircuitExploreCandidate* b)
            {
                if (a->score != b->score) return a->score > b->score;
                return a->rootId < b->rootId;
            });
            const double y0 = -0.5 * static_cast<double>(list.size() - 1) * yStep;
            for (int i = 0; i < list.size(); ++i)
                positions.insert(list.at(i)->rootId, QPointF(sign * xStep * it.key(), y0 + i * yStep));
        }
    };

    placeColumn(upstreamByDepth, -1.0);
    placeColumn(downstreamByDepth, 1.0);

    for (int i = 0; i < bothSide.size(); ++i)
        positions.insert(bothSide.at(i)->rootId, QPointF(0.0, 95.0 + i * yStep));

    // Edges first, so nodes stay readable on top.
    for (int i = 0; i < result.edges.size(); ++i)
    {
        const CircuitExploreEdge& e = result.edges.at(i);
        if (!positions.contains(e.sourceRootId) || !positions.contains(e.targetRootId))
            continue;
        const QPointF a = positions.value(e.sourceRootId);
        const QPointF b = positions.value(e.targetRootId);
        const int magnitude = std::abs(static_cast<int>(e.weight));
        QPen pen(e.weight >= 0 ? QColor(80, 80, 80) : QColor(100, 100, 100));
        pen.setWidthF(0.8 + std::min(4.0, std::log1p(static_cast<double>(magnitude)) / 2.2));
        scene_->addLine(QLineF(a, b), pen);

        const QLineF line(a, b);
        if (line.length() > 1.0)
        {
            const QPointF center = line.pointAt(0.72);
            const double ux = (b.x() - a.x()) / line.length();
            const double uy = (b.y() - a.y()) / line.length();
            const QPointF back(center.x() - ux * 10.0, center.y() - uy * 10.0);
            const QPointF normal(-uy * 4.0, ux * 4.0);
            QPolygonF arrow;
            arrow << center << (back + normal) << (back - normal);
            scene_->addPolygon(arrow, QPen(pen.color()), QBrush(pen.color()));
        }

        if (magnitude >= std::max(10, result.minimumSynapses * 2))
        {
            QGraphicsSimpleTextItem* label = scene_->addSimpleText(QString::number(magnitude));
            label->setBrush(QBrush(QColor(90, 90, 90)));
            label->setPos((a.x() + b.x()) * 0.5 + 3.0, (a.y() + b.y()) * 0.5 - 10.0);
        }
    }

    const auto addNode = [&](std::uint64_t rootId, const QPointF& center, bool rootNode, double score)
    {
        const double w = rootNode ? 128.0 : 118.0;
        const double h = rootNode ? 42.0 : 36.0;
        QGraphicsRectItem* box = scene_->addRect(-w * 0.5, -h * 0.5, w, h,
                                                 QPen(rootNode ? QColor(40, 40, 40) : QColor(90, 90, 90)),
                                                 QBrush(rootNode ? QColor(235, 235, 235) : QColor(250, 250, 250)));
        box->setPos(center);
        box->setFlag(QGraphicsItem::ItemIsSelectable, true);
        box->setToolTip(QStringLiteral("%1\nroot %2\nscore %3")
                        .arg(displayName(rootId, result))
                        .arg(static_cast<qulonglong>(rootId))
                        .arg(score, 0, 'f', 3));
        rootByItem_.insert(box, rootId);
        itemByRoot_.insert(rootId, box);

        QGraphicsSimpleTextItem* label = scene_->addSimpleText(displayName(rootId, result));
        QFont font = label->font();
        font.setBold(rootNode);
        font.setPointSizeF(rootNode ? 9.5 : 8.5);
        label->setFont(font);
        const QRectF br = label->boundingRect();
        label->setPos(center.x() - br.width() * 0.5, center.y() - br.height() * 0.5);
        label->setAcceptedMouseButtons(Qt::NoButton);
        rootByItem_.insert(label, rootId);
    };

    addNode(result.rootId, positions.value(result.rootId), true, 1.0);
    for (int i = 0; i < result.candidates.size(); ++i)
    {
        const CircuitExploreCandidate& c = result.candidates.at(i);
        if (positions.contains(c.rootId))
            addNode(c.rootId, positions.value(c.rootId), false, c.score);
    }

    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-40, -40, 40, 40));
    fitAll();
}


void CircuitGraphController::applyZoom(double factor)
{
    if (!view_ || factor <= 0.0)
        return;

    const double current = std::abs(view_->transform().m11());
    if (current <= 0.0)
        return;

    const double minimum = 0.02;
    const double maximum = 8.0;
    const double requested = current * factor;
    const double target = std::max(minimum, std::min(maximum, requested));
    const double actual = target / current;
    if (std::abs(actual - 1.0) < 1e-9)
        return;

    view_->scale(actual, actual);
    publishZoom();
}

void CircuitGraphController::zoomIn()
{
    applyZoom(1.25);
}

void CircuitGraphController::zoomOut()
{
    applyZoom(0.8);
}

void CircuitGraphController::zoom100()
{
    if (!view_)
        return;
    const QPointF center = view_->mapToScene(view_->viewport()->rect().center());
    view_->resetTransform();
    view_->centerOn(center);
    publishZoom();
}

void CircuitGraphController::fitAll()
{
    if (!view_ || !scene_ || scene_->sceneRect().isEmpty())
        return;
    view_->resetTransform();
    view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
    publishZoom();
}

void CircuitGraphController::publishZoom()
{
    if (!view_)
        return;
    const int percent = qRound(std::abs(view_->transform().m11()) * 100.0);
    emit zoomChanged(percent);
}

bool CircuitGraphController::eventFilter(QObject* watched, QEvent* event)
{
    if (view_ && watched == view_->viewport())
    {
        if (event->type() == QEvent::Wheel)
        {
            QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
            const int delta = wheel->angleDelta().y();
            if (delta != 0)
            {
                applyZoom(std::pow(1.20, static_cast<double>(delta) / 120.0));
                wheel->accept();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonDblClick)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton)
            {
                QGraphicsItem* item = view_->itemAt(mouse->pos());
                if (item && rootByItem_.contains(item))
                {
                    const std::uint64_t rootId = rootByItem_.value(item, 0);
                    QGraphicsItem* node = itemByRoot_.value(static_cast<quint64>(rootId), 0);
                    if (node)
                    {
                        scene_->clearSelection();
                        node->setSelected(true);
                        view_->centerOn(node);
                    }
                }
                else
                {
                    view_->centerOn(view_->mapToScene(mouse->pos()));
                }
                mouse->accept();
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void CircuitGraphController::selectRoot(std::uint64_t rootId)
{
    if (!scene_)
        return;
    scene_->clearSelection();
    QGraphicsItem* item = itemByRoot_.value(rootId, 0);
    if (item)
    {
        item->setSelected(true);
        view_->centerOn(item);
    }
}

void CircuitGraphController::sceneSelectionChanged()
{
    if (!scene_)
        return;
    const QList<QGraphicsItem*> selected = scene_->selectedItems();
    if (selected.isEmpty())
        return;
    const std::uint64_t rootId = rootByItem_.value(selected.first(), 0);
    if (rootId != 0)
        emit neuronSelected(static_cast<qulonglong>(rootId));
}

}
