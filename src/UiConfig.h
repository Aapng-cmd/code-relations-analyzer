#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

class UiConfig {
public:
    static void load();
    static const UiConfig &get();

    int windowWidth = 1100;
    int windowHeight = 720;
    int reserveHeight = 90;
    int infoPanelMinWidth = 240;
    int settingsMinWidth = 360;
    int graphMinWidth = 400;
    int graphMinHeight = 280;
    qreal nodeWidth = 170;
    qreal nodeHeight = 70;
    qreal nodeCornerRadius = 12;
    int nodeTitlePointSize = 11;
    int nodeSubtitlePointSize = 8;
    qreal nodePadding = 8;
    qreal layoutXGap = 240;
    qreal layoutYGap = 140;
    qreal layoutMinGap = 36;
    qreal scenePaddingBlocks = 5;
    qreal edgeHitWidth = 22;
    qreal edgeArrowSize = 14;
    int edgeLabelPointSize = 8;
    qreal edgePenWidth = 2.6;
    qreal edgePenWidthSelected = 3.4;
    qreal visibleOpacity = 1.0;
    qreal hiddenOpacity = 0.4;
    int eyeAnimationMs = 180;
    qreal eyeSize = 18;
    qreal eyeHitSize = 28;
    qreal eyeMargin = 4;
    int pathFontPointSize = 8;
    int infoPanelMargins = 10;
    int treeIndent = 16;
    qreal zoomStep = 1.15;
    qreal zoomMin = 0.2;
    qreal zoomMax = 4.0;
    qreal edgeCurveBase = 36;
    qreal edgeCurveStep = 22;
    bool showOnlyConnectedFiles = true;
    QStringList ignoredFileNames;
    QStringList skipPathParts;
    QStringList scanGlobs;

private:
    static UiConfig &mutableGet();
};
