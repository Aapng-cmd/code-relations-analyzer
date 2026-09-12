#pragma once

#include "Model.h"

#include <QString>

class CppAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};
