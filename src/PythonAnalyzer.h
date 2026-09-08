#pragma once

#include "Model.h"

#include <QString>

class PythonAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};
