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
    void setComment(const QString &comment);
    QPointF center() const;
    bool isGraphVisible() const { return !m_manualHidden && !m_autoHidden; }
    bool isManuallyHidden() const { return m_manualHidden; }
    void setAutoHidden(bool hidden);
    void toggleHidden();
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
    bool contains(const QPointF &point) const override;

private:
    QRectF eyeVisualRect() const;
    QRectF eyeHitRect() const;
    void animateHiddenState();
    void applyInteractionState();
    void paintEye(QPainter *painter) const;
    void updateEyeHitItem();

    FileNode m_file;
    bool m_manualHidden = false;
    bool m_autoHidden = false;
    qreal m_eyeClose = 0;
    qreal m_fade = 1;
    QParallelAnimationGroup *m_anim = nullptr;
    QGraphicsRectItem *m_eyeHit = nullptr;
};
