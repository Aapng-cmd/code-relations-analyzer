#pragma once

#include "Model.h"

#include <QString>

class JavaAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};
