#pragma once

#include "Model.h"

#include <QString>
#include <QStringList>

namespace AnalysisUtil {

bool shouldSkipPath(const QString &path);
bool isPythonFile(const QString &path);
bool isCppFile(const QString &path);
bool isJavaFile(const QString &path);
bool isGoFile(const QString &path);
QStringList scanFiles(const QString &rootDir);
void finalize(AnalysisResult &result);
AnalysisResult merge(AnalysisResult a, const AnalysisResult &b);

} // namespace AnalysisUtil
