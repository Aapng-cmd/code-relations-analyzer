#include "InfoPanel.h"
#include "I18n.h"
#include "Theme.h"
#include "UiConfig.h"

#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QTreeWidget>
#include <QVBoxLayout>

InfoPanel::InfoPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_path = new QLabel;
    m_path->setWordWrap(true);
    QFont pf = m_path->font();
    pf.setPointSize(UiConfig::get().pathFontPointSize);
    m_path->setFont(pf);

    m_title = new QLabel;
    QFont tf = m_title->font();
    tf.setBold(true);
    tf.setPointSize(tf.pointSize() + 1);
    m_title->setFont(tf);
    m_title->setWordWrap(true);

    m_subtitle = new QLabel;
    m_subtitle->setWordWrap(true);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setAnimated(true);
    m_tree->setIndentation(16);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setItemsExpandable(false);
    connect(m_tree, &QTreeWidget::itemClicked, this, [](QTreeWidgetItem *item) {
        if (item && item->childCount() > 0)
            item->setExpanded(!item->isExpanded());
    });

    layout->addWidget(m_path);
    layout->addWidget(m_title);
    layout->addWidget(m_subtitle);
    layout->addWidget(m_tree, 1);
    setMinimumWidth(UiConfig::get().infoPanelMinWidth);
    retranslate();
}

void InfoPanel::clearInfo()
{
    m_mode = Mode::Empty;
    m_file = FileNode();
    m_relation = FileRelation();
    m_tree->clear();
    retranslate();
}

void InfoPanel::retranslate()
{
    if (m_mode == Mode::File) {
        m_path->setText(m_file.path);
        m_path->setVisible(!m_file.path.isEmpty());
        m_title->setText(m_file.fileName);
        m_subtitle->setText(I18n::t(QStringLiteral("file_hint")));
        fillTree(m_file.symbols);
        return;
    }
    if (m_mode == Mode::Relation) {
        m_path->setText(QStringLiteral("%1\n%2").arg(m_relation.fromPath, m_relation.toPath));
        m_path->setVisible(true);
        m_title->setText(QStringLiteral("%1 → %2").arg(m_relation.fromFileName, m_relation.toFileName));
        m_subtitle->setText(I18n::t(QStringLiteral("relation_hint")));
        fillTree(m_relation.used);
        return;
    }
    m_path->clear();
    m_path->setVisible(false);
    m_title->setText(I18n::t(QStringLiteral("selection")));
    m_subtitle->setText(I18n::t(QStringLiteral("click_file_or_arrow")));
}

void InfoPanel::applyTheme()
{
    const ThemeColors c = Theme::colors();
    m_path->setStyleSheet(QStringLiteral("color: %1;").arg(c.muted.name()));
    m_subtitle->setStyleSheet(QStringLiteral("color: %1;").arg(c.muted.name()));
}

QTreeWidgetItem *InfoPanel::ensureGroup(QTreeWidgetItem *parent, const QString &title, bool expanded)
{
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, title);
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    item->setExpanded(expanded);
    return item;
}

void InfoPanel::addGrouped(QTreeWidgetItem *parent, const QString &parentQualified,
                           const QMap<QString, QVector<DefinedSymbol>> &children, bool expandGroups)
{
    const QVector<DefinedSymbol> items = children.value(parentQualified);
    QVector<DefinedSymbol> classes;
    QVector<DefinedSymbol> functions;
    QVector<DefinedSymbol> variables;
    for (const DefinedSymbol &s : items) {
        if (s.kind == SymbolKind::Class)
            classes.push_back(s);
        else if (s.kind == SymbolKind::Function)
            functions.push_back(s);
        else
            variables.push_back(s);
    }

    auto addBranch = [&](const QString &title, const QVector<DefinedSymbol> &syms, bool leafKind) {
        if (syms.isEmpty())
            return;
        QTreeWidgetItem *group = ensureGroup(parent, title, expandGroups);
        for (const DefinedSymbol &s : syms) {
            if (leafKind && s.kind == SymbolKind::Variable && !children.contains(s.qualifiedName)) {
                auto *leaf = new QTreeWidgetItem(group);
                leaf->setText(0, s.display);
                QFont f = leaf->font(0);
                f.setFamily(QStringLiteral("monospace"));
                leaf->setFont(0, f);
                continue;
            }
            QTreeWidgetItem *node = ensureGroup(group, s.display, false);
            QFont f = node->font(0);
            f.setBold(s.kind != SymbolKind::Variable);
            f.setFamily(QStringLiteral("monospace"));
            node->setFont(0, f);
            addGrouped(node, s.qualifiedName, children, true);
        }
    };

    addBranch(I18n::t(QStringLiteral("group_classes")), classes, false);
    addBranch(I18n::t(QStringLiteral("group_functions")), functions, false);
    addBranch(I18n::t(QStringLiteral("group_variables")), variables, true);
}

void InfoPanel::fillTree(const QVector<DefinedSymbol> &symbols)
{
    m_tree->clear();
    QMap<QString, QVector<DefinedSymbol>> children;
    for (const DefinedSymbol &s : symbols)
        children[s.parentQualified].push_back(s);
    addGrouped(nullptr, QString(), children, true);
}

void InfoPanel::showRelation(const FileRelation &relation)
{
    m_mode = Mode::Relation;
    m_relation = relation;
    retranslate();
}

void InfoPanel::showFile(const FileNode &file)
{
    m_mode = Mode::File;
    m_file = file;
    retranslate();
}
