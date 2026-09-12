#include "GraphView.h"

#include "FileNodeItem.h"
#include "I18n.h"
#include "RelationEdgeItem.h"
#include "Theme.h"
#include "UiConfig.h"

#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QQueue>
#include <QSet>
#include <QWheelEvent>

#include <algorithm>
#include <functional>

GraphView::GraphView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    const UiConfig &u = UiConfig::get();
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setBackgroundBrush(Theme::colors().graphBg);
    setMinimumSize(u.graphMinWidth, u.graphMinHeight);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setFocusPolicy(Qt::StrongFocus);
}

void GraphView::applyTheme()
{
    setBackgroundBrush(Theme::colors().graphBg);
    if (m_scene)
        m_scene->update();
    viewport()->update();
}

void GraphView::zoomBy(qreal factor)
{
    const UiConfig &u = UiConfig::get();
    const qreal current = transform().m11();
    const qreal next = current * factor;
    if (next < u.zoomMin || next > u.zoomMax)
        return;
    scale(factor, factor);
}

void GraphView::wheelEvent(QWheelEvent *event)
{
    const UiConfig &u = UiConfig::get();
    const qreal step = event->angleDelta().y() > 0 ? u.zoomStep : (1.0 / u.zoomStep);
    zoomBy(step);
    event->accept();
}

