#include "GraphView.h"

#include "AnalysisUtil.h"
#include "FileNodeItem.h"
#include "I18n.h"
#include "RelationEdgeItem.h"
#include "Theme.h"
#include "UiConfig.h"

#include <QAction>
#include <QColor>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
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
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setBackgroundBrush(Theme::colors().graphBg);
    setMinimumSize(u.graphMinWidth, u.graphMinHeight);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setFocusPolicy(Qt::StrongFocus);
}

void GraphView::applyTheme()
{
    setBackgroundBrush(Theme::colors().graphBg);
    refreshNodeColors();
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
    m_full = result;
    m_notes.setRoot(rootDir);
    bool filterOk = m_notes.languageFilter() == QLatin1String("all");
    refreshWebLinks();
    if (m_notes.languageFilter() == QLatin1String("web") && hasWebFilter())
        filterOk = true;
    for (SourceLanguage language : presentLanguages()) {
        if (AnalysisUtil::languageKey(language) == m_notes.languageFilter())
            filterOk = true;
    }
    if (!filterOk)
        m_notes.setLanguageFilter(QStringLiteral("all"));
    rebuildGraph(true);
    emit viewOptionsChanged();
}

QVector<SourceLanguage> GraphView::presentLanguages() const
{
    QSet<int> seen;
    QVector<SourceLanguage> out;
    for (const FileNode &file : m_full.files) {
        if (m_notes.isDeleted(file.path) || file.language == SourceLanguage::Unknown)
            continue;
        const int key = static_cast<int>(file.language);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.push_back(file.language);
    }
    std::sort(out.begin(), out.end(), [](SourceLanguage a, SourceLanguage b) {
        return static_cast<int>(a) < static_cast<int>(b);
    });
    return out;
}

bool GraphView::hasWebFilter() const
{
    for (const FileNode &file : m_full.files) {
        if (m_notes.isDeleted(file.path))
            continue;
        if (AnalysisUtil::isWebFile(file.path) || m_webLinked.contains(file.path))
            return true;
    }
    return false;
}

void GraphView::refreshWebLinks()
{
    m_webLinked.clear();
    QSet<QString> webFiles;
    for (const FileNode &file : m_full.files) {
        if (m_notes.isDeleted(file.path))
            continue;
        if (AnalysisUtil::isWebFile(file.path))
            webFiles.insert(file.path);
    }
    for (const FileRelation &rel : m_full.relations) {
        const bool fromWeb = webFiles.contains(rel.fromPath);
        const bool toWeb = webFiles.contains(rel.toPath);
        if (fromWeb == toWeb)
            continue;
        m_webLinked.insert(fromWeb ? rel.toPath : rel.fromPath);
    }
}

QString GraphView::languageFilter() const
{
    return m_notes.languageFilter();
}

void GraphView::setLanguageFilter(const QString &key)
{
    if (m_notes.languageFilter() == key)
        return;
    m_notes.setLanguageFilter(key);
    rebuildGraph(false);
    emit viewOptionsChanged();
}

bool GraphView::filePassesFilter(const FileNode &file) const
{
    if (m_notes.isDeleted(file.path) || m_notes.isFileHidden(file.path))
        return false;
    const QString filter = m_notes.languageFilter();
    if (filter.isEmpty() || filter == QLatin1String("all"))
        return true;
    if (filter == QLatin1String("web"))
        return AnalysisUtil::isWebFile(file.path) || m_webLinked.contains(file.path);
    return AnalysisUtil::languageKey(file.language) == filter;
}

void GraphView::syncColorMode()
{
    const QString filter = m_notes.languageFilter();
    if (filter != QLatin1String("all") && filter != QLatin1String("web")) {
        m_colorByLanguage = false;
        return;
    }
    QSet<int> langs;
    for (const FileNode &file : m_full.files) {
        if (!filePassesFilter(file) || file.language == SourceLanguage::Unknown)
            continue;
        langs.insert(static_cast<int>(file.language));
    }
    m_colorByLanguage = langs.size() > 1;
}

