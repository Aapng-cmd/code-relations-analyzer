#pragma once

#include "Model.h"

#include <QString>

class HtmlAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};

class CssAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};

class JsAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};

class WebAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};
