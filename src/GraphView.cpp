#include "GraphView.h"

#include "FileNodeItem.h"
#include "RelationEdgeItem.h"
#include "Theme.h"
#include "UiConfig.h"

#include <QGraphicsScene>
#include <QMap>
#include <QPainter>
#include <QQueue>
#include <QSet>

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
    setDragMode(QGraphicsView::NoDrag);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setBackgroundBrush(Theme::colors().graphBg);
    setMinimumSize(u.graphMinWidth, u.graphMinHeight);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
}

void GraphView::applyTheme()
{
    setBackgroundBrush(Theme::colors().graphBg);
    if (m_scene)
        m_scene->update();
    viewport()->update();
}

void GraphView::setAnalysis(const AnalysisResult &result)
{
    m_scene->clear();
    m_edges.clear();
    QMap<QString, FileNodeItem *> items;

    for (const FileNode &file : result.files) {
        auto *node = new FileNodeItem(file);
        m_scene->addItem(node);
        items.insert(file.path, node);
        connect(node, &FileNodeItem::selected, this, [this, node]() { emit fileSelected(node->file()); });
        connect(node, &FileNodeItem::visibilityToggled, this, &GraphView::refreshEdgeVisibility);
    }

    layoutNodes(QVector<FileNodeItem *>::fromList(items.values()), result);

    for (const FileRelation &rel : result.relations) {
        FileNodeItem *from = items.value(rel.fromPath);
        FileNodeItem *to = items.value(rel.toPath);
        if (!from || !to)
            continue;
        auto *edge = new RelationEdgeItem(from, to, rel);
        m_scene->addItem(edge);
        m_edges.push_back(edge);
        connect(from, &FileNodeItem::moved, edge, &RelationEdgeItem::updatePath);
        connect(to, &FileNodeItem::moved, edge, &RelationEdgeItem::updatePath);
        connect(edge, &RelationEdgeItem::clicked, this, &GraphView::relationSelected);
        edge->updatePath();
    }

    const qreal pad = UiConfig::get().scenePadding;
    const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-pad, -pad, pad, pad);
    m_scene->setSceneRect(bounds);
    resetTransform();
    centerOn(bounds.center());
}

void GraphView::refreshEdgeVisibility()
{
    for (RelationEdgeItem *edge : m_edges) {
        if (edge)
            edge->updateVisibility();
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
    qreal nextY = 0;
    std::function<qreal(const QString &, int)> place = [&](const QString &path, int depth) -> qreal {
        const QStringList kids = tree.value(path);
        qreal y = 0;
        if (kids.isEmpty()) {
            y = nextY;
            nextY += u.layoutYGap;
        } else {
            qreal first = 0;
            qreal last = 0;
            for (int i = 0; i < kids.size(); ++i) {
                const qreal cy = place(kids[i], depth + 1);
                if (i == 0)
                    first = cy;
                last = cy;
            }
            y = (first + last) / 2.0;
        }
        byPath[path]->setPos(depth * u.layoutXGap, y);
        return y;
    };
    nextY = 0;
    for (const QString &root : roots) {
        if (byPath.contains(root))
            place(root, 0);
    }
    for (const QString &path : extraRoots) {
        if (byPath.contains(path))
            place(path, 0);
    }
}
