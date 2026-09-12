#include "AnalysisUtil.h"
#include "UiConfig.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <QPair>
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

bool isJavaFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("java"), Qt::CaseInsensitive) == 0;
}

bool isGoFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("go"), Qt::CaseInsensitive) == 0;
}

bool isRustFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("rs"), Qt::CaseInsensitive) == 0;
}

bool isRFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("r") || s == QLatin1String("rmd");
}

bool isPhpFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("php") || s == QLatin1String("phtml") || s == QLatin1String("php5");
}

bool isHtmlFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("html") || s == QLatin1String("htm");
}

bool isCssFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("css"), Qt::CaseInsensitive) == 0;
}

bool isJsFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("js") || s == QLatin1String("mjs");
}

bool isWebFile(const QString &path)
{
    return isHtmlFile(path) || isCssFile(path) || isJsFile(path);
}

bool isExternalRef(const QString &spec)
{
    const QString s = spec.trimmed().toLower();
    return s.startsWith(QLatin1String("http:")) || s.startsWith(QLatin1String("https:"))
           || s.startsWith(QLatin1String("//")) || s.startsWith(QLatin1String("data:"))
           || s.startsWith(QLatin1String("mailto:")) || s.startsWith(QLatin1String("javascript:"));
}

bool isMinifiedAsset(const QString &path)
{
    const QString name = QFileInfo(path).fileName().toLower();
    return name.contains(QLatin1String(".min.")) || name.endsWith(QLatin1String(".min.js"))
           || name.endsWith(QLatin1String(".min.css")) || name.endsWith(QLatin1String(".min.mjs"));
}

QString cleanRef(const QString &spec)
{
    QString s = spec.trimmed();
    if ((s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
        || (s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\''))))
        s = s.mid(1, s.size() - 2);
    const int hash = s.indexOf(QLatin1Char('#'));
    if (hash >= 0)
        s = s.left(hash);
    const int query = s.indexOf(QLatin1Char('?'));
    if (query >= 0)
        s = s.left(query);
    return s.trimmed();
}

QString resolveRelative(const QString &fromFile, const QString &spec, const QString &rootDir)
{
    const QString ref = cleanRef(spec);
    if (ref.isEmpty() || isExternalRef(ref))
        return {};
    const QString root = QDir(rootDir).absolutePath();
    const QString fromDir = QFileInfo(fromFile).absolutePath();
    QStringList candidates;
    if (ref.startsWith(QLatin1Char('/'))) {
        candidates << QFileInfo(QDir(root).filePath(ref.mid(1))).absoluteFilePath();
        candidates << QFileInfo(QDir(fromDir).filePath(ref.mid(1))).absoluteFilePath();
    }
    candidates << QFileInfo(QDir(fromDir).filePath(ref)).absoluteFilePath();
    candidates << QFileInfo(QDir(root).filePath(ref)).absoluteFilePath();
    for (const QString &cand : candidates) {
        if (QFileInfo::exists(cand) && !shouldSkipPath(cand))
            return cand;
    }
    return {};
}

QStringList webAssetSubdirs()
{
    return QStringList() << QStringLiteral("templates") << QStringLiteral("views") << QStringLiteral("static")
                         << QStringLiteral("public") << QStringLiteral("www") << QStringLiteral("html")
                         << QStringLiteral("resources") << QStringLiteral("frontend") << QStringLiteral("client")
                         << QStringLiteral("assets") << QStringLiteral("tmpl") << QStringLiteral("web")
                         << QStringLiteral("dist");
}

QString findNamedUnder(const QString &dir, const QString &relative, int depth)
{
    if (depth > 5 || dir.isEmpty() || shouldSkipPath(dir))
        return {};
    const QString direct = QFileInfo(QDir(dir).filePath(relative)).absoluteFilePath();
    if (QFileInfo::exists(direct) && !shouldSkipPath(direct))
        return direct;
    if (depth >= 5)
        return {};
    QDir d(dir);
    const QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subs) {
        const QString hit = findNamedUnder(d.filePath(sub), relative, depth + 1);
        if (!hit.isEmpty())
            return hit;
    }
    return {};
}

