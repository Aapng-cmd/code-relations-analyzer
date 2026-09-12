#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QWidget>
#include <QtMath>

namespace {
AppTheme g_theme = AppTheme::Light;

int colorDistance(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
}

NodeTint makeTint(const QColor &bg, const QColor &border)
{
    NodeTint t;
    t.bg = bg;
    t.border = border;
    t.bgSelected = g_theme == AppTheme::Dark ? bg.lighter(128) : bg.darker(108);
    return t;
}

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

ThemeColors g_colors = makeLight();
} // namespace

AppTheme Theme::current()
{
    return g_theme;
}

const ThemeColors &Theme::colors()
{
    return g_colors;
}

NodeTint Theme::defaultNodeTint()
{
    const ThemeColors c = colors();
    NodeTint t;
    t.bg = c.nodeBg;
    t.bgSelected = c.nodeBgSelected;
    t.border = c.nodeBorder;
    return t;
}

NodeTint Theme::languageTint(SourceLanguage language)
{
    const bool dark = current() == AppTheme::Dark;
    switch (language) {
    case SourceLanguage::Python:
        return dark ? makeTint(QColor("#4a3d1f"), QColor("#e2c36b")) : makeTint(QColor("#f6e7a8"), QColor("#9a7420"));
    case SourceLanguage::Cpp:
        return dark ? makeTint(QColor("#24344c"), QColor("#8cbcff")) : makeTint(QColor("#c9dcf5"), QColor("#3d6ea8"));
    case SourceLanguage::Java:
        return dark ? makeTint(QColor("#4a3224"), QColor("#e09b72")) : makeTint(QColor("#f5c9b0"), QColor("#c25b2a"));
    case SourceLanguage::Go:
        return dark ? makeTint(QColor("#1c3d38"), QColor("#6dc4b3")) : makeTint(QColor("#bfe8dc"), QColor("#2a8f7a"));
    case SourceLanguage::Rust:
        return dark ? makeTint(QColor("#4a2f22"), QColor("#e0a070")) : makeTint(QColor("#f3d0bc"), QColor("#b85c2a"));
    case SourceLanguage::R:
        return dark ? makeTint(QColor("#1e3050"), QColor("#7eb0e8")) : makeTint(QColor("#c5d8f0"), QColor("#2a5a9a"));
    case SourceLanguage::Php:
        return dark ? makeTint(QColor("#35284a"), QColor("#c4b0ff")) : makeTint(QColor("#ddd0f8"), QColor("#6a40c0"));
    case SourceLanguage::Html:
        return dark ? makeTint(QColor("#4a2430"), QColor("#f0a0b8")) : makeTint(QColor("#f4b8c8"), QColor("#c03860"));
    case SourceLanguage::Css:
        return dark ? makeTint(QColor("#1e3848"), QColor("#7ec8e8")) : makeTint(QColor("#c5e4f4"), QColor("#2a7aa0"));
    case SourceLanguage::JavaScript:
        return dark ? makeTint(QColor("#4a4418"), QColor("#e8d060")) : makeTint(QColor("#f3e6a0"), QColor("#9a8020"));
    default:
        return defaultNodeTint();
    }
}

QVector<NodeTint> Theme::groupPalette()
{
    const bool dark = current() == AppTheme::Dark;
    QVector<NodeTint> out;
    struct Pair {
        const char *bg;
        const char *bd;
        const char *dbg;
        const char *dbd;
    };
    const Pair pairs[] = {
        {"#e7c0d6", "#9a3d7a", "#4a2a3d", "#e7a0c8"},
        {"#d5c7f0", "#6b4aa8", "#32284a", "#c2b0ff"},
        {"#c5e4a8", "#4a8a28", "#2c4020", "#b0e080"},
        {"#f3c48a", "#b86a18", "#4a3418", "#f0b060"},
        {"#a8d4e8", "#2a7aa0", "#1e3848", "#80c8e0"},
        {"#e8d5a8", "#8a7020", "#403820", "#e0d080"},
        {"#e8b8b8", "#a84848", "#4a2828", "#e09090"},
        {"#b8c8e8", "#4868a8", "#28344a", "#90a8e0"},
        {"#a8e0c8", "#2a8a60", "#204838", "#80d0b0"},
        {"#e0c8e8", "#8a48a0", "#402848", "#d0a0e0"},
        {"#d0d0a8", "#6a6a28", "#383820", "#d0d070"},
        {"#c8b8a0", "#7a6040", "#3a3020", "#d0b890"},
    };
    for (const Pair &p : pairs)
        out.push_back(dark ? makeTint(QColor(p.dbg), QColor(p.dbd)) : makeTint(QColor(p.bg), QColor(p.bd)));
    return out;
}

NodeTint Theme::pickUnusedTint(const QVector<QColor> &usedBg)
{
    const QVector<NodeTint> palette = groupPalette();
    for (const NodeTint &cand : palette) {
        bool clash = false;
        for (const QColor &used : usedBg) {
            if (colorDistance(cand.bg, used) < 90) {
                clash = true;
                break;
            }
        }
        if (!clash)
            return cand;
    }
    return palette.isEmpty() ? defaultNodeTint() : palette.first();
}

void Theme::setCurrent(AppTheme theme)
{
    g_theme = theme;
    g_colors = g_theme == AppTheme::Dark ? makeDark() : makeLight();
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
               "QMenuBar { background-color: %1; color: %2; border-bottom: 1px solid %4; }"
               "QMenuBar::item { background: transparent; color: %2; padding: 6px 12px; }"
               "QMenuBar::item:selected { background-color: %5; }"
               "QMenuBar::item:pressed { background-color: %3; }"
               "QMenu { background-color: %3; color: %2; border: 1px solid %4; }"
               "QMenu::item { padding: 6px 18px; color: %2; }"
               "QMenu::item:disabled { color: %6; }"
               "QMenu::indicator { width: 14px; height: 14px; }"
               "QLabel { background: transparent; color: %2; }"
               "QStatusBar { background-color: %1; color: %6; }"
               "QTreeWidget { background-color: %3; color: %2; border: 1px solid %4; border-radius: 6px; }"
               "QTreeWidget::item { color: %2; padding: 4px 2px; }"
               "QTreeWidget::item:selected { background-color: %5; color: %2; }"
               "QHeaderView, QAbstractScrollArea { background-color: %3; color: %2; }"
               "QScrollBar:vertical, QScrollBar:horizontal { background: %1; }"
               "QGraphicsView { background-color: %1; border: none; }"
               "QDialog { background-color: %1; color: %2; }"
               "QPushButton { background-color: %5; color: %2; padding: 6px 12px; border: 1px solid %4; border-radius: 4px; }"
               "QPushButton:hover { background-color: %3; }"
               "QCheckBox, QGroupBox { color: %2; background: transparent; }"
               "QRadioButton { color: %2; background: transparent; }")
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
