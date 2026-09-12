#include "ProjectAnalyzer.h"
#include "AnalysisUtil.h"
#include "CppAnalyzer.h"
#include "GoAnalyzer.h"
#include "JavaAnalyzer.h"
#include "PythonAnalyzer.h"

AnalysisResult ProjectAnalyzer::analyzeDirectory(const QString &rootDir)
{
    AnalysisResult result = AnalysisUtil::merge(PythonAnalyzer::analyzeDirectory(rootDir),
                                                CppAnalyzer::analyzeDirectory(rootDir));
    result = AnalysisUtil::merge(result, JavaAnalyzer::analyzeDirectory(rootDir));
    return AnalysisUtil::merge(result, GoAnalyzer::analyzeDirectory(rootDir));
}
