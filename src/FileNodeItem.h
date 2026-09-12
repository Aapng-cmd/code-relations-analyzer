#pragma once

#include "Model.h"
#include "Theme.h"

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
    void setTint(const NodeTint &tint);
    QPointF center() const;
    bool isGraphVisible() const { return !m_manualHidden && !m_autoHidden; }
    bool isManuallyHidden() const { return m_manualHidden; }
    bool isPinnedVisible() const { return m_pinnedVisible && !m_manualHidden; }
    void setAutoHidden(bool hidden, bool animate = true);
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
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QRectF eyeVisualRect() const;
    QRectF eyeHitRect() const;
    void animateHiddenState();
    void applyHiddenState(bool animate);
    void applyInteractionState();
    void paintEye(QPainter *painter) const;
    void updateEyeHitItem();
    void updateToolTip();
    QString elidedFileName(const QFont &font, qreal maxWidth) const;

    FileNode m_file;
    NodeTint m_tint;
    bool m_manualHidden = false;
    bool m_autoHidden = false;
    bool m_pinnedVisible = false;
    qreal m_eyeClose = 0;
    qreal m_fade = 1;
    QParallelAnimationGroup *m_anim = nullptr;
    QGraphicsRectItem *m_eyeHit = nullptr;
};