void GraphView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::ZoomIn) || event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) {
        zoomBy(UiConfig::get().zoomStep);
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::ZoomOut) || event->key() == Qt::Key_Minus) {
        zoomBy(1.0 / UiConfig::get().zoomStep);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_0 && event->modifiers() & Qt::ControlModifier) {
        resetTransform();
        event->accept();
        return;
    }
    if (m_linkFrom && event->key() == Qt::Key_Escape) {
        cancelFictitiousLink();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void GraphView::setAnalysis(const AnalysisResult &result, const QString &rootDir)
{
    cancelFictitiousLink();
    m_scene->clear();
    m_nodes.clear();
    m_edges.clear();
    m_notes.setRoot(rootDir);

    AnalysisResult visible;
    QMap<QString, FileNodeItem *> items;
    for (FileNode file : result.files) {
        if (m_notes.isDeleted(file.path))
            continue;
        file.comment = m_notes.comment(file.path);
        visible.files.push_back(file);
    }
    QSet<QString> kept;
    for (const FileNode &file : visible.files)
        kept.insert(file.path);
    for (const FileRelation &rel : result.relations) {
        if (rel.fictitious)
            continue;
        if (kept.contains(rel.fromPath) && kept.contains(rel.toPath))
            visible.relations.push_back(rel);
    }

    for (const FileNode &file : visible.files) {
        auto *node = new FileNodeItem(file);
        m_scene->addItem(node);
        items.insert(file.path, node);
        m_nodes.push_back(node);
        connect(node, &FileNodeItem::selected, this, [this, node]() {
            if (m_linkFrom)
                return;
            emit fileSelected(node->file());
        });
        connect(node, &FileNodeItem::visibilityToggled, this, [this]() {
            recomputeAutoHidden();
            refreshEdgeVisibility();
            updateSceneExtents();
        });
        connect(node, &FileNodeItem::moved, this, &GraphView::updateSceneExtents);
    }

    layoutNodes(QVector<FileNodeItem *>::fromList(items.values()), visible);
    resolveOverlaps();

    for (const FileRelation &rel : visible.relations) {
        FileNodeItem *from = items.value(rel.fromPath);
        FileNodeItem *to = items.value(rel.toPath);
        if (from && to)
            addEdge(from, to, rel);
    }
    for (const FictitiousLink &link : m_notes.fictitious()) {
        FileNodeItem *from = items.value(link.fromPath);
        FileNodeItem *to = items.value(link.toPath);
        if (!from || !to)
            continue;
        FileRelation rel;
        rel.fromPath = from->file().path;
        rel.toPath = to->file().path;
        rel.fromFileName = from->file().fileName;
        rel.toFileName = to->file().fileName;
        rel.fictitious = true;
        rel.comment = link.comment;
        addEdge(from, to, rel);
    }

    assignCurveOffsets();
    for (RelationEdgeItem *edge : m_edges)
        edge->updatePath();

    updateSceneExtents();
    resetTransform();
    centerOn(m_scene->sceneRect().center());
}

void GraphView::updateSceneExtents()
{
    if (!m_scene)
        return;
    const UiConfig &u = UiConfig::get();
    const qreal padX = u.scenePaddingBlocks * (u.nodeWidth + u.layoutMinGap);
    const qreal padY = u.scenePaddingBlocks * (u.nodeHeight + u.layoutMinGap);
    QRectF bounds;
    bool any = false;
    for (FileNodeItem *n : m_nodes) {
        if (!n)
            continue;
        const QRectF r(n->scenePos(), QSizeF(u.nodeWidth, u.nodeHeight));
        if (!any) {
            bounds = r;
            any = true;
        } else {
            bounds = bounds.united(r);
        }
    }
    if (!any)
        bounds = QRectF(0, 0, u.graphMinWidth, u.graphMinHeight);
    m_scene->setSceneRect(bounds.adjusted(-padX, -padY, padX, padY));
}

void GraphView::assignCurveOffsets()
{
    const UiConfig &u = UiConfig::get();
    QMap<QString, int> pairCount;
    for (RelationEdgeItem *edge : m_edges) {
        if (!edge->fromNode() || !edge->toNode())
            continue;
        const QString a = edge->fromNode()->file().path;
        const QString b = edge->toNode()->file().path;
        const QString key = a < b ? (a + QLatin1Char('\n') + b) : (b + QLatin1Char('\n') + a);
        const int n = pairCount.value(key, 0);
        pairCount[key] = n + 1;
        qreal mag = u.edgeCurveBase + (n / 2) * u.edgeCurveStep;
        qreal sign = (n % 2 == 0) ? 1.0 : -1.0;
        if (a > b)
            sign = -sign;
        edge->setCurveOffset(sign * mag);
    }
}

void GraphView::refreshEdgeVisibility()
{
    for (RelationEdgeItem *edge : m_edges) {
        if (edge)
            edge->updateVisibility();
    }
}

void GraphView::recomputeAutoHidden()
{
    QSet<FileNodeItem *> notManual;
    for (FileNodeItem *n : m_nodes) {
        if (n && !n->isManuallyHidden())
            notManual.insert(n);
    }
    QSet<FileNodeItem *> linked;
    for (RelationEdgeItem *edge : m_edges) {
        if (!edge || !edge->fromNode() || !edge->toNode())
            continue;
        if (notManual.contains(edge->fromNode()) && notManual.contains(edge->toNode())) {
            linked.insert(edge->fromNode());
            linked.insert(edge->toNode());
        }
    }
    for (FileNodeItem *n : m_nodes) {
        if (!n)
            continue;
        if (n->isManuallyHidden()) {
            n->setAutoHidden(false);
            continue;
        }
        n->setAutoHidden(!linked.contains(n));
    }
}

void GraphView::layoutNodes(const QVector<FileNodeItem *> &nodes, const AnalysisResult &result)
{
    QMap<QString, FileNodeItem *> byPath;
    QMap<QString, QStringList> outs;
    QMap<QString, int> indeg;
    for (FileNodeItem *n : nodes) {
        byPath[n->file().path] = n;
        indeg[n->file().path] = 0;
    }
    for (const FileRelation &rel : result.relations) {
        if (!byPath.contains(rel.fromPath) || !byPath.contains(rel.toPath))
            continue;
        outs[rel.fromPath] << rel.toPath;
        indeg[rel.toPath] += 1;
    }

    QStringList roots;
    for (auto it = indeg.begin(); it != indeg.end(); ++it) {
        if (it.value() == 0)
            roots << it.key();
    }
    std::sort(roots.begin(), roots.end(), [&](const QString &a, const QString &b) {
        return byPath[a]->file().fileName < byPath[b]->file().fileName;
    });

    QSet<QString> placed;
    QMap<QString, QStringList> tree;
    std::function<void(const QString &)> walk = [&](const QString &u) {
        if (placed.contains(u))
            return;
        placed.insert(u);
        QStringList kids = outs.value(u);
        std::sort(kids.begin(), kids.end(), [&](const QString &a, const QString &b) {
            return byPath[a]->file().fileName < byPath[b]->file().fileName;
        });
        for (const QString &v : kids) {
            if (placed.contains(v))
                continue;
            tree[u] << v;
            walk(v);
        }
    };
    for (const QString &root : roots)
        walk(root);
    QStringList extraRoots;
    QStringList leftover;
    for (FileNodeItem *n : nodes) {
        if (!placed.contains(n->file().path))
            leftover << n->file().path;
    }
    std::sort(leftover.begin(), leftover.end(), [&](const QString &a, const QString &b) {
        return byPath[a]->file().fileName < byPath[b]->file().fileName;
    });
    for (const QString &path : leftover) {
        if (placed.contains(path))
            continue;
        extraRoots << path;
        walk(path);
    }

    const UiConfig &u = UiConfig::get();
    const qreal xStep = qMax(u.layoutXGap, u.nodeWidth + u.layoutMinGap);
    const qreal yStep = qMax(u.layoutYGap, u.nodeHeight + u.layoutMinGap);
    qreal nextX = 0;
    QSet<QString> laid;
    std::function<qreal(const QString &, int)> place = [&](const QString &path, int depth) -> qreal {
        if (laid.contains(path))
            return byPath[path]->pos().x();
        laid.insert(path);
        const QStringList kids = tree.value(path);
        qreal x = 0;
        if (kids.isEmpty()) {
            x = nextX;
            nextX += xStep;
        } else {
            qreal first = 0;
            qreal last = 0;
            for (int i = 0; i < kids.size(); ++i) {
                const qreal cx = place(kids[i], depth + 1);
                if (i == 0)
                    first = cx;
                last = cx;
            }
            x = (first + last) / 2.0;
        }
        byPath[path]->setPos(x, depth * yStep);
        return x;
    };
    nextX = 0;
    for (const QString &root : roots) {
        if (byPath.contains(root) && !laid.contains(root))
            place(root, 0);
    }
    for (const QString &path : extraRoots) {
        if (byPath.contains(path) && !laid.contains(path))
            place(path, 0);
    }
    for (FileNodeItem *n : nodes) {
        if (!laid.contains(n->file().path)) {
            n->setPos(nextX, 0);
            nextX += xStep;
            laid.insert(n->file().path);
        }
    }
}

void GraphView::resolveOverlaps()
{
    const UiConfig &u = UiConfig::get();
    const qreal gap = u.layoutMinGap;
    const qreal w = u.nodeWidth;
    const qreal h = u.nodeHeight;
    for (int pass = 0; pass < 24; ++pass) {
        bool moved = false;
        for (int i = 0; i < m_nodes.size(); ++i) {
            for (int j = i + 1; j < m_nodes.size(); ++j) {
                FileNodeItem *a = m_nodes[i];
                FileNodeItem *b = m_nodes[j];
                if (!a || !b)
                    continue;
                QRectF ra(a->pos(), QSizeF(w, h));
                QRectF rb(b->pos(), QSizeF(w, h));
                ra.adjust(-gap / 2.0, -gap / 2.0, gap / 2.0, gap / 2.0);
                rb.adjust(-gap / 2.0, -gap / 2.0, gap / 2.0, gap / 2.0);
                if (!ra.intersects(rb))
                    continue;
                FileNodeItem *left = a;
                FileNodeItem *right = b;
                if (a->pos().x() > b->pos().x() || (qFuzzyCompare(a->pos().x(), b->pos().x()) && a->pos().y() > b->pos().y())) {
                    left = b;
                    right = a;
                }
                const qreal neededX = left->pos().x() + w + gap;
                if (right->pos().x() < neededX - 0.5) {
                    right->setPos(neededX, right->pos().y());
                    moved = true;
                }
            }
        }
        if (!moved)
            break;
    }
}

FileNodeItem *GraphView::nodeFromItem(QGraphicsItem *item) const
{
    while (item) {
        if (auto *node = dynamic_cast<FileNodeItem *>(item))
            return node;
        item = item->parentItem();
    }
    return nullptr;
}

RelationEdgeItem *GraphView::edgeFromItem(QGraphicsItem *item) const
{
    while (item) {
        if (auto *edge = dynamic_cast<RelationEdgeItem *>(item))
            return edge;
        item = item->parentItem();
    }
    return nullptr;
}

FileNodeItem *GraphView::nodeAtViewPos(const QPoint &pos) const
{
    return nodeFromItem(itemAt(pos));
}

RelationEdgeItem *GraphView::addEdge(FileNodeItem *from, FileNodeItem *to, const FileRelation &rel)
{
    auto *edge = new RelationEdgeItem(from, to, rel);
    m_scene->addItem(edge);
    m_edges.push_back(edge);
    connect(from, &FileNodeItem::moved, edge, &RelationEdgeItem::updatePath);
    connect(to, &FileNodeItem::moved, edge, &RelationEdgeItem::updatePath);
    connect(edge, &RelationEdgeItem::clicked, this, &GraphView::relationSelected);
    return edge;
}

void GraphView::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_linkFrom) {
        cancelFictitiousLink();
        event->accept();
        return;
    }
    FileNodeItem *node = nodeFromItem(itemAt(event->pos()));
    if (node) {
        showNodeMenu(node, event->globalPos());
        event->accept();
        return;
    }
    RelationEdgeItem *edge = edgeFromItem(itemAt(event->pos()));
    if (edge && edge->relation().fictitious) {
        showEdgeMenu(edge, event->globalPos());
        event->accept();
        return;
    }
    QGraphicsView::contextMenuEvent(event);
}

