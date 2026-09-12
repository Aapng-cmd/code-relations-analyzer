#include "ProjectAnalyzer.h"
#include "AnalysisUtil.h"
#include "CppAnalyzer.h"
#include "PythonAnalyzer.h"

AnalysisResult ProjectAnalyzer::analyzeDirectory(const QString &rootDir)
{
    return AnalysisUtil::merge(PythonAnalyzer::analyzeDirectory(rootDir), CppAnalyzer::analyzeDirectory(rootDir));
}
