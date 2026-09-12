#pragma once

#include "Model.h"

#include <QString>

class GoAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};