NodeTint GraphView::tintFor(const FileNode &file) const
{
    if (const FileGroup *group = m_notes.groupOf(file.path)) {
        NodeTint t;
        t.bg = QColor(group->color);
        t.border = QColor(group->border);
        if (!t.bg.isValid())
            return Theme::defaultNodeTint();
        t.bgSelected = Theme::current() == AppTheme::Dark ? t.bg.lighter(128) : t.bg.darker(108);
        if (!t.border.isValid())
            t.border = t.bg.darker(130);
        return t;
    }
    if (useLanguageTints())
        return Theme::languageTint(file.language);
    return Theme::defaultNodeTint();
}

NodeTint GraphView::unusedGroupTint() const
{
    QVector<QColor> used;
    used.push_back(Theme::defaultNodeTint().bg);
    for (const FileGroup &g : m_notes.groups()) {
        const QColor c(g.color);
        if (c.isValid())
            used.push_back(c);
    }
    if (useLanguageTints()) {
        for (SourceLanguage language : presentLanguages())
            used.push_back(Theme::languageTint(language).bg);
    }
    return Theme::pickUnusedTint(used);
}

void GraphView::refreshNodeColors()
{
    for (FileNodeItem *node : m_nodes) {
        if (node)
            node->setTint(tintFor(node->file()));
    }
}

void GraphView::rebuildGraph(bool resetView)
{
    QMap<QString, QPointF> savedPos;
    for (FileNodeItem *node : m_nodes) {
        if (node)
            savedPos.insert(node->file().path, node->pos());
    }

    cancelFictitiousLink();
    m_scene->clear();
    m_nodes.clear();
    m_edges.clear();
    refreshWebLinks();
    syncColorMode();

    AnalysisResult visible;
    QMap<QString, FileNodeItem *> items;
    for (FileNode file : m_full.files) {
        if (!filePassesFilter(file))
            continue;
        file.comment = m_notes.comment(file.path);
        visible.files.push_back(file);
    }
    QSet<QString> kept;
    for (const FileNode &file : visible.files)
        kept.insert(file.path);
    for (const FileRelation &rel : m_full.relations) {
        if (rel.fictitious)
            continue;
        if (kept.contains(rel.fromPath) && kept.contains(rel.toPath))
            visible.relations.push_back(rel);
    }

    for (const FileNode &file : visible.files) {
        auto *node = new FileNodeItem(file);
        node->setTint(tintFor(file));
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
        connect(node, &FileNodeItem::moved, this, &GraphView::onNodeMoved);
    }

    m_layoutBusy = true;
    layoutNodes(m_nodes, visible.relations);
    resolveOverlaps();
    for (FileNodeItem *node : m_nodes) {
        auto it = savedPos.find(node->file().path);
        if (it != savedPos.end())
            node->setPos(*it);
    }
    if (!savedPos.isEmpty())
        resolveOverlaps();
    m_layoutBusy = false;

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
    if (resetView) {
        resetTransform();
        centerOn(m_scene->sceneRect().center());
    }
}

