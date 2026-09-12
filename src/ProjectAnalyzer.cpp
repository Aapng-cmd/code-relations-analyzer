#include "ProjectAnalyzer.h"

#include "AnalysisUtil.h"
#include "PluginHost.h"

#include <QDebug>

AnalysisResult ProjectAnalyzer::analyzeDirectory(const QString &rootDir)
{
    PluginHost::instance().load();
    if (PluginHost::instance().loadedIds().isEmpty())
        qWarning("No analyzer plugins found (expected libcra-*.so in a plugins/ directory next to the executable)");
    AnalysisResult result = PluginHost::instance().analyzeDirectory(rootDir);
    for (FileNode &file : result.files)
        file.language = AnalysisUtil::languageOf(file.path);
    AnalysisUtil::finalize(result);
    return result;
}
