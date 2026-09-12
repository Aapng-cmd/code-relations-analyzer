#include "AnalyzerPlugin.h"
#include "WebAnalyzer.h"

CRA_REGISTER_PLUGIN("html", &HtmlAnalyzer::analyzeDirectory)
