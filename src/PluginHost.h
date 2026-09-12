#pragma once

#include "AnalyzerPlugin.h"

#include <QString>
#include <QStringList>
#include <QVector>

class QLibrary;

class PluginHost {
public:
    static PluginHost &instance();

    void load();
    AnalysisResult analyzeDirectory(const QString &rootDir) const;
    QStringList loadedIds() const;

private:
    PluginHost() = default;
    ~PluginHost();

    bool m_loaded = false;
    QVector<QLibrary *> m_libs;
    QVector<const AnalyzerPlugin *> m_plugins;
};
