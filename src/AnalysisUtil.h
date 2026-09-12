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
bool isRustFile(const QString &path);
bool isRFile(const QString &path);
bool isPhpFile(const QString &path);
bool isHtmlFile(const QString &path);
bool isCssFile(const QString &path);
bool isJsFile(const QString &path);
bool isWebFile(const QString &path);
bool isExternalRef(const QString &spec);
bool isMinifiedAsset(const QString &path);
QString cleanRef(const QString &spec);
QString resolveRelative(const QString &fromFile, const QString &spec, const QString &rootDir);
QString resolveWebAsset(const QString &fromFile, const QString &spec, const QString &rootDir);
bool isWebLanguage(SourceLanguage language);
bool isWebBackendLanguage(SourceLanguage language);
SourceLanguage languageOf(const QString &path);
QString languageKey(SourceLanguage language);
SourceLanguage languageFromKey(const QString &key);
QStringList scanFiles(const QString &rootDir);
void finalize(AnalysisResult &result);
AnalysisResult merge(AnalysisResult a, const AnalysisResult &b);

} // namespace AnalysisUtil
