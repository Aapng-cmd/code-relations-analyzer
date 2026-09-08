#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QWidget>

namespace {
AppTheme g_theme = AppTheme::Light;

ThemeColors makeLight()
{
    ThemeColors c;
    c.window = QColor("#f4f6fa");
    c.windowText = QColor("#1c2330");
    c.base = QColor("#ffffff");
    c.text = QColor("#1c2330");
    c.muted = QColor("#5b6575");
    c.border = QColor("#c9d0dc");
    c.button = QColor("#e6ebf3");
    c.graphBg = QColor("#e8ebf1");
    c.nodeBg = QColor("#ffffff");
    c.nodeBgSelected = QColor("#dce8ff");
    c.nodeBorder = QColor("#4d6a93");
    c.nodeBorderSelected = QColor("#1f63d6");
    c.nodeText = QColor("#1c2330");
    c.nodeMuted = QColor("#5b6575");
    c.edge = QColor("#1e3a5f");
    c.edgeSelected = QColor("#0b57d0");
    c.edgeLabelBg = QColor("#ffffff");
    return c;
}

ThemeColors makeDark()
{
    ThemeColors c;
    c.window = QColor("#1b1f27");
    c.windowText = QColor("#e8edf2");
    c.base = QColor("#141820");
    c.text = QColor("#e8edf2");
    c.muted = QColor("#9aa3b5");
    c.border = QColor("#3a4250");
    c.button = QColor("#2a3140");
    c.graphBg = QColor("#12151c");
    c.nodeBg = QColor("#2a3344");
    c.nodeBgSelected = QColor("#3b4d6b");
    c.nodeBorder = QColor("#7ea0d4");
    c.nodeBorderSelected = QColor("#8cbcff");
    c.nodeText = QColor("#f2f4f8");
    c.nodeMuted = QColor("#a7b4c6");
    c.edge = QColor("#d5e4f7");
    c.edgeSelected = QColor("#8cbcff");
    c.edgeLabelBg = QColor("#2a3344");
    return c;
}
} // namespace

AppTheme Theme::current()
{
    return g_theme;
}

ThemeColors Theme::colors()
{
    return g_theme == AppTheme::Dark ? makeDark() : makeLight();
}

void Theme::setCurrent(AppTheme theme)
{
    g_theme = theme;
}

QString Theme::globalStyleSheet()
{
    const ThemeColors c = colors();
    const QString win = c.window.name();
    const QString text = c.windowText.name();
    const QString base = c.base.name();
    const QString muted = c.muted.name();
    const QString border = c.border.name();
    const QString button = c.button.name();
    return QStringLiteral(
               "QWidget { background-color: %1; color: %2; }"
               "QMainWindow, QSplitter, QSplitter::handle { background-color: %1; color: %2; }"
               "QToolBar { background-color: %1; border: none; spacing: 8px; padding: 4px; }"
               "QToolBar QToolButton, QToolButton { background-color: %5; color: %2; padding: 6px 10px; border: 1px solid %4; border-radius: 4px; }"
               "QToolBar QToolButton:hover, QMenu::item:selected { background-color: %3; }"
               "QMenu { background-color: %3; color: %2; border: 1px solid %4; }"
               "QMenu::item { padding: 6px 18px; color: %2; }"
               "QLabel { background: transparent; color: %2; }"
               "QStatusBar { background-color: %1; color: %6; }"
               "QTreeWidget { background-color: %3; color: %2; border: 1px solid %4; border-radius: 6px; }"
               "QTreeWidget::item { color: %2; padding: 4px 2px; }"
               "QTreeWidget::item:selected { background-color: %5; color: %2; }"
               "QHeaderView, QAbstractScrollArea { background-color: %3; color: %2; }"
               "QScrollBar:vertical, QScrollBar:horizontal { background: %1; }"
               "QGraphicsView { background-color: %1; border: none; }")
        .arg(win, text, base, border, button, muted);
}

void Theme::applyTo(QWidget *root)
{
    const ThemeColors c = colors();
    QPalette pal;
    pal.setColor(QPalette::Window, c.window);
    pal.setColor(QPalette::WindowText, c.windowText);
    pal.setColor(QPalette::Base, c.base);
    pal.setColor(QPalette::Text, c.text);
    pal.setColor(QPalette::Button, c.button);
    pal.setColor(QPalette::ButtonText, c.windowText);
    pal.setColor(QPalette::Highlight, c.nodeBgSelected);
    pal.setColor(QPalette::HighlightedText, c.windowText);
    pal.setColor(QPalette::ToolTipBase, c.base);
    pal.setColor(QPalette::ToolTipText, c.text);
    if (qApp)
        qApp->setPalette(pal);
    if (root)
        root->setStyleSheet(globalStyleSheet());
}
