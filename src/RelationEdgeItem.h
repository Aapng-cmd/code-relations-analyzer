#pragma once

#include "Model.h"

#include <QGraphicsPathItem>
#include <QObject>
#include <QPainterPath>

class FileNodeItem;
class QGraphicsSceneHoverEvent;

class RelationEdgeItem : public QObject, public QGraphicsPathItem {
    Q_OBJECT
public:
    RelationEdgeItem(FileNodeItem *from, FileNodeItem *to, const FileRelation &relation,
                     QGraphicsItem *parent = nullptr);

    const FileRelation &relation() const { return m_relation; }
    void setComment(const QString &comment);
    FileNodeItem *fromNode() const { return m_from; }
    FileNodeItem *toNode() const { return m_to; }
    void setCurveOffset(qreal offset) { m_curveOffset = offset; }
    void setLabelsOnDemand(bool on) { m_labelsOnDemand = on; }
    void updatePath();
    void updateVisibility();

signals:
    void clicked(const FileRelation &relation);

protected:
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    void invalidateShape();
    QString labelText() const;

    FileNodeItem *m_from;
    FileNodeItem *m_to;
    FileRelation m_relation;
    qreal m_curveOffset = 40;
    bool m_hovered = false;
    bool m_labelsOnDemand = false;
    mutable bool m_shapeDirty = true;
    mutable QPainterPath m_shape;
};
