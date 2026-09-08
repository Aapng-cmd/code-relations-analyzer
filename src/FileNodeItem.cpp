#include "FileNodeItem.h"

#include "I18n.h"
#include "Theme.h"
#include "UiConfig.h"

#include <QCursor>
#include <QEasingCurve>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

FileNodeItem::FileNodeItem(const FileNode &file, QGraphicsItem *parent)
    : QGraphicsRectItem(0, 0, UiConfig::get().nodeWidth, UiConfig::get().nodeHeight, parent)
    , m_file(file)
{
    setFlag(ItemIsMovable, true);
    setFlag(ItemIsSelectable, true);
    setFlag(ItemSendsGeometryChanges, true);
    setFlag(ItemIsFocusable, true);
    setBrush(Qt::NoBrush);
    setPen(Qt::NoPen);
    setZValue(1);
    setCursor(QCursor(Qt::OpenHandCursor));
    m_fade = UiConfig::get().visibleOpacity;
    setOpacity(m_fade);
}

QPointF FileNodeItem::center() const
{
    return mapToScene(rect().center());
}

QRectF FileNodeItem::eyeRect() const
{
    const UiConfig &u = UiConfig::get();
    return QRectF(rect().right() - u.eyeSize - u.eyeMargin, rect().top() + u.eyeMargin, u.eyeSize, u.eyeSize);
}

void FileNodeItem::setEyeClose(qreal value)
{
    m_eyeClose = qBound(0.0, value, 1.0);
    update();
}

void FileNodeItem::setFade(qreal value)
{
    m_fade = value;
    setOpacity(value);
    update();
}

void FileNodeItem::toggleHidden()
{
    m_graphVisible = !m_graphVisible;
    const UiConfig &u = UiConfig::get();
    const qreal eyeTo = m_graphVisible ? 0.0 : 1.0;
    const qreal fadeTo = m_graphVisible ? u.visibleOpacity : u.hiddenOpacity;

    if (m_anim)
        m_anim->stop();
    m_anim = new QParallelAnimationGroup(this);

    auto *eyeAnim = new QPropertyAnimation(this, "eyeClose", m_anim);
    eyeAnim->setDuration(u.eyeAnimationMs);
    eyeAnim->setStartValue(m_eyeClose);
    eyeAnim->setEndValue(eyeTo);
    eyeAnim->setEasingCurve(QEasingCurve::InOutQuad);

    auto *fadeAnim = new QPropertyAnimation(this, "fade", m_anim);
    fadeAnim->setDuration(u.eyeAnimationMs);
    fadeAnim->setStartValue(m_fade);
    fadeAnim->setEndValue(fadeTo);
    fadeAnim->setEasingCurve(QEasingCurve::InOutQuad);

    m_anim->start(QAbstractAnimation::DeleteWhenStopped);
    connect(m_anim, &QParallelAnimationGroup::finished, this, [this]() { m_anim = nullptr; });
    emit visibilityToggled();
}

void FileNodeItem::paintEye(QPainter *painter) const
{
    const QRectF r = eyeRect();
    const ThemeColors c = Theme::colors();
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    const qreal rx = r.width() * 0.42;
    const qreal ry = r.height() * 0.28;
    const qreal close = m_eyeClose;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(c.nodeMuted, 1.3);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const qreal openRy = ry * (1.0 - close);
    QPainterPath eye;
    eye.moveTo(cx - rx, cy);
    eye.quadTo(cx, cy - openRy - 0.5, cx + rx, cy);
    eye.quadTo(cx, cy + openRy + 0.5, cx - rx, cy);
    painter->drawPath(eye);

    if (close < 0.85) {
        const qreal pupil = r.width() * 0.14 * (1.0 - close);
        painter->setBrush(c.nodeText);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(cx, cy), pupil, pupil * (1.0 - close * 0.5));
    }
    if (close > 0.2) {
        painter->setPen(pen);
        painter->drawLine(QPointF(cx - rx, cy), QPointF(cx + rx, cy));
    }
    painter->restore();
}

void FileNodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);
    painter->setRenderHint(QPainter::Antialiasing);
    const bool sel = option->state & QStyle::State_Selected;
    const ThemeColors c = Theme::colors();
    const UiConfig &u = UiConfig::get();
    painter->setPen(QPen(sel ? c.nodeBorderSelected : c.nodeBorder, sel ? 3 : 2));
    painter->setBrush(sel ? c.nodeBgSelected : c.nodeBg);
    painter->drawRoundedRect(rect().adjusted(1, 1, -1, -1), u.nodeCornerRadius, u.nodeCornerRadius);

    painter->setPen(c.nodeText);
    QFont title = painter->font();
    title.setBold(true);
    title.setPointSize(u.nodeTitlePointSize);
    painter->setFont(title);
    const qreal pad = u.nodePadding;
    painter->drawText(rect().adjusted(pad, pad + 4, -pad - u.eyeSize, -pad),
                      Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_file.fileName);

    QFont sub = title;
    sub.setBold(false);
    sub.setPointSize(u.nodeSubtitlePointSize);
    painter->setFont(sub);
    painter->setPen(c.nodeMuted);
    painter->drawText(rect().adjusted(pad, 34, -pad, -pad), Qt::AlignHCenter | Qt::AlignTop,
                      I18n::t(QStringLiteral("node_symbols")).arg(m_file.symbols.size()));
    paintEye(painter);
}

QVariant FileNodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged)
        emit moved();
    return QGraphicsRectItem::itemChange(change, value);
}

void FileNodeItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (eyeRect().contains(event->pos())) {
        toggleHidden();
        event->accept();
        return;
    }
    setCursor(QCursor(Qt::ClosedHandCursor));
    emit selected();
    QGraphicsRectItem::mousePressEvent(event);
}

void FileNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    setCursor(QCursor(Qt::OpenHandCursor));
    QGraphicsRectItem::mouseReleaseEvent(event);
}
