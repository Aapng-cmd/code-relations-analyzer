#pragma once

#include "Model.h"

#include <QGraphicsView>
#include <QVector>

class FileNodeItem;
class RelationEdgeItem;

class GraphView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphView(QWidget *parent = nullptr);

    void setAnalysis(const AnalysisResult &result);
    void applyTheme();

signals:
    void relationSelected(const FileRelation &relation);
    void fileSelected(const FileNode &file);

private:
    void layoutNodes(const QVector<FileNodeItem *> &nodes, const AnalysisResult &result);
    void refreshEdgeVisibility();

    QGraphicsScene *m_scene = nullptr;
    QVector<RelationEdgeItem *> m_edges;
};
