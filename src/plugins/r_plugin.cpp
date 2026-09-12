#include "AnalyzerPlugin.h"
#include "RAnalyzer.h"

CRA_REGISTER_PLUGIN("r", &RAnalyzer::analyzeDirectory)
