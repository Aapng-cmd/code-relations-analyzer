#include "FileNodeItem.h"

#include "I18n.h"
#include "Theme.h"
#include "UiConfig.h"

#include <QCursor>
#include <QEasingCurve>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtGlobal>
#include <QtMath>

namespace {

class EyeHitItem : public QGraphicsRectItem {
public:
    explicit EyeHitItem(FileNodeItem *owner)
        : QGraphicsRectItem(owner)
        , m_owner(owner)
    {
        setAcceptedMouseButtons(Qt::LeftButton);
        setCursor(QCursor(Qt::PointingHandCursor));
        setZValue(50);
        setBrush(Qt::NoBrush);
        setPen(Qt::NoPen);
        setFlag(QGraphicsItem::ItemIsMovable, false);
        setFlag(QGraphicsItem::ItemIsSelectable, false);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        m_owner->toggleHidden();
        event->accept();
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override { event->accept(); }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override { event->accept(); }

private:
    FileNodeItem *m_owner = nullptr;
};

QString elideStemKeepExt(const QString &name, const QFontMetrics &fm, int maxWidth)
{
    if (maxWidth <= 0)
        return {};
    if (fm.horizontalAdvance(name) <= maxWidth)
        return name;

    const QFileInfo info(name);
    const QString ext = info.suffix();
    const QString stem = info.completeBaseName();
    const QString ellipsis = QStringLiteral("...");
    if (ext.isEmpty() || stem.isEmpty() || stem == name)
        return fm.elidedText(name, Qt::ElideMiddle, maxWidth);

    const QString suffix = ellipsis + ext;
    const int suffixWidth = fm.horizontalAdvance(suffix);
    if (suffixWidth >= maxWidth)
        return fm.elidedText(name, Qt::ElideMiddle, maxWidth);

    const int stemBudget = maxWidth - suffixWidth;
    int lo = 0;
    int hi = stem.size();
    int best = 0;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (fm.horizontalAdvance(stem.left(mid)) <= stemBudget) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (best <= 0)
        return fm.elidedText(name, Qt::ElideMiddle, maxWidth);
    return stem.left(best) + suffix;
}

} // namespace

FileNodeItem::FileNodeItem(const FileNode &file, QGraphicsItem *parent)
    : QGraphicsRectItem(0, 0, UiConfig::get().nodeWidth, UiConfig::get().nodeHeight, parent)
    , m_file(file)
{
    setFlag(ItemIsMovable, true);
    setFlag(ItemIsSelectable, true);
    setFlag(ItemSendsGeometryChanges, true);
    setBrush(Qt::NoBrush);
    setPen(Qt::NoPen);
    setZValue(3);
    setCursor(QCursor(Qt::OpenHandCursor));
    m_tint = Theme::defaultNodeTint();
    m_fade = UiConfig::get().visibleOpacity;
    setCacheMode(QGraphicsItem::NoCache);
    updateToolTip();
    m_eyeHit = new EyeHitItem(this);
    updateEyeHitItem();
}

void FileNodeItem::setComment(const QString &comment)
{
    m_file.comment = comment.trimmed();
    updateToolTip();
    update();
}

void FileNodeItem::updateToolTip()
{
    if (m_file.comment.isEmpty())
        setToolTip(m_file.fileName);
    else
        setToolTip(m_file.fileName + QLatin1Char('\n') + m_file.comment);
}

void FileNodeItem::setTint(const NodeTint &tint)
{
    if (m_tint == tint)
        return;
    m_tint = tint;
    update();
}

QPointF FileNodeItem::center() const
{
    return mapToScene(rect().center());
}

QRectF FileNodeItem::boundingRect() const
{
    return rect().adjusted(-12, -12, 12, 12);
}

QRectF FileNodeItem::eyeVisualRect() const
{
    const UiConfig &u = UiConfig::get();
    return QRectF(rect().right() - u.eyeSize - u.eyeMargin, rect().top() + u.eyeMargin, u.eyeSize, u.eyeSize);
}

QRectF FileNodeItem::eyeHitRect() const
{
    const UiConfig &u = UiConfig::get();
    const QRectF vis = eyeVisualRect();
    const qreal extra = qMax<qreal>(0, (u.eyeHitSize - vis.width()) / 2.0);
    return vis.adjusted(-extra, -extra, extra, extra);
}

void FileNodeItem::updateEyeHitItem()
{
    if (!m_eyeHit)
        return;
    m_eyeHit->setRect(eyeHitRect());
}

void FileNodeItem::setEyeClose(qreal value)
{
    m_eyeClose = qBound(0.0, value, 1.0);
    update();
}

void FileNodeItem::setFade(qreal value)
{
    if (qFuzzyCompare(m_fade, value))
        return;
    m_fade = value;
    update();
}

void FileNodeItem::applyInteractionState()
{
    setVisible(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemIsMovable, true);
    setFlag(ItemIsSelectable, true);
    if (m_autoHidden)
        setZValue(2);
    else if (m_manualHidden)
        setZValue(6);
    else
        setZValue(3);
    if (m_eyeHit) {
        m_eyeHit->setAcceptedMouseButtons(Qt::LeftButton);
        m_eyeHit->setZValue(50);
        m_eyeHit->setVisible(true);
    }
}

void FileNodeItem::applyHiddenState(bool animate)
{
    applyInteractionState();
    const UiConfig &u = UiConfig::get();
    const bool dimmed = m_manualHidden || m_autoHidden;
    const qreal eyeTo = dimmed ? 1.0 : 0.0;
    const qreal fadeTo = dimmed ? u.hiddenOpacity : u.visibleOpacity;
    if (!animate) {
        if (m_anim)
            m_anim->stop();
        m_anim = nullptr;
        m_eyeClose = eyeTo;
        setFade(fadeTo);
        update();
        return;
    }
    animateHiddenState();
}

void FileNodeItem::animateHiddenState()
{
    applyInteractionState();
    const UiConfig &u = UiConfig::get();
    const bool dimmed = m_manualHidden || m_autoHidden;
    const qreal eyeTo = dimmed ? 1.0 : 0.0;
    const qreal fadeTo = dimmed ? u.hiddenOpacity : u.visibleOpacity;

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

    connect(m_anim, &QParallelAnimationGroup::finished, this, [this]() { m_anim = nullptr; });
    m_anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void FileNodeItem::setAutoHidden(bool hidden, bool animate)
{
    if (m_pinnedVisible || m_manualHidden)
        hidden = false;
    if (m_autoHidden == hidden)
        return;
    m_autoHidden = hidden;
    applyHiddenState(animate);
}

void FileNodeItem::toggleHidden()
{
    if (m_manualHidden || m_autoHidden) {
        m_manualHidden = false;
        m_autoHidden = false;
        m_pinnedVisible = true;
    } else {
        m_pinnedVisible = false;
        m_manualHidden = true;
    }
    applyHiddenState(true);
    emit visibilityToggled();
}

QString FileNodeItem::elidedFileName(const QFont &font, qreal maxWidth) const
{
    return elideStemKeepExt(m_file.fileName, QFontMetrics(font), int(maxWidth));
}

void FileNodeItem::paintEye(QPainter *painter) const
{
    const QRectF r = eyeVisualRect();
    const ThemeColors &c = Theme::colors();
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    const qreal rx = r.width() * 0.42;
    const qreal ry = r.height() * 0.28;
    const qreal close = m_eyeClose;

    painter->save();
    QPen pen(c.nodeMuted, 1.6);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const qreal openRy = ry * (1.0 - close);
    QPainterPath eye;
    eye.moveTo(cx - rx, cy);
    eye.quadTo(cx, cy - openRy - 0.5, cx + rx, cy);
    eye.quadTo(cx, cy + openRy + 0.5, cx - rx, cy);
    painter->drawPath(eye);

    if (close < 0.85) {
        const qreal pupil = r.width() * 0.16 * (1.0 - close);
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
    painter->setOpacity(m_fade);
    const bool sel = option->state & QStyle::State_Selected;
    const ThemeColors &c = Theme::colors();
    const UiConfig &u = UiConfig::get();
    painter->setPen(QPen(sel ? c.nodeBorderSelected : m_tint.border, sel ? 3 : 2));
    painter->setBrush(sel ? m_tint.bgSelected : m_tint.bg);
    painter->drawRoundedRect(rect().adjusted(1, 1, -1, -1), u.nodeCornerRadius, u.nodeCornerRadius);

    painter->setPen(c.nodeText);
    QFont title = painter->font();
    title.setBold(true);
    title.setPointSize(u.nodeTitlePointSize);
    painter->setFont(title);
    const qreal pad = u.nodePadding;
    const qreal nameWidth = rect().width() - 2 * pad - u.eyeHitSize;
    painter->drawText(rect().adjusted(pad, pad + 4, -pad - u.eyeHitSize, -pad),
                      Qt::AlignHCenter | Qt::AlignTop, elidedFileName(title, nameWidth));

    QFont sub = title;
    sub.setBold(false);
    sub.setPointSize(u.nodeSubtitlePointSize);
    painter->setFont(sub);
    painter->setPen(c.nodeMuted);
    qreal subTop = 34;
    if (!m_file.comment.isEmpty()) {
        const QFontMetrics fm(sub);
        const QString shown = fm.elidedText(m_file.comment, Qt::ElideRight, int(rect().width() - 2 * pad));
        painter->drawText(rect().adjusted(pad, 32, -pad, -pad), Qt::AlignHCenter | Qt::AlignTop, shown);
        subTop = 48;
    }
    painter->drawText(rect().adjusted(pad, subTop, -pad, -pad), Qt::AlignHCenter | Qt::AlignTop,
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
    if (eyeHitRect().contains(event->pos())) {
        toggleHidden();
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        if (!isSelected())
            QGraphicsRectItem::mousePressEvent(event);
        else
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