void GraphView::mousePressEvent(QMouseEvent *event)
{
    if (m_linkFrom) {
        if (event->button() == Qt::LeftButton) {
            FileNodeItem *to = nodeAtViewPos(event->pos());
            if (to)
                completeFictitiousLink(to);
            else
                cancelFictitiousLink();
        } else {
            cancelFictitiousLink();
        }
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void GraphView::showNodeMenu(FileNodeItem *node, const QPoint &globalPos)
{
    if (!node)
        return;
    emit fileSelected(node->file());
    QMenu menu(this);
    QAction *del = menu.addAction(I18n::t(QStringLiteral("menu_delete_block")));
    QAction *comment = menu.addAction(I18n::t(QStringLiteral("menu_comment_block")));
    QAction *link = menu.addAction(I18n::t(QStringLiteral("menu_fictitious_link")));
    QAction *chosen = menu.exec(globalPos);
    if (chosen == del)
        deleteNode(node);
    else if (chosen == comment)
        editNodeComment(node);
    else if (chosen == link)
        startFictitiousLink(node);
}

void GraphView::showEdgeMenu(RelationEdgeItem *edge, const QPoint &globalPos)
{
    if (!edge)
        return;
    emit relationSelected(edge->relation());
    QMenu menu(this);
    QAction *edit = menu.addAction(I18n::t(QStringLiteral("menu_edit_link_comment")));
    QAction *del = menu.addAction(I18n::t(QStringLiteral("menu_delete_link")));
    QAction *chosen = menu.exec(globalPos);
    if (chosen == edit)
        editEdgeComment(edge);
    else if (chosen == del)
        deleteEdge(edge);
}

void GraphView::deleteNode(FileNodeItem *node)
{
    if (!node)
        return;
    const QString name = node->file().fileName;
    if (QMessageBox::question(this, I18n::t(QStringLiteral("delete_block_title")),
                              I18n::t(QStringLiteral("delete_block_confirm")).arg(name))
        != QMessageBox::Yes)
        return;
    const QString path = node->file().path;
    QVector<RelationEdgeItem *> leftover;
    for (RelationEdgeItem *edge : m_edges) {
        if (!edge)
            continue;
        if (edge->fromNode() == node || edge->toNode() == node) {
            m_scene->removeItem(edge);
            delete edge;
            continue;
        }
        leftover.push_back(edge);
    }
    m_edges = leftover;
    m_nodes.removeAll(node);
    m_scene->removeItem(node);
    delete node;
    m_notes.deleteFile(path);
    assignCurveOffsets();
    for (RelationEdgeItem *edge : m_edges)
        edge->updatePath();
    recomputeAutoHidden();
    refreshEdgeVisibility();
    updateSceneExtents();
    emit selectionCleared();
}

void GraphView::editNodeComment(FileNodeItem *node)
{
    if (!node)
        return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, I18n::t(QStringLiteral("comment_title")),
                                                        I18n::t(QStringLiteral("comment_prompt")).arg(node->file().fileName),
                                                        node->file().comment, &ok);
    if (!ok)
        return;
    node->setComment(text);
    m_notes.setComment(node->file().path, node->file().comment);
    emit fileSelected(node->file());
}

void GraphView::startFictitiousLink(FileNodeItem *from)
{
    if (!from)
        return;
    m_linkFrom = from;
    from->setSelected(true);
    setDragMode(QGraphicsView::NoDrag);
    viewport()->setCursor(Qt::CrossCursor);
    emit statusMessage(I18n::t(QStringLiteral("fictitious_status")).arg(from->file().fileName));
}

void GraphView::completeFictitiousLink(FileNodeItem *to)
{
    FileNodeItem *from = m_linkFrom;
    m_linkFrom = nullptr;
    setDragMode(QGraphicsView::ScrollHandDrag);
    viewport()->unsetCursor();
    if (!from || !to || from == to)
        return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, I18n::t(QStringLiteral("fictitious_title")),
                                                        I18n::t(QStringLiteral("fictitious_prompt"))
                                                            .arg(from->file().fileName, to->file().fileName),
                                                        QString(), &ok);
    if (!ok)
        return;

    for (RelationEdgeItem *edge : m_edges) {
        if (!edge || !edge->relation().fictitious)
            continue;
        if (edge->fromNode() == from && edge->toNode() == to) {
            edge->setComment(text);
            m_notes.setFictitiousComment(from->file().path, to->file().path, edge->relation().comment);
            emit relationSelected(edge->relation());
            return;
        }
    }

    FileRelation rel;
    rel.fromPath = from->file().path;
    rel.toPath = to->file().path;
    rel.fromFileName = from->file().fileName;
    rel.toFileName = to->file().fileName;
    rel.fictitious = true;
    rel.comment = text.trimmed();
    RelationEdgeItem *edge = addEdge(from, to, rel);
    m_notes.addFictitious(rel.fromPath, rel.toPath, rel.comment);
    assignCurveOffsets();
    for (RelationEdgeItem *e : m_edges)
        e->updatePath();
    recomputeAutoHidden();
    refreshEdgeVisibility();
    emit relationSelected(edge->relation());
}

