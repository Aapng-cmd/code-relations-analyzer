#include "AnalyzerPlugin.h"
#include "RustAnalyzer.h"

CRA_REGISTER_PLUGIN("rust", &RustAnalyzer::analyzeDirectory)