QString resolveWebAsset(const QString &fromFile, const QString &spec, const QString &rootDir)
{
    const QString ref = cleanRef(spec);
    if (ref.isEmpty() || isExternalRef(ref))
        return {};
    const QString suffix = QFileInfo(ref).suffix().toLower();
    const bool hasWebSuffix = suffix == QLatin1String("html") || suffix == QLatin1String("htm")
                              || suffix == QLatin1String("css") || suffix == QLatin1String("js")
                              || suffix == QLatin1String("mjs");
    const bool looksLikePath = hasWebSuffix || ref.contains(QLatin1Char('/')) || ref.contains(QLatin1Char('\\'))
                               || ref.startsWith(QLatin1Char('.'));
    if (!looksLikePath)
        return {};
    QStringList tries;
    tries << ref;
    if (QFileInfo(ref).suffix().isEmpty()) {
        tries << ref + QStringLiteral(".html") << ref + QStringLiteral(".htm") << ref + QStringLiteral(".js")
              << ref + QStringLiteral(".mjs") << ref + QStringLiteral(".css") << ref + QStringLiteral("/index.html")
              << ref + QStringLiteral("/index.js");
    }
    for (const QString &trySpec : tries) {
        const QString resolved = resolveRelative(fromFile, trySpec, rootDir);
        if (!resolved.isEmpty())
            return resolved;
    }

    const QString root = QDir(rootDir).absolutePath();
    const QString fromDir = QFileInfo(fromFile).absolutePath();
    const QStringList bases = QStringList() << root << fromDir;
    const QStringList extraDirs = webAssetSubdirs();
    for (const QString &trySpec : tries) {
        for (const QString &base : bases) {
            for (const QString &sub : extraDirs) {
                const QString cand = QFileInfo(QDir(base).filePath(sub + QLatin1Char('/') + trySpec)).absoluteFilePath();
                if (QFileInfo::exists(cand) && !shouldSkipPath(cand))
                    return cand;
            }
        }
    }

    const QString nameOrRel = ref.contains(QLatin1Char('/')) ? ref : QFileInfo(ref).fileName();
    for (const QString &sub : extraDirs) {
        const QString hit = findNamedUnder(QDir(root).filePath(sub), nameOrRel, 0);
        if (!hit.isEmpty())
            return hit;
    }
    return {};
}

bool isWebLanguage(SourceLanguage language)
{
    return language == SourceLanguage::Html || language == SourceLanguage::Css
           || language == SourceLanguage::JavaScript;
}

bool isWebBackendLanguage(SourceLanguage language)
{
    return language == SourceLanguage::Python || language == SourceLanguage::Php || language == SourceLanguage::Go
           || language == SourceLanguage::JavaScript;
}

SourceLanguage languageOf(const QString &path)
{
    if (isPythonFile(path))
        return SourceLanguage::Python;
    if (isCppFile(path))
        return SourceLanguage::Cpp;
    if (isJavaFile(path))
        return SourceLanguage::Java;
    if (isGoFile(path))
        return SourceLanguage::Go;
    if (isRustFile(path))
        return SourceLanguage::Rust;
    if (isRFile(path))
        return SourceLanguage::R;
    if (isPhpFile(path))
        return SourceLanguage::Php;
    if (isHtmlFile(path))
        return SourceLanguage::Html;
    if (isCssFile(path))
        return SourceLanguage::Css;
    if (isJsFile(path))
        return SourceLanguage::JavaScript;
    return SourceLanguage::Unknown;
}

QString languageKey(SourceLanguage language)
{
    switch (language) {
    case SourceLanguage::Python:
        return QStringLiteral("python");
    case SourceLanguage::Cpp:
        return QStringLiteral("cpp");
    case SourceLanguage::Java:
        return QStringLiteral("java");
    case SourceLanguage::Go:
        return QStringLiteral("go");
    case SourceLanguage::Rust:
        return QStringLiteral("rust");
    case SourceLanguage::R:
        return QStringLiteral("r");
    case SourceLanguage::Php:
        return QStringLiteral("php");
    case SourceLanguage::Html:
        return QStringLiteral("html");
    case SourceLanguage::Css:
        return QStringLiteral("css");
    case SourceLanguage::JavaScript:
        return QStringLiteral("js");
    default:
        return QStringLiteral("unknown");
    }
}

