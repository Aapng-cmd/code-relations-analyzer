#pragma once

#include "Model.h"

#include <QMap>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

class InfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit InfoPanel(QWidget *parent = nullptr);

    void showRelation(const FileRelation &relation);
    void showFile(const FileNode &file);
    void clearInfo();
    void applyTheme();
    void retranslate();

private:
    void fillTree(const QVector<DefinedSymbol> &symbols, bool relationMode);
    void addGrouped(QTreeWidgetItem *parent, const QString &parentQualified,
                    const QMap<QString, QVector<DefinedSymbol>> &children, bool expandGroups, bool relationMode);
    QTreeWidgetItem *ensureGroup(QTreeWidgetItem *parent, const QString &title, bool expanded);
    void styleLeaf(QTreeWidgetItem *item, const DefinedSymbol &sym, bool relationMode);

    QLabel *m_path = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QTreeWidget *m_tree = nullptr;
    enum class Mode { Empty, File, Relation };
    Mode m_mode = Mode::Empty;
    FileNode m_file;
    FileRelation m_relation;
};
