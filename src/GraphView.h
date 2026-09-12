#pragma once

#include "GraphAnnotations.h"
#include "Model.h"

#include <QGraphicsView>
#include <QVector>

class FileNodeItem;
class RelationEdgeItem;
class QContextMenuEvent;
class QGraphicsItem;
class QMouseEvent;

class GraphView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphView(QWidget *parent = nullptr);

    void setAnalysis(const AnalysisResult &result, const QString &rootDir = QString());
    void applyTheme();

signals:
    void relationSelected(const FileRelation &relation);
    void fileSelected(const FileNode &file);
    void selectionCleared();
    void statusMessage(const QString &text);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void layoutNodes(const QVector<FileNodeItem *> &nodes, const AnalysisResult &result);
    void resolveOverlaps();
    void updateSceneExtents();
    void assignCurveOffsets();
    void recomputeAutoHidden();
    void refreshEdgeVisibility();
    void zoomBy(qreal factor);
    FileNodeItem *nodeFromItem(QGraphicsItem *item) const;
    RelationEdgeItem *edgeFromItem(QGraphicsItem *item) const;
    FileNodeItem *nodeAtViewPos(const QPoint &pos) const;
    RelationEdgeItem *addEdge(FileNodeItem *from, FileNodeItem *to, const FileRelation &rel);
    void showNodeMenu(FileNodeItem *node, const QPoint &globalPos);
    void showEdgeMenu(RelationEdgeItem *edge, const QPoint &globalPos);
    void deleteNode(FileNodeItem *node);
    void editNodeComment(FileNodeItem *node);
    void startFictitiousLink(FileNodeItem *from);
    void completeFictitiousLink(FileNodeItem *to);
    void cancelFictitiousLink();
    void editEdgeComment(RelationEdgeItem *edge);
    void deleteEdge(RelationEdgeItem *edge);

    QGraphicsScene *m_scene = nullptr;
    QVector<FileNodeItem *> m_nodes;
    QVector<RelationEdgeItem *> m_edges;
    GraphAnnotations m_notes;
    FileNodeItem *m_linkFrom = nullptr;
};
