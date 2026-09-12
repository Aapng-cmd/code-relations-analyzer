#include "AnalyzerPlugin.h"
#include "PhpAnalyzer.h"

CRA_REGISTER_PLUGIN("php", &PhpAnalyzer::analyzeDirectory)
