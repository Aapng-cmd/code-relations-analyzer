#pragma once

#include "Model.h"

#include <QString>

class RAnalyzer {
public:
    static AnalysisResult analyzeDirectory(const QString &rootDir);
};
