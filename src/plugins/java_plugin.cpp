#include "AnalyzerPlugin.h"
#include "JavaAnalyzer.h"

CRA_REGISTER_PLUGIN("java", &JavaAnalyzer::analyzeDirectory)
