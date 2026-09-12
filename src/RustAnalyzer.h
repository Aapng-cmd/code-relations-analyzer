#pragma once

#include "Model.h"

#include <QString>

class RustAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};
