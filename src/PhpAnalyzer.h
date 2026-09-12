#pragma once

#include "Model.h"

#include <QString>

class PhpAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};
