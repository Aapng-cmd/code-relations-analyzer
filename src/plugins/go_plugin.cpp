#include "AnalyzerPlugin.h"
#include "GoAnalyzer.h"

CRA_REGISTER_PLUGIN("go", &GoAnalyzer::analyzeDirectory)
