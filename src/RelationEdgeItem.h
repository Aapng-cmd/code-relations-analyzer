#pragma once

#include "Model.h"

#include <QGraphicsPathItem>
#include <QObject>

class FileNodeItem;

class RelationEdgeItem : public QObject, public QGraphicsPathItem {
    Q_OBJECT
public:
    RelationEdgeItem(FileNodeItem *from, FileNodeItem *to, const FileRelation &relation,
                     QGraphicsItem *parent = nullptr);

    const FileRelation &relation() const { return m_relation; }
    FileNodeItem *fromNode() const { return m_from; }
    FileNodeItem *toNode() const { return m_to; }
    void updatePath();
    void updateVisibility();

signals:
    void clicked(const FileRelation &relation);

protected:
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
    FileNodeItem *m_from;
    FileNodeItem *m_to;
    FileRelation m_relation;
};
