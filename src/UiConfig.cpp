#include "UiConfig.h"

#include "ConfigPaths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QStringList toStringList(const QJsonArray &arr)
{
    QStringList out;
    for (const QJsonValue &v : arr) {
        if (v.isString())
            out << v.toString();
    }
    return out;
}

} // namespace

UiConfig &UiConfig::mutableGet()
{
    static UiConfig cfg;
    return cfg;
}

const UiConfig &UiConfig::get()
{
    return mutableGet();
}

void UiConfig::load()
{
    UiConfig &cfg = mutableGet();
    cfg.ignoredFileNames = QStringList() << QStringLiteral("__init__.py");
    cfg.skipPathParts = QStringList() << QStringLiteral("__pycache__") << QStringLiteral(".venv")
                                     << QStringLiteral("venv") << QStringLiteral(".git") << QStringLiteral("build")
                                     << QStringLiteral("CMakeFiles") << QStringLiteral(".cache")
                                     << QStringLiteral("cmake-build-debug") << QStringLiteral("cmake-build-release")
                                     << QStringLiteral("target") << QStringLiteral(".gradle") << QStringLiteral("vendor")
                                     << QStringLiteral("node_modules");
    cfg.scanGlobs = QStringList() << QStringLiteral("*.py") << QStringLiteral("*.c") << QStringLiteral("*.cc")
                                 << QStringLiteral("*.cpp") << QStringLiteral("*.cxx") << QStringLiteral("*.h")
                                 << QStringLiteral("*.hh") << QStringLiteral("*.hpp") << QStringLiteral("*.hxx")
                                 << QStringLiteral("*.java") << QStringLiteral("*.go");

    const QString path = ConfigPaths::findFile(QStringLiteral("config.json"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject o = doc.object();
    auto num = [&](const char *key, qreal fallback) {
        return o.contains(QLatin1String(key)) ? o.value(QLatin1String(key)).toDouble(fallback) : fallback;
    };
    auto integer = [&](const char *key, int fallback) {
        return o.contains(QLatin1String(key)) ? o.value(QLatin1String(key)).toInt(fallback) : fallback;
    };

    cfg.windowWidth = integer("windowWidth", cfg.windowWidth);
    cfg.windowHeight = integer("windowHeight", cfg.windowHeight);
    cfg.reserveHeight = integer("reserveHeight", cfg.reserveHeight);
    cfg.infoPanelMinWidth = integer("infoPanelMinWidth", cfg.infoPanelMinWidth);
    cfg.settingsMinWidth = integer("settingsMinWidth", cfg.settingsMinWidth);
    cfg.graphMinWidth = integer("graphMinWidth", cfg.graphMinWidth);
    cfg.graphMinHeight = integer("graphMinHeight", cfg.graphMinHeight);
    cfg.nodeWidth = num("nodeWidth", cfg.nodeWidth);
    cfg.nodeHeight = num("nodeHeight", cfg.nodeHeight);
    cfg.nodeCornerRadius = num("nodeCornerRadius", cfg.nodeCornerRadius);
    cfg.nodeTitlePointSize = integer("nodeTitlePointSize", cfg.nodeTitlePointSize);
    cfg.nodeSubtitlePointSize = integer("nodeSubtitlePointSize", cfg.nodeSubtitlePointSize);
    cfg.nodePadding = num("nodePadding", cfg.nodePadding);
    cfg.layoutXGap = num("layoutXGap", cfg.layoutXGap);
    cfg.layoutYGap = num("layoutYGap", cfg.layoutYGap);
    cfg.layoutMinGap = num("layoutMinGap", cfg.layoutMinGap);
    cfg.scenePaddingBlocks = num("scenePaddingBlocks", cfg.scenePaddingBlocks);
    cfg.edgeHitWidth = num("edgeHitWidth", cfg.edgeHitWidth);
    cfg.edgeArrowSize = num("edgeArrowSize", cfg.edgeArrowSize);
    cfg.edgeLabelPointSize = integer("edgeLabelPointSize", cfg.edgeLabelPointSize);
    cfg.edgePenWidth = num("edgePenWidth", cfg.edgePenWidth);
    cfg.edgePenWidthSelected = num("edgePenWidthSelected", cfg.edgePenWidthSelected);
    cfg.visibleOpacity = num("visibleOpacity", cfg.visibleOpacity);
    cfg.hiddenOpacity = num("hiddenOpacity", cfg.hiddenOpacity);
    cfg.eyeAnimationMs = integer("eyeAnimationMs", cfg.eyeAnimationMs);
    cfg.eyeSize = num("eyeSize", cfg.eyeSize);
    cfg.eyeHitSize = num("eyeHitSize", cfg.eyeHitSize);
    cfg.eyeMargin = num("eyeMargin", cfg.eyeMargin);
    cfg.pathFontPointSize = integer("pathFontPointSize", cfg.pathFontPointSize);
    cfg.infoPanelMargins = integer("infoPanelMargins", cfg.infoPanelMargins);
    cfg.treeIndent = integer("treeIndent", cfg.treeIndent);
    cfg.zoomStep = num("zoomStep", cfg.zoomStep);
    cfg.zoomMin = num("zoomMin", cfg.zoomMin);
    cfg.zoomMax = num("zoomMax", cfg.zoomMax);
    cfg.edgeCurveBase = num("edgeCurveBase", cfg.edgeCurveBase);
    cfg.edgeCurveStep = num("edgeCurveStep", cfg.edgeCurveStep);
    if (o.contains(QLatin1String("showOnlyConnectedFiles")))
        cfg.showOnlyConnectedFiles = o.value(QLatin1String("showOnlyConnectedFiles")).toBool(true);
    if (o.contains(QLatin1String("ignoredFileNames")))
        cfg.ignoredFileNames = toStringList(o.value(QLatin1String("ignoredFileNames")).toArray());
    if (o.contains(QLatin1String("skipPathParts")))
        cfg.skipPathParts = toStringList(o.value(QLatin1String("skipPathParts")).toArray());
    if (o.contains(QLatin1String("scanGlobs")))
        cfg.scanGlobs = toStringList(o.value(QLatin1String("scanGlobs")).toArray());
}
