#include "PluginHost.h"

#include "AnalysisUtil.h"
#include "ConfigPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QSet>
#include <QtAlgorithms>

namespace {

QStringList pluginSearchDirs()
{
    QStringList dirs;
    const QString exe = QCoreApplication::applicationDirPath();
    dirs << exe + QStringLiteral("/plugins");
    dirs << exe;
    dirs << exe + QStringLiteral("/../plugins");
    for (const QString &configDir : ConfigPaths::searchDirs())
        dirs << QFileInfo(configDir).absolutePath() + QStringLiteral("/plugins");
    dirs.removeDuplicates();
    return dirs;
}

} // namespace

PluginHost &PluginHost::instance()
{
    static PluginHost host;
    return host;
}

PluginHost::~PluginHost()
{
    qDeleteAll(m_libs);
}

void PluginHost::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    QSet<QString> seenIds;
    QSet<QString> seenFiles;
    const QStringList nameFilters = QStringList() << QStringLiteral("libcra-*.so") << QStringLiteral("cra-*.so");

    for (const QString &dirPath : pluginSearchDirs()) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        const QStringList entries = dir.entryList(nameFilters, QDir::Files);
        for (const QString &entry : entries) {
            if (entry.contains(QLatin1String("cra-core")))
                continue;
            const QString path = dir.absoluteFilePath(entry);
            const QString canonical = QFileInfo(path).canonicalFilePath();
            if (canonical.isEmpty() || seenFiles.contains(canonical))
                continue;
            seenFiles.insert(canonical);

            auto *lib = new QLibrary(path);
            lib->setLoadHints(QLibrary::PreventUnloadHint);
            if (!lib->load()) {
                delete lib;
                continue;
            }
            auto entryFn = reinterpret_cast<AnalyzerPlugin *(*)()>(lib->resolve("cra_analyzer_plugin"));
            if (!entryFn) {
                lib->unload();
                delete lib;
                continue;
            }
            AnalyzerPlugin *plugin = entryFn();
            if (!plugin || plugin->abi != CRA_PLUGIN_ABI || !plugin->id || !plugin->analyze) {
                lib->unload();
                delete lib;
                continue;
            }
            const QString id = QString::fromUtf8(plugin->id);
            if (id.isEmpty() || seenIds.contains(id)) {
                lib->unload();
                delete lib;
                continue;
            }
            seenIds.insert(id);
            m_libs.push_back(lib);
            m_plugins.push_back(plugin);
        }
    }
}

AnalysisResult PluginHost::analyzeDirectory(const QString &rootDir) const
{
    AnalysisResult result;
    for (const AnalyzerPlugin *plugin : m_plugins) {
        if (!plugin || !plugin->analyze)
            continue;
        result = AnalysisUtil::merge(result, plugin->analyze(rootDir));
    }
    return result;
}

QStringList PluginHost::loadedIds() const
{
    QStringList ids;
    for (const AnalyzerPlugin *plugin : m_plugins) {
        if (plugin && plugin->id)
            ids << QString::fromUtf8(plugin->id);
    }
    ids.sort();
    return ids;
}
