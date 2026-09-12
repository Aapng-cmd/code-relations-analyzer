#include "AnalyzerPlugin.h"
#include "CppAnalyzer.h"

CRA_REGISTER_PLUGIN("cpp", &CppAnalyzer::analyzeDirectory)
