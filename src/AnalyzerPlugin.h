#pragma once

#include "Model.h"

#include <QString>

static const int CRA_PLUGIN_ABI = 1;

struct AnalyzerPlugin {
    int abi = CRA_PLUGIN_ABI;
    const char *id = nullptr;
    AnalysisResult (*analyze)(const QString &rootDir) = nullptr;
};

#if defined(_WIN32)
#define CRA_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CRA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define CRA_REGISTER_PLUGIN(id_str, fn)                                                                                 \
    extern "C" CRA_PLUGIN_EXPORT AnalyzerPlugin *cra_analyzer_plugin()                                                  \
    {                                                                                                                   \
        static AnalyzerPlugin plugin{CRA_PLUGIN_ABI, id_str, fn};                                                       \
        return &plugin;                                                                                                 \
    }
