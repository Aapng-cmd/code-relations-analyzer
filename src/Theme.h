#pragma once

#include "Model.h"

#include <QColor>
#include <QString>
#include <QVector>

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

struct NodeTint {
    QColor bg;
    QColor bgSelected;
    QColor border;

    bool operator==(const NodeTint &other) const
    {
        return bg == other.bg && bgSelected == other.bgSelected && border == other.border;
    }
    bool operator!=(const NodeTint &other) const { return !(*this == other); }
};

class Theme {
public:
    static AppTheme current();
    static void setCurrent(AppTheme theme);
    static const ThemeColors &colors();
    static NodeTint defaultNodeTint();
    static NodeTint languageTint(SourceLanguage language);
    static QVector<NodeTint> groupPalette();
    static NodeTint pickUnusedTint(const QVector<QColor> &usedBg);
    static QString globalStyleSheet();
    static void applyTo(QWidget *root);
};
