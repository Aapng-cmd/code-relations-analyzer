#include "RelationEdgeItem.h"
#include "FileNodeItem.h"
#include "Theme.h"
#include "UiConfig.h"

#include <QCursor>
#include <QGraphicsItem>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

#include <cmath>

namespace {

QPointF borderPoint(const QRectF &rect, const QPointF &from, const QPointF &to)
{
    const QLineF line(from, to);
    const QLineF sides[] = {
        QLineF(rect.topLeft(), rect.topRight()),
        QLineF(rect.topRight(), rect.bottomRight()),
        QLineF(rect.bottomRight(), rect.bottomLeft()),
        QLineF(rect.bottomLeft(), rect.topLeft()),
    };
    QPointF best = from;
    qreal bestDist = 1e12;
    for (const QLineF &side : sides) {
        QPointF ip;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        const bool hit = (line.intersects(side, &ip) == QLineF::BoundedIntersection);
#else
        const bool hit = (line.intersect(side, &ip) == QLineF::BoundedIntersection);
#endif
        if (!hit)
            continue;
        const qreal d = QLineF(from, ip).length();
        if (d < bestDist) {
            bestDist = d;
            best = ip;
        }
    }
    return best;
}

} // namespace

RelationEdgeItem::RelationEdgeItem(FileNodeItem *from, FileNodeItem *to, const FileRelation &relation,
                                   QGraphicsItem *parent)
    : QGraphicsPathItem(parent)
    , m_from(from)
    , m_to(to)
    , m_relation(relation)
{
    setZValue(0);
    setFlag(ItemIsSelectable, true);
    setPen(Qt::NoPen);
    setBrush(Qt::NoBrush);
    setCacheMode(QGraphicsItem::NoCache);
    setCursor(QCursor(Qt::PointingHandCursor));
    setAcceptHoverEvents(true);
    updatePath();
}

void RelationEdgeItem::setComment(const QString &comment)
{
    m_relation.comment = comment.trimmed();
    update();
}

void RelationEdgeItem::updateVisibility()
{
    const bool show = m_from && m_to && m_from->isGraphVisible() && m_to->isGraphVisible();
    setVisible(show);
}

void RelationEdgeItem::invalidateShape()
{
    m_shapeDirty = true;
}

void RelationEdgeItem::updatePath()
{
    if (!m_from || !m_to)
        return;

    const QRectF aRect(m_from->scenePos(), m_from->rect().size());
    const QRectF bRect(m_to->scenePos(), m_to->rect().size());
    const QPointF ac = aRect.center();
    const QPointF bc = bRect.center();
    const QPointF a = borderPoint(aRect, ac, bc);
    const QPointF b = borderPoint(bRect, bc, ac);
    QPointF delta = b - a;
    const qreal len = std::hypot(delta.x(), delta.y());
    QPointF normal(0, -1);
    if (len > 1.0)
        normal = QPointF(-delta.y() / len, delta.x() / len);
    const QPointF c1 = a + delta / 3.0 + normal * m_curveOffset;
    const QPointF c2 = a + delta * (2.0 / 3.0) + normal * m_curveOffset;

    prepareGeometryChange();
    QPainterPath path(a);
    path.cubicTo(c1, c2, b);
    m_shapeDirty = true;
    setPath(path);
}

QRectF RelationEdgeItem::boundingRect() const
{
    const UiConfig &u = UiConfig::get();
    qreal extra = qMax(u.edgeHitWidth, u.edgeArrowSize) * 0.5 + 16;
    if (m_hovered || isSelected() || !m_labelsOnDemand)
        extra += 28;
    return path().controlPointRect().adjusted(-extra, -extra, extra, extra);
}

QString RelationEdgeItem::labelText() const
{
    if (m_relation.fictitious && !m_relation.comment.isEmpty())
        return m_relation.comment;
    if (m_relation.fictitious)
        return QStringLiteral("%1 ⇢ %2").arg(m_relation.fromFileName, m_relation.toFileName);
    return QStringLiteral("%1 → %2").arg(m_relation.fromFileName, m_relation.toFileName);
}

QPainterPath RelationEdgeItem::shape() const
{
    if (m_shapeDirty) {
        QPainterPathStroker stroker;
        stroker.setWidth(UiConfig::get().edgeHitWidth);
        stroker.setCapStyle(Qt::RoundCap);
        m_shape = stroker.createStroke(path());
        m_shapeDirty = false;
    }
    return m_shape;
}

void RelationEdgeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);
    const bool sel = option->state & QStyle::State_Selected;
    const bool detailed = sel || m_hovered || !m_labelsOnDemand;
    const ThemeColors &c = Theme::colors();
    const QColor color = sel ? c.edgeSelected : c.edge;
    const UiConfig &u = UiConfig::get();
    const qreal lod = QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform());

    QPen pen(color, sel ? u.edgePenWidthSelected : u.edgePenWidth);
    pen.setCapStyle(Qt::RoundCap);
    if (m_relation.fictitious)
        pen.setStyle(Qt::DashLine);
    painter->setBrush(Qt::NoBrush);

    if (lod < 0.45) {
        pen.setWidthF(qMax<qreal>(1.0, u.edgePenWidth * 0.7));
        painter->setPen(pen);
        painter->drawPath(path());
        return;
    }

    if (sel) {
        QPen outline(c.graphBg, u.edgePenWidthSelected + 3.5);
        outline.setCapStyle(Qt::RoundCap);
        if (m_relation.fictitious)
            outline.setStyle(Qt::DashLine);
        painter->setPen(outline);
        painter->drawPath(path());
    }

    painter->setPen(pen);
    painter->drawPath(path());

    const QPainterPath p = path();
    if (p.isEmpty())
        return;

    const QPointF tip = p.pointAtPercent(1.0);
    const QPointF prev = p.pointAtPercent(0.88);
    const QLineF line(prev, tip);
    const double angle = std::atan2(line.dy(), line.dx());
    const qreal size = u.edgeArrowSize;
    QPolygonF arrow;
    arrow << tip
          << tip - QPointF(std::cos(angle - M_PI / 7) * size, std::sin(angle - M_PI / 7) * size)
          << tip - QPointF(std::cos(angle + M_PI / 7) * size, std::sin(angle + M_PI / 7) * size);
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);
    painter->drawPolygon(arrow);

    if (!detailed)
        return;

    const QPointF mid = p.pointAtPercent(0.5);
    const QString label = labelText();
    QFont font = painter->font();
    font.setPointSize(u.edgeLabelPointSize);
    font.setBold(true);
    painter->setFont(font);
    const QFontMetrics fm(font);
    const QRect textRect = fm.boundingRect(label).adjusted(-6, -3, 6, 3);
    QRectF box(mid.x() - textRect.width() / 2.0, mid.y() - textRect.height() - 6, textRect.width(), textRect.height());
    painter->setBrush(c.edgeLabelBg);
    painter->setPen(QPen(color, 1));
    painter->drawRoundedRect(box, 4, 4);
    painter->setPen(c.nodeText);
    painter->drawText(box, Qt::AlignCenter, label);
}

void RelationEdgeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    m_hovered = true;
    prepareGeometryChange();
    update();
    QGraphicsPathItem::hoverEnterEvent(event);
}

void RelationEdgeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    m_hovered = false;
    prepareGeometryChange();
    update();
    QGraphicsPathItem::hoverLeaveEvent(event);
}

void RelationEdgeItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    emit clicked(m_relation);
    event->accept();
    QGraphicsPathItem::mousePressEvent(event);
}
