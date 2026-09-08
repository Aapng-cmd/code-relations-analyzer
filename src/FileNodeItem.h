#pragma once

#include "Model.h"

#include <QGraphicsRectItem>
#include <QObject>

class QParallelAnimationGroup;

class FileNodeItem : public QObject, public QGraphicsRectItem {
    Q_OBJECT
    Q_PROPERTY(qreal eyeClose READ eyeClose WRITE setEyeClose)
    Q_PROPERTY(qreal fade READ fade WRITE setFade)
public:
    explicit FileNodeItem(const FileNode &file, QGraphicsItem *parent = nullptr);

    const FileNode &file() const { return m_file; }
    QPointF center() const;
    bool isGraphVisible() const { return m_graphVisible; }
    qreal eyeClose() const { return m_eyeClose; }
    void setEyeClose(qreal value);
    qreal fade() const { return m_fade; }
    void setFade(qreal value);

signals:
    void moved();
    void selected();
    void visibilityToggled();

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QRectF eyeRect() const;
    void toggleHidden();
    void paintEye(QPainter *painter) const;

    FileNode m_file;
    bool m_graphVisible = true;
    qreal m_eyeClose = 0;
    qreal m_fade = 1;
    QParallelAnimationGroup *m_anim = nullptr;
};
