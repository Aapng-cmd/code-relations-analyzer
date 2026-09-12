#include "AnalyzerPlugin.h"
#include "PythonAnalyzer.h"

CRA_REGISTER_PLUGIN("python", &PythonAnalyzer::analyzeDirectory)
