#pragma once

#include "Model.h"

#include <QString>

class ProjectAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};