SourceLanguage languageFromKey(const QString &key)
{
    if (key == QLatin1String("python"))
        return SourceLanguage::Python;
    if (key == QLatin1String("cpp"))
        return SourceLanguage::Cpp;
    if (key == QLatin1String("java"))
        return SourceLanguage::Java;
    if (key == QLatin1String("go"))
        return SourceLanguage::Go;
    if (key == QLatin1String("rust"))
        return SourceLanguage::Rust;
    if (key == QLatin1String("r"))
        return SourceLanguage::R;
    if (key == QLatin1String("php"))
        return SourceLanguage::Php;
    if (key == QLatin1String("html"))
        return SourceLanguage::Html;
    if (key == QLatin1String("css"))
        return SourceLanguage::Css;
    if (key == QLatin1String("js") || key == QLatin1String("javascript"))
        return SourceLanguage::JavaScript;
    return SourceLanguage::Unknown;
}

QStringList scanFiles(const QString &rootDir)
{
    const UiConfig &cfg = UiConfig::get();
    QStringList globs = cfg.scanGlobs;
    if (globs.isEmpty()) {
        globs << QStringLiteral("*.py") << QStringLiteral("*.c") << QStringLiteral("*.cc") << QStringLiteral("*.cpp")
              << QStringLiteral("*.cxx") << QStringLiteral("*.h") << QStringLiteral("*.hh") << QStringLiteral("*.hpp")
              << QStringLiteral("*.hxx") << QStringLiteral("*.java") << QStringLiteral("*.go")
              << QStringLiteral("*.rs") << QStringLiteral("*.R") << QStringLiteral("*.r")
              << QStringLiteral("*.Rmd") << QStringLiteral("*.rmd");
        globs << QStringLiteral("*.php") << QStringLiteral("*.phtml") << QStringLiteral("*.html")
              << QStringLiteral("*.htm") << QStringLiteral("*.css") << QStringLiteral("*.js")
              << QStringLiteral("*.mjs");
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

    QSet<QString> filePaths;
    for (const FileNode &f : result.files)
        filePaths.insert(f.path);
    QSet<QString> ignoredNames;
    for (const QString &name : cfg.ignoredFileNames)
        ignoredNames.insert(name);
    QSet<QString> connected;
    for (const FileRelation &rel : result.relations) {
        if (!filePaths.contains(rel.fromPath) || !filePaths.contains(rel.toPath))
            continue;
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
    QMap<QString, FileNode> files;
    for (const FileNode &file : a.files)
        files.insert(file.path, file);
    for (const FileNode &file : b.files) {
        auto it = files.find(file.path);
        if (it == files.end() || it.value().symbols.size() < file.symbols.size())
            files.insert(file.path, file);
    }
    QMap<QPair<QString, QString>, FileRelation> rels;
    auto addRel = [&](const FileRelation &rel) {
        if (rel.fromPath.isEmpty() || rel.toPath.isEmpty() || rel.fromPath == rel.toPath)
            return;
        const auto key = qMakePair(rel.fromPath, rel.toPath);
        auto it = rels.find(key);
        if (it == rels.end()) {
            rels.insert(key, rel);
            return;
        }
        QVector<DefinedSymbol> &used = it.value().used;
        for (const DefinedSymbol &sym : rel.used) {
            bool found = false;
            for (DefinedSymbol &u : used) {
                if (u.qualifiedName == sym.qualifiedName && u.kind == sym.kind) {
                    for (int line : sym.useLines) {
                        if (!u.useLines.contains(line))
                            u.useLines.push_back(line);
                    }
                    found = true;
                    break;
                }
            }
            if (!found)
                used.push_back(sym);
        }
    };
    for (const FileRelation &rel : a.relations)
        addRel(rel);
    for (const FileRelation &rel : b.relations)
        addRel(rel);

    AnalysisResult out;
    for (auto it = files.begin(); it != files.end(); ++it)
        out.files.push_back(it.value());
    for (auto it = rels.begin(); it != rels.end(); ++it)
        out.relations.push_back(it.value());
    std::sort(out.files.begin(), out.files.end(), [](const FileNode &x, const FileNode &y) {
        return x.fileName < y.fileName;
    });
    std::sort(out.relations.begin(), out.relations.end(), [](const FileRelation &x, const FileRelation &y) {
        if (x.fromFileName != y.fromFileName)
            return x.fromFileName < y.fromFileName;
        return x.toFileName < y.toFileName;
    });
    return out;
}

} // namespace AnalysisUtil