void GraphView::updateSceneExtents()
{
    if (!m_scene || m_layoutBusy)
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

void GraphView::onNodeMoved()
{
    if (m_layoutBusy || !m_scene || m_draggingNode)
        return;
    auto *node = qobject_cast<FileNodeItem *>(sender());
    if (!node)
        return;
    const UiConfig &u = UiConfig::get();
    const qreal padX = u.scenePaddingBlocks * (u.nodeWidth + u.layoutMinGap);
    const qreal padY = u.scenePaddingBlocks * (u.nodeHeight + u.layoutMinGap);
    const QRectF need = QRectF(node->scenePos(), QSizeF(u.nodeWidth, u.nodeHeight)).adjusted(-padX, -padY, padX, padY);
    const QRectF cur = m_scene->sceneRect();
    if (!cur.contains(need))
        m_scene->setSceneRect(cur.united(need));
}

void GraphView::setFastRender(bool enabled)
{
    if (m_fastRender == enabled)
        return;
    m_fastRender = enabled;
    setRenderHint(QPainter::Antialiasing, !enabled);
    setRenderHint(QPainter::TextAntialiasing, !enabled);
    if (!enabled)
        viewport()->update();
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
    const bool labelsOnDemand = m_edges.size() > 48;
    for (RelationEdgeItem *edge : m_edges) {
        if (edge)
            edge->setLabelsOnDemand(labelsOnDemand);
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
        if (n->isManuallyHidden() || n->isPinnedVisible()) {
            n->setAutoHidden(false, false);
            continue;
        }
        n->setAutoHidden(!linked.contains(n), m_nodes.size() <= 48);
    }
}

void GraphView::layoutNodes(const QVector<FileNodeItem *> &nodes, const QVector<FileRelation> &relations)
{
    QMap<QString, FileNodeItem *> byPath;
    QMap<QString, QStringList> outs;
    QMap<QString, int> indeg;
    for (FileNodeItem *n : nodes) {
        byPath[n->file().path] = n;
        indeg[n->file().path] = 0;
    }
    for (const FileRelation &rel : relations) {
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
    const qreal minDistX = w + gap;
    const qreal minDistY = h + gap;
    QVector<FileNodeItem *> nodes = m_nodes;
    for (int pass = 0; pass < 12; ++pass) {
        std::sort(nodes.begin(), nodes.end(), [](FileNodeItem *a, FileNodeItem *b) {
            if (!a)
                return false;
            if (!b)
                return true;
            if (a->pos().x() != b->pos().x())
                return a->pos().x() < b->pos().x();
            return a->pos().y() < b->pos().y();
        });
        bool moved = false;
        for (int i = 0; i < nodes.size(); ++i) {
            FileNodeItem *a = nodes[i];
            if (!a)
                continue;
            for (int j = i + 1; j < nodes.size(); ++j) {
                FileNodeItem *b = nodes[j];
                if (!b)
                    continue;
                if (b->pos().x() >= a->pos().x() + minDistX)
                    break;
                if (qAbs(a->pos().y() - b->pos().y()) >= minDistY)
                    continue;
                FileNodeItem *left = a;
                FileNodeItem *right = b;
                if (a->pos().x() > b->pos().x()
                    || (qFuzzyCompare(a->pos().x() + 1.0, b->pos().x() + 1.0) && a->pos().y() > b->pos().y())) {
                    left = b;
                    right = a;
                }
                const qreal neededX = left->pos().x() + minDistX;
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
    if (event->button() == Qt::LeftButton) {
        if (nodeAtViewPos(event->pos())) {
            m_draggingNode = true;
            setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
        } else if (!edgeFromItem(itemAt(event->pos())))
            setFastRender(true);
    }
    QGraphicsView::mousePressEvent(event);
}

void GraphView::mouseReleaseEvent(QMouseEvent *event)
{
    QGraphicsView::mouseReleaseEvent(event);
    m_draggingNode = false;
    if (m_fastRender)
        setFastRender(false);
    updateSceneExtents();
    if (m_scene)
        m_scene->invalidate();
    viewport()->repaint();
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
}

void GraphView::showNodeMenu(FileNodeItem *node, const QPoint &globalPos)
{
    if (!node)
        return;
    const QVector<FileNodeItem *> targets = nodesForMenu(node);
    emit fileSelected(node->file());
    QMenu menu(this);
    QAction *del = menu.addAction(I18n::t(QStringLiteral("menu_delete_block")));
    QAction *comment = menu.addAction(I18n::t(QStringLiteral("menu_comment_block")));
    QAction *link = menu.addAction(I18n::t(QStringLiteral("menu_fictitious_link")));
    del->setEnabled(targets.size() == 1);
    comment->setEnabled(targets.size() == 1);
    link->setEnabled(targets.size() == 1);
    menu.addSeparator();
    QAction *newGroup = menu.addAction(I18n::t(QStringLiteral("menu_new_group")));
    const QVector<FileGroup> groups = m_notes.groups();
    QVector<QAction *> addActions;
    if (!groups.isEmpty()) {
        QMenu *addMenu = menu.addMenu(I18n::t(QStringLiteral("menu_add_to_group")));
        for (const FileGroup &g : groups) {
            QAction *a = addMenu->addAction(g.name);
            a->setData(g.id);
            bool allIn = true;
            for (FileNodeItem *n : targets) {
                const FileGroup *cur = n ? m_notes.groupOf(n->file().path) : nullptr;
                if (!cur || cur->id != g.id) {
                    allIn = false;
                    break;
                }
            }
            a->setEnabled(!allIn);
            addActions.push_back(a);
        }
    }
    bool anyGrouped = false;
    for (FileNodeItem *n : targets) {
        if (n && m_notes.groupOf(n->file().path)) {
            anyGrouped = true;
            break;
        }
    }
    QAction *removeGroup = nullptr;
    if (anyGrouped)
        removeGroup = menu.addAction(I18n::t(QStringLiteral("menu_remove_from_group")));
    QAction *chosen = menu.exec(globalPos);
    if (chosen == del)
        deleteNode(node);
    else if (chosen == comment)
        editNodeComment(node);
    else if (chosen == link)
        startFictitiousLink(node);
    else if (chosen == newGroup)
        createGroupFor(targets);
    else if (removeGroup && chosen == removeGroup)
        removeNodesFromGroup(targets);
    else if (chosen) {
        for (QAction *a : addActions) {
            if (chosen == a) {
                addNodesToGroup(targets, a->data().toString());
                break;
            }
        }
    }
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
    syncColorMode();
    refreshNodeColors();
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

void GraphView::chooseVisibleFiles()
{
    QDialog dialog(this);
    dialog.setWindowTitle(I18n::t(QStringLiteral("files_dialog_title")));
    auto *layout = new QVBoxLayout(&dialog);
    auto *tree = new QTreeWidget(&dialog);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);
    layout->addWidget(tree);

    QMap<int, QVector<const FileNode *>> byLang;
    QVector<const FileNode *> unknown;
    for (const FileNode &file : m_full.files) {
        if (m_notes.isDeleted(file.path))
            continue;
        if (file.language == SourceLanguage::Unknown)
            unknown.push_back(&file);
        else
            byLang[static_cast<int>(file.language)].push_back(&file);
    }

    auto addFiles = [&](QTreeWidgetItem *parent, QVector<const FileNode *> files) {
        std::sort(files.begin(), files.end(), [](const FileNode *a, const FileNode *b) {
            return a->fileName < b->fileName;
        });
        int checked = 0;
        for (const FileNode *file : files) {
            auto *item = new QTreeWidgetItem(parent);
            item->setText(0, file->fileName);
            item->setToolTip(0, file->path);
            item->setData(0, Qt::UserRole, file->path);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            const bool vis = !m_notes.isFileHidden(file->path);
            item->setCheckState(0, vis ? Qt::Checked : Qt::Unchecked);
            if (vis)
                ++checked;
        }
        if (!files.isEmpty()) {
            parent->setFlags(parent->flags() | Qt::ItemIsUserCheckable);
            if (checked == files.size())
                parent->setCheckState(0, Qt::Checked);
            else if (checked == 0)
                parent->setCheckState(0, Qt::Unchecked);
            else
                parent->setCheckState(0, Qt::PartiallyChecked);
        }
        parent->setExpanded(true);
    };

    const QVector<SourceLanguage> order = presentLanguages();
    for (SourceLanguage language : order) {
        auto *parent = new QTreeWidgetItem(tree);
        parent->setText(0, I18n::languageName(language));
        addFiles(parent, byLang.value(static_cast<int>(language)));
    }
    if (!unknown.isEmpty()) {
        auto *parent = new QTreeWidgetItem(tree);
        parent->setText(0, I18n::t(QStringLiteral("lang_all")));
        addFiles(parent, unknown);
    }

    QObject::connect(tree, &QTreeWidget::itemChanged, &dialog, [tree](QTreeWidgetItem *item, int) {
        if (!item)
            return;
        tree->blockSignals(true);
        if (item->childCount() > 0) {
            const Qt::CheckState state = item->checkState(0);
            if (state != Qt::PartiallyChecked) {
                for (int i = 0; i < item->childCount(); ++i)
                    item->child(i)->setCheckState(0, state);
            }
        } else if (QTreeWidgetItem *parent = item->parent()) {
            int checked = 0;
            for (int i = 0; i < parent->childCount(); ++i) {
                if (parent->child(i)->checkState(0) == Qt::Checked)
                    ++checked;
            }
            if (checked == 0)
                parent->setCheckState(0, Qt::Unchecked);
            else if (checked == parent->childCount())
                parent->setCheckState(0, Qt::Checked);
            else
                parent->setCheckState(0, Qt::PartiallyChecked);
        }
        tree->blockSignals(false);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(420, 480);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QStringList hidden;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *parent = tree->topLevelItem(i);
        for (int j = 0; j < parent->childCount(); ++j) {
            QTreeWidgetItem *item = parent->child(j);
            if (item->checkState(0) != Qt::Checked)
                hidden << item->data(0, Qt::UserRole).toString();
        }
    }
    m_notes.setHiddenFiles(hidden);
    rebuildGraph(false);
}

void GraphView::createGroupFor(const QVector<FileNodeItem *> &nodes)
{
    const QStringList paths = pathsOf(nodes);
    if (paths.isEmpty())
        return;
    bool ok = false;
    const QString suggested = I18n::t(QStringLiteral("group_default")).arg(m_notes.groups().size() + 1);
    const QString rawName = QInputDialog::getText(this, I18n::t(QStringLiteral("group_title")),
                                               I18n::t(QStringLiteral("group_prompt")), QLineEdit::Normal, suggested,
                                               &ok);
    if (!ok)
        return;
    QString name = rawName.trimmed();
    if (name.isEmpty())
        name = suggested;
    const NodeTint tint = unusedGroupTint();
    m_notes.createGroup(name, paths, tint.bg.name(), tint.border.name());
    refreshNodeColors();
}

void GraphView::addNodesToGroup(const QVector<FileNodeItem *> &nodes, const QString &groupId)
{
    if (groupId.isEmpty())
        return;
    for (const QString &path : pathsOf(nodes))
        m_notes.addToGroup(groupId, path);
    refreshNodeColors();
}

void GraphView::removeNodesFromGroup(const QVector<FileNodeItem *> &nodes)
{
    for (const QString &path : pathsOf(nodes))
        m_notes.removeFromGroup(path);
    refreshNodeColors();
}

QVector<FileNodeItem *> GraphView::nodesForMenu(FileNodeItem *clicked) const
{
    QVector<FileNodeItem *> out;
    if (clicked && clicked->isSelected()) {
        for (FileNodeItem *node : m_nodes) {
            if (node && node->isSelected())
                out.push_back(node);
        }
    }
    if (out.isEmpty() && clicked)
        out.push_back(clicked);
    return out;
}

QStringList GraphView::pathsOf(const QVector<FileNodeItem *> &nodes) const
{
    QStringList paths;
    for (FileNodeItem *node : nodes) {
        if (!node)
            continue;
        const QString path = node->file().path;
        if (!path.isEmpty() && !paths.contains(path))
            paths << path;
    }
    return paths;
}
