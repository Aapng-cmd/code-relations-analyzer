#include "AnalysisUtil.h"
#include "UiConfig.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <QSet>

#include <algorithm>

namespace AnalysisUtil {

bool shouldSkipPath(const QString &path)
{
    const UiConfig &cfg = UiConfig::get();
    const QString norm = QDir::fromNativeSeparators(path);
    for (const QString &part : cfg.skipPathParts) {
        if (norm.contains(QLatin1Char('/') + part + QLatin1Char('/')) || norm.endsWith(QLatin1Char('/') + part))
            return true;
    }
    return false;
}

bool isPythonFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("py"), Qt::CaseInsensitive) == 0;
}

bool isCppFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("c") || s == QLatin1String("cc") || s == QLatin1String("cpp") || s == QLatin1String("cxx")
           || s == QLatin1String("h") || s == QLatin1String("hh") || s == QLatin1String("hpp")
           || s == QLatin1String("hxx");
}

QStringList scanFiles(const QString &rootDir)
{
    const UiConfig &cfg = UiConfig::get();
    QStringList globs = cfg.scanGlobs;
    if (globs.isEmpty()) {
        globs << QStringLiteral("*.py") << QStringLiteral("*.c") << QStringLiteral("*.cc") << QStringLiteral("*.cpp")
              << QStringLiteral("*.cxx") << QStringLiteral("*.h") << QStringLiteral("*.hh") << QStringLiteral("*.hpp")
              << QStringLiteral("*.hxx");
    }
    QStringList out;
    QDirIterator it(rootDir, globs, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (shouldSkipPath(path))
            continue;
        out << QFileInfo(path).absoluteFilePath();
    }
    return out;
}

void finalize(AnalysisResult &result)
{
    const UiConfig &cfg = UiConfig::get();
    std::sort(result.files.begin(), result.files.end(), [](const FileNode &a, const FileNode &b) {
        return a.fileName < b.fileName;
    });

    QMap<QString, int> indeg;
    QMap<QString, QStringList> outs;
    for (const FileNode &f : result.files)
        indeg[f.path] = 0;
    for (const FileRelation &rel : result.relations) {
        outs[rel.fromPath] << rel.toPath;
        if (indeg.contains(rel.toPath))
            indeg[rel.toPath] += 1;
    }
    QMap<QString, int> rank;
    QStringList ready;
    for (auto it = indeg.begin(); it != indeg.end(); ++it) {
        if (it.value() == 0)
            ready << it.key();
    }
    while (!ready.isEmpty()) {
        const QString u = ready.takeFirst();
        for (const QString &v : outs.value(u)) {
            rank[v] = qMax(rank.value(v, 0), rank.value(u) + 1);
            indeg[v] -= 1;
            if (indeg[v] == 0)
                ready << v;
        }
    }
    std::sort(result.relations.begin(), result.relations.end(), [&](const FileRelation &a, const FileRelation &b) {
        const int ra = rank.value(a.fromPath, 0);
        const int rb = rank.value(b.fromPath, 0);
        if (ra != rb)
            return ra < rb;
        if (a.fromFileName != b.fromFileName)
            return a.fromFileName < b.fromFileName;
        return a.toFileName < b.toFileName;
    });

    QSet<QString> ignoredNames;
    for (const QString &name : cfg.ignoredFileNames)
        ignoredNames.insert(name);
    QSet<QString> connected;
    for (const FileRelation &rel : result.relations) {
        connected.insert(rel.fromPath);
        connected.insert(rel.toPath);
    }
    QVector<FileNode> files;
    QSet<QString> keptPaths;
    for (const FileNode &file : result.files) {
        if (ignoredNames.contains(file.fileName) && !connected.contains(file.path))
            continue;
        if (cfg.showOnlyConnectedFiles && !connected.contains(file.path))
            continue;
        files.push_back(file);
        keptPaths.insert(file.path);
    }
    result.files = files;
    QVector<FileRelation> rels;
    for (const FileRelation &rel : result.relations) {
        if (keptPaths.contains(rel.fromPath) && keptPaths.contains(rel.toPath))
            rels.push_back(rel);
    }
    result.relations = rels;
}

AnalysisResult merge(AnalysisResult a, const AnalysisResult &b)
{
    a.files.append(b.files);
    a.relations.append(b.relations);
    std::sort(a.files.begin(), a.files.end(), [](const FileNode &x, const FileNode &y) {
        return x.fileName < y.fileName;
    });
    std::sort(a.relations.begin(), a.relations.end(), [](const FileRelation &x, const FileRelation &y) {
        if (x.fromFileName != y.fromFileName)
            return x.fromFileName < y.fromFileName;
        return x.toFileName < y.toFileName;
    });
    return a;
}

} // namespace AnalysisUtil