void GraphView::cancelFictitiousLink()
{
    if (!m_linkFrom)
        return;
    m_linkFrom = nullptr;
    setDragMode(QGraphicsView::ScrollHandDrag);
    viewport()->unsetCursor();
    emit statusMessage(I18n::t(QStringLiteral("fictitious_cancelled")));
}

void GraphView::editEdgeComment(RelationEdgeItem *edge)
{
    if (!edge)
        return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, I18n::t(QStringLiteral("fictitious_title")),
                                                        I18n::t(QStringLiteral("fictitious_prompt"))
                                                            .arg(edge->relation().fromFileName, edge->relation().toFileName),
                                                        edge->relation().comment, &ok);
    if (!ok)
        return;
    edge->setComment(text);
    m_notes.setFictitiousComment(edge->relation().fromPath, edge->relation().toPath, edge->relation().comment);
    emit relationSelected(edge->relation());
}

void GraphView::deleteEdge(RelationEdgeItem *edge)
{
    if (!edge)
        return;
    m_notes.removeFictitious(edge->relation().fromPath, edge->relation().toPath);
    m_edges.removeAll(edge);
    m_scene->removeItem(edge);
    delete edge;
    assignCurveOffsets();
    for (RelationEdgeItem *e : m_edges)
        e->updatePath();
    recomputeAutoHidden();
    refreshEdgeVisibility();
    emit selectionCleared();
}
