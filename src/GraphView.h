#pragma once

#include "GraphAnnotations.h"
#include "Model.h"
#include "Theme.h"

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

    QVector<SourceLanguage> presentLanguages() const;
    QString languageFilter() const;
    void setLanguageFilter(const QString &key);
    void chooseVisibleFiles();

signals:
    void relationSelected(const FileRelation &relation);
    void fileSelected(const FileNode &file);
    void selectionCleared();
    void statusMessage(const QString &text);
    void viewOptionsChanged();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void rebuildGraph(bool resetView);
    void layoutNodes(const QVector<FileNodeItem *> &nodes, const QVector<FileRelation> &relations);
    void resolveOverlaps();
    void updateSceneExtents();
    void onNodeMoved();
    void setFastRender(bool enabled);
    void syncColorMode();
    void assignCurveOffsets();
    void recomputeAutoHidden();
    void refreshEdgeVisibility();
    void refreshNodeColors();
    void zoomBy(qreal factor);
    bool filePassesFilter(const FileNode &file) const;
    bool useLanguageTints() const { return m_colorByLanguage; }
    NodeTint tintFor(const FileNode &file) const;
    NodeTint unusedGroupTint() const;
    FileNodeItem *nodeFromItem(QGraphicsItem *item) const;
    RelationEdgeItem *edgeFromItem(QGraphicsItem *item) const;
    FileNodeItem *nodeAtViewPos(const QPoint &pos) const;
    RelationEdgeItem *addEdge(FileNodeItem *from, FileNodeItem *to, const FileRelation &rel);
    void showNodeMenu(FileNodeItem *node, const QPoint &globalPos);
    void showEdgeMenu(RelationEdgeItem *edge, const QPoint &globalPos);
    QVector<FileNodeItem *> nodesForMenu(FileNodeItem *clicked) const;
    QStringList pathsOf(const QVector<FileNodeItem *> &nodes) const;
    void deleteNode(FileNodeItem *node);
    void editNodeComment(FileNodeItem *node);
    void startFictitiousLink(FileNodeItem *from);
    void completeFictitiousLink(FileNodeItem *to);
    void cancelFictitiousLink();
    void editEdgeComment(RelationEdgeItem *edge);
    void deleteEdge(RelationEdgeItem *edge);
    void createGroupFor(const QVector<FileNodeItem *> &nodes);
    void addNodesToGroup(const QVector<FileNodeItem *> &nodes, const QString &groupId);
    void removeNodesFromGroup(const QVector<FileNodeItem *> &nodes);

    QGraphicsScene *m_scene = nullptr;
    QVector<FileNodeItem *> m_nodes;
    QVector<RelationEdgeItem *> m_edges;
    GraphAnnotations m_notes;
    AnalysisResult m_full;
    FileNodeItem *m_linkFrom = nullptr;
    bool m_layoutBusy = false;
    bool m_colorByLanguage = false;
    bool m_fastRender = false;
};
