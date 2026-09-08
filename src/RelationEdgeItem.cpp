#include "RelationEdgeItem.h"
#include "FileNodeItem.h"
#include "Theme.h"
#include "UiConfig.h"

#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

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
    setZValue(2);
    setFlag(ItemIsSelectable, true);
    setCursor(QCursor(Qt::PointingHandCursor));
    setAcceptHoverEvents(true);
    updatePath();
}

void RelationEdgeItem::updateVisibility()
{
    const bool show = m_from && m_to && m_from->isGraphVisible() && m_to->isGraphVisible();
    setVisible(show);
}

void RelationEdgeItem::updatePath()
{
    if (!m_from || !m_to)
        return;

    const QRectF aRect = m_from->sceneBoundingRect();
    const QRectF bRect = m_to->sceneBoundingRect();
    const QPointF ac = aRect.center();
    const QPointF bc = bRect.center();
    const QPointF a = borderPoint(aRect, ac, bc);
    const QPointF b = borderPoint(bRect, bc, ac);

    QPainterPath path(a);
    path.lineTo(b);
    setPath(path);
}

QPainterPath RelationEdgeItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(UiConfig::get().edgeHitWidth);
    return stroker.createStroke(path());
}

void RelationEdgeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);
    painter->setRenderHint(QPainter::Antialiasing);
    const bool sel = option->state & QStyle::State_Selected;
    const ThemeColors c = Theme::colors();
    const QColor color = sel ? c.edgeSelected : c.edge;

    const UiConfig &u = UiConfig::get();
    QPen outline(c.graphBg, sel ? u.edgePenWidthSelected + 3.5 : u.edgePenWidth + 3.4);
    outline.setCapStyle(Qt::RoundCap);
    painter->setPen(outline);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());

    QPen pen(color, sel ? u.edgePenWidthSelected : u.edgePenWidth);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->drawPath(path());

    const QPainterPath p = path();
    if (p.isEmpty())
        return;

    const QPointF tip = p.pointAtPercent(1.0);
    const QPointF prev = p.pointAtPercent(0.88);
    const QLineF line(prev, tip);
    const double angle = std::atan2(line.dy(), line.dx());
    const qreal size = UiConfig::get().edgeArrowSize;
    QPolygonF arrow;
    arrow << tip
          << tip - QPointF(std::cos(angle - M_PI / 7) * size, std::sin(angle - M_PI / 7) * size)
          << tip - QPointF(std::cos(angle + M_PI / 7) * size, std::sin(angle + M_PI / 7) * size);
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);
    painter->drawPolygon(arrow);

    const QPointF mid = p.pointAtPercent(0.5);
    const QString label = QStringLiteral("%1 → %2").arg(m_relation.fromFileName, m_relation.toFileName);
    QFont font = painter->font();
    font.setPointSize(UiConfig::get().edgeLabelPointSize);
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

void RelationEdgeItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    emit clicked(m_relation);
    event->accept();
    QGraphicsPathItem::mousePressEvent(event);
}
