#pragma once

#include <QColor>
#include <QString>

class QWidget;

enum class AppTheme { Light, Dark };

struct ThemeColors {
    QColor window;
    QColor windowText;
    QColor base;
    QColor text;
    QColor muted;
    QColor border;
    QColor button;
    QColor graphBg;
    QColor nodeBg;
    QColor nodeBgSelected;
    QColor nodeBorder;
    QColor nodeBorderSelected;
    QColor nodeText;
    QColor nodeMuted;
    QColor edge;
    QColor edgeSelected;
    QColor edgeLabelBg;
};

class Theme {
public:
    static AppTheme current();
    static void setCurrent(AppTheme theme);
    static ThemeColors colors();
    static QString globalStyleSheet();
    static void applyTo(QWidget *root);
};
