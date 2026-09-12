#include "WebAnalyzer.h"
#include "AnalysisUtil.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace {

struct ParsedFile {
    FileNode node;
    QStringList refs;
};

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    return in.readAll();
}

void addSymbol(FileNode &node, const DefinedSymbol &sym)
{
    if (sym.name.isEmpty())
        return;
    for (const DefinedSymbol &existing : node.symbols) {
        if (existing.kind == sym.kind && existing.qualifiedName == sym.qualifiedName)
            return;
    }
    node.symbols.push_back(sym);
}

void addUsed(QVector<DefinedSymbol> &used, const DefinedSymbol &sym, int useLine)
{
    for (DefinedSymbol &u : used) {
        if (u.qualifiedName == sym.qualifiedName && u.kind == sym.kind) {
            if (useLine > 0 && !u.useLines.contains(useLine))
                u.useLines.push_back(useLine);
            return;
        }
    }
    DefinedSymbol copy = sym;
    copy.useLines.clear();
    if (useLine > 0)
        copy.useLines.push_back(useLine);
    used.push_back(copy);
}

int lineAt(const QString &src, int index)
{
    return src.left(index).count(QLatin1Char('\n')) + 1;
}

QString stripJsComments(const QString &src)
{
    QString out;
    out.reserve(src.size());
    bool inLine = false;
    bool inBlock = false;
    bool inStr = false;
    QChar quote;
    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src[i];
        const QChar next = (i + 1 < src.size()) ? src[i + 1] : QChar();
        if (inLine) {
            if (c == '\n') {
                inLine = false;
                out += c;
            }
            continue;
        }
        if (inBlock) {
            if (c == '*' && next == '/') {
                inBlock = false;
                ++i;
            } else if (c == '\n')
                out += c;
            continue;
        }
        if (inStr) {
            out += c;
            if (c == '\\' && i + 1 < src.size()) {
                out += src[++i];
                continue;
            }
            if (c == quote)
                inStr = false;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            inStr = true;
            quote = c;
            out += c;
            continue;
        }
        if (c == '/' && next == '/') {
            inLine = true;
            ++i;
            continue;
        }
        if (c == '/' && next == '*') {
            inBlock = true;
            ++i;
            continue;
        }
        out += c;
    }
    return out;
}

void collectRefs(const QString &src, const QRegularExpression &re, int cap, QStringList *out)
{
    auto it = re.globalMatch(src);
    while (it.hasNext()) {
        const QString spec = AnalysisUtil::cleanRef(it.next().captured(cap));
        if (!spec.isEmpty() && !AnalysisUtil::isExternalRef(spec) && !out->contains(spec))
            *out << spec;
    }
}

void parseHtml(ParsedFile &parsed, const QString &src)
{
    static const QRegularExpression scriptSrc(
        QStringLiteral("<script\\b[^>]*\\bsrc\\s*=\\s*['\"]([^'\"]+)['\"]"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression linkHref(
        QStringLiteral("<link\\b[^>]*\\bhref\\s*=\\s*['\"]([^'\"]+)['\"]"),
        QRegularExpression::CaseInsensitiveOption);
    collectRefs(src, scriptSrc, 1, &parsed.refs);
    collectRefs(src, linkHref, 1, &parsed.refs);

    static const QRegularExpression aHref(QStringLiteral("<a\\b[^>]*\\bhref\\s*=\\s*['\"]([^'\"]+)['\"]"),
                                          QRegularExpression::CaseInsensitiveOption);
    QStringList pageRefs;
    collectRefs(src, aHref, 1, &pageRefs);
    for (const QString &spec : pageRefs) {
        const QString s = spec.toLower();
        if ((s.endsWith(QLatin1String(".html")) || s.endsWith(QLatin1String(".htm"))) && !parsed.refs.contains(spec))
            parsed.refs << spec;
    }

    static const QRegularExpression idRe(QStringLiteral("\\bid\\s*=\\s*['\"]([^'\"]+)['\"]"),
                                         QRegularExpression::CaseInsensitiveOption);
    auto it = idRe.globalMatch(src);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        DefinedSymbol sym;
        sym.name = m.captured(1);
        if (sym.name.isEmpty())
            continue;
        sym.qualifiedName = QStringLiteral("#") + sym.name;
        sym.kind = SymbolKind::Variable;
        sym.display = sym.qualifiedName;
        sym.line = lineAt(src, m.capturedStart());
        addSymbol(parsed.node, sym);
    }
}

void parseCss(ParsedFile &parsed, const QString &src)
{
    static const QRegularExpression importRe(
        QStringLiteral("@import\\s+(?:url\\(\\s*)?['\"]?([^'\")\\s]+)['\"]?"),
        QRegularExpression::CaseInsensitiveOption);
    collectRefs(src, importRe, 1, &parsed.refs);
}

void parseJs(ParsedFile &parsed, const QString &src)
{
    const QString code = stripJsComments(src);
    static const QRegularExpression fromRe(
        QStringLiteral("(?:import|export)\\s+[^;]*?\\sfrom\\s+['\"]([^'\"]+)['\"]"));
    static const QRegularExpression sideRe(QStringLiteral("import\\s+['\"]([^'\"]+)['\"]"));
    static const QRegularExpression reqRe(QStringLiteral("(?:require|import)\\s*\\(\\s*['\"]([^'\"]+)['\"]\\s*\\)"));
    collectRefs(code, fromRe, 1, &parsed.refs);
    collectRefs(code, sideRe, 1, &parsed.refs);
    collectRefs(code, reqRe, 1, &parsed.refs);

    QRegularExpression fnRe(QStringLiteral("(?:export\\s+)?(?:async\\s+)?function\\s+([A-Za-z_$][\\w$]*)"));
    auto it = fnRe.globalMatch(code);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        DefinedSymbol sym;
        sym.name = m.captured(1);
        sym.qualifiedName = sym.name;
        sym.kind = SymbolKind::Function;
        sym.display = QStringLiteral("function ") + sym.name + QStringLiteral("()");
        sym.line = lineAt(code, m.capturedStart());
        addSymbol(parsed.node, sym);
    }

    QRegularExpression classRe(QStringLiteral("(?:export\\s+)?class\\s+([A-Za-z_$][\\w$]*)"));
    it = classRe.globalMatch(code);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        DefinedSymbol sym;
        sym.name = m.captured(1);
        sym.qualifiedName = sym.name;
        sym.kind = SymbolKind::Class;
        sym.display = QStringLiteral("class ") + sym.name;
        sym.line = lineAt(code, m.capturedStart());
        addSymbol(parsed.node, sym);
    }

    QRegularExpression constFn(
        QStringLiteral("(?:export\\s+)?(?:const|let|var)\\s+([A-Za-z_$][\\w$]*)\\s*=\\s*(?:async\\s*)?(?:function\\b|\\()"));
    it = constFn.globalMatch(code);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        DefinedSymbol sym;
        sym.name = m.captured(1);
        sym.qualifiedName = sym.name;
        sym.kind = SymbolKind::Function;
        sym.display = QStringLiteral("function ") + sym.name + QStringLiteral("()");
        sym.line = lineAt(code, m.capturedStart());
        addSymbol(parsed.node, sym);
    }
}

QString resolveWebRef(const QString &fromFile, const QString &spec, const QString &rootDir)
{
    return AnalysisUtil::resolveWebAsset(fromFile, spec, rootDir);
}

void addEdge(QMap<QPair<QString, QString>, QVector<DefinedSymbol>> &edges, const QString &from, const QString &to,
             const QVector<DefinedSymbol> &used)
{
    if (from.isEmpty() || to.isEmpty() || from == to)
        return;
    const auto key = qMakePair(from, to);
    if (!edges.contains(key)) {
        edges.insert(key, used);
        return;
    }
    QVector<DefinedSymbol> &dst = edges[key];
    for (const DefinedSymbol &sym : used)
        addUsed(dst, sym, sym.useLines.isEmpty() ? 0 : sym.useLines.first());
}

ParsedFile parseWebFile(const QString &path)
{
    ParsedFile parsed;
    parsed.node.path = QFileInfo(path).absoluteFilePath();
    parsed.node.fileName = QFileInfo(path).fileName();
    parsed.node.moduleName = QFileInfo(path).completeBaseName();
    parsed.node.language = AnalysisUtil::languageOf(path);
    const QString src = readFile(path);
    if (AnalysisUtil::isHtmlFile(path))
        parseHtml(parsed, src);
    else if (AnalysisUtil::isCssFile(path))
        parseCss(parsed, src);
    else if (AnalysisUtil::isJsFile(path))
        parseJs(parsed, src);
    return parsed;
}

void collectPhpWebRefs(const QString &path, const QString &rootDir,
                       QMap<QPair<QString, QString>, QVector<DefinedSymbol>> *edges)
{
    const QString src = readFile(path);
    ParsedFile tmp;
    parseHtml(tmp, src);
    for (const QString &spec : tmp.refs) {
        const QString resolved = resolveWebRef(path, spec, rootDir);
        if (!resolved.isEmpty() && AnalysisUtil::isWebFile(resolved))
            addEdge(*edges, resolved, QFileInfo(path).absoluteFilePath(), {});
    }
}

QStringList extractQuotedStrings(const QString &src)
{
    QStringList out;
    QChar quote;
    bool inStr = false;
    QString cur;
    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src[i];
        if (!inStr) {
            if (c == '"' || c == '\'' || c == '`') {
                inStr = true;
                quote = c;
                cur.clear();
            }
            continue;
        }
        if (c == '\\' && i + 1 < src.size()) {
            cur += src[++i];
            continue;
        }
        if (c == quote) {
            if (!cur.isEmpty() && !out.contains(cur))
                out << cur;
            inStr = false;
            continue;
        }
        if (c == '\n' && quote != '`') {
            inStr = false;
            continue;
        }
        cur += c;
    }
    return out;
}

bool looksLikeWebAsset(const QString &spec)
{
    const QString ref = AnalysisUtil::cleanRef(spec);
    if (ref.isEmpty() || AnalysisUtil::isExternalRef(ref))
        return false;
    const QString suffix = QFileInfo(ref).suffix().toLower();
    return suffix == QLatin1String("html") || suffix == QLatin1String("htm") || suffix == QLatin1String("css")
           || suffix == QLatin1String("js") || suffix == QLatin1String("mjs");
}

bool looksLikeStaticDir(const QString &spec)
{
    const QString name = QFileInfo(AnalysisUtil::cleanRef(spec)).fileName().toLower();
    const QStringList names = QStringList() << QStringLiteral("static") << QStringLiteral("public")
                                           << QStringLiteral("www") << QStringLiteral("templates")
                                           << QStringLiteral("views") << QStringLiteral("html")
                                           << QStringLiteral("assets") << QStringLiteral("frontend")
                                           << QStringLiteral("client") << QStringLiteral("dist")
                                           << QStringLiteral("web") << QStringLiteral("tmpl")
                                           << QStringLiteral("resources");
    return names.contains(name);
}

bool isJsServer(const QString &src)
{
    return src.contains(QLatin1String("express")) || src.contains(QLatin1String("createServer"))
           || src.contains(QLatin1String("sendFile")) || src.contains(QLatin1String("fastify"))
           || src.contains(QLatin1String("koa")) || src.contains(QLatin1String("express.static"))
           || src.contains(QLatin1String("ListenAndServe"))
           || (src.contains(QLatin1String("listen("))
               && (src.contains(QLatin1String("http")) || src.contains(QLatin1String("https"))));
}

bool isGoServer(const QString &src)
{
    return src.contains(QLatin1String("net/http")) || src.contains(QLatin1String("html/template"))
           || src.contains(QLatin1String("ListenAndServe")) || src.contains(QLatin1String("http.Dir"))
           || src.contains(QLatin1String("http.FileServer")) || src.contains(QLatin1String("ParseFiles"))
           || src.contains(QLatin1String("ParseGlob")) || src.contains(QLatin1String("gin."))
           || src.contains(QLatin1String("echo.")) || src.contains(QLatin1String("fiber."));
}

void linkDirAssets(const QString &dirPath, const QString &backend,
                   QMap<QPair<QString, QString>, QVector<DefinedSymbol>> *edges)
{
    if (dirPath.isEmpty() || !QFileInfo(dirPath).isDir() || AnalysisUtil::shouldSkipPath(dirPath))
        return;
    QDirIterator it(dirPath,
                    QStringList() << QStringLiteral("*.html") << QStringLiteral("*.htm") << QStringLiteral("*.css")
                                  << QStringLiteral("*.js") << QStringLiteral("*.mjs"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = QFileInfo(it.next()).absoluteFilePath();
        if (AnalysisUtil::shouldSkipPath(path) || AnalysisUtil::isMinifiedAsset(path)
            || !AnalysisUtil::isWebFile(path))
            continue;
        addEdge(*edges, path, backend, {});
    }
}

void linkSpec(const QString &fromFile, const QString &spec, const QString &rootDir, const QString &backend,
              QMap<QPair<QString, QString>, QVector<DefinedSymbol>> *edges)
{
    const QString ref = AnalysisUtil::cleanRef(spec);
    if (ref.isEmpty() || AnalysisUtil::isExternalRef(ref))
        return;
    if (ref.contains(QLatin1Char('*')) || ref.contains(QLatin1Char('?'))) {
        const QString glob = QFileInfo(ref).fileName();
        QString dirPart = QFileInfo(ref).path();
        QStringList bases;
        bases << QFileInfo(fromFile).absolutePath() << QDir(rootDir).absolutePath();
        const QStringList extra = QStringList() << QStringLiteral("templates") << QStringLiteral("views")
                                                << QStringLiteral("static") << QStringLiteral("public");
        for (const QString &sub : extra)
            bases << QDir(rootDir).filePath(sub);
        if (dirPart == QLatin1String("."))
            dirPart.clear();
        for (const QString &base : bases) {
            const QString dir = dirPart.isEmpty() ? base : QDir(base).filePath(dirPart);
            if (!QFileInfo(dir).isDir())
                continue;
            const QStringList files = QDir(dir).entryList(QStringList() << glob, QDir::Files);
            for (const QString &name : files)
                linkSpec(fromFile, QDir(dir).filePath(name), rootDir, backend, edges);
        }
        return;
    }

    const QString fromDir = QFileInfo(fromFile).absolutePath();
    QStringList dirCandidates;
    dirCandidates << QFileInfo(QDir(fromDir).filePath(ref)).absoluteFilePath();
    dirCandidates << QFileInfo(QDir(rootDir).filePath(ref)).absoluteFilePath();
    if (looksLikeStaticDir(ref)) {
        for (const QString &cand : dirCandidates) {
            if (QFileInfo(cand).isDir())
                linkDirAssets(cand, backend, edges);
        }
    }

    if (!looksLikeWebAsset(ref))
        return;
    const QString resolved = resolveWebRef(fromFile, ref, rootDir);
    if (!resolved.isEmpty() && AnalysisUtil::isWebFile(resolved))
        addEdge(*edges, resolved, backend, {});
}

AnalysisResult analyzeBackends(const QString &rootDir)
{
    AnalysisResult result;
    QMap<QPair<QString, QString>, QVector<DefinedSymbol>> edges;
    for (const QString &path : AnalysisUtil::scanFiles(rootDir)) {
        if (AnalysisUtil::isMinifiedAsset(path))
            continue;
        const SourceLanguage lang = AnalysisUtil::languageOf(path);
        if (!AnalysisUtil::isWebBackendLanguage(lang) || AnalysisUtil::isHtmlFile(path)
            || AnalysisUtil::isCssFile(path))
            continue;
        const QString src = readFile(path);
        if (src.isEmpty())
            continue;
        if (lang == SourceLanguage::JavaScript && !isJsServer(src))
            continue;
        if (lang == SourceLanguage::Go && !isGoServer(src))
            continue;

        const QString backend = QFileInfo(path).absoluteFilePath();
        if (lang == SourceLanguage::Php)
            collectPhpWebRefs(path, rootDir, &edges);

        for (const QString &spec : extractQuotedStrings(src))
            linkSpec(path, spec, rootDir, backend, &edges);
    }

    for (auto it = edges.begin(); it != edges.end(); ++it) {
        FileRelation rel;
        rel.fromPath = it.key().first;
        rel.toPath = it.key().second;
        rel.fromFileName = QFileInfo(rel.fromPath).fileName();
        rel.toFileName = QFileInfo(rel.toPath).fileName();
        rel.used = it.value();
        result.relations.push_back(rel);
    }
    return result;
}

AnalysisResult analyzeKind(const QString &rootDir, SourceLanguage kind)
{
    AnalysisResult result;
    QVector<ParsedFile> parsed;
    for (const QString &path : AnalysisUtil::scanFiles(rootDir)) {
        if (AnalysisUtil::isMinifiedAsset(path) || AnalysisUtil::languageOf(path) != kind)
            continue;
        parsed.push_back(parseWebFile(path));
    }

    QMap<QString, FileNode *> byPath;
    for (ParsedFile &p : parsed)
        byPath.insert(p.node.path, &p.node);

    QMap<QPair<QString, QString>, QVector<DefinedSymbol>> edges;
    for (const ParsedFile &consumer : parsed) {
        const QString src = readFile(consumer.node.path);
        for (const QString &spec : consumer.refs) {
            const QString resolved = resolveWebRef(consumer.node.path, spec, rootDir);
            if (resolved.isEmpty() || resolved == consumer.node.path)
                continue;
            QVector<DefinedSymbol> used;
            if (FileNode *provider = byPath.value(resolved)) {
                for (const DefinedSymbol &s : provider->symbols) {
                    if (s.parentQualified.isEmpty()) {
                        QRegularExpression re(QStringLiteral("\\b") + QRegularExpression::escape(s.name)
                                              + QStringLiteral("\\b"));
                        if (re.match(src).hasMatch())
                            addUsed(used, s, 0);
                    }
                }
            }
            addEdge(edges, resolved, consumer.node.path, used);
        }
    }

    for (const ParsedFile &p : parsed)
        result.files.push_back(p.node);
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        FileRelation rel;
        rel.fromPath = it.key().first;
        rel.toPath = it.key().second;
        rel.fromFileName = QFileInfo(rel.fromPath).fileName();
        rel.toFileName = QFileInfo(rel.toPath).fileName();
        rel.used = it.value();
        for (DefinedSymbol &s : rel.used)
            std::sort(s.useLines.begin(), s.useLines.end());
        result.relations.push_back(rel);
    }
    return result;
}

} // namespace

AnalysisResult HtmlAnalyzer::analyzeDirectory(const QString &rootDir)
{
    return analyzeKind(rootDir, SourceLanguage::Html);
}

AnalysisResult CssAnalyzer::analyzeDirectory(const QString &rootDir)
{
    return analyzeKind(rootDir, SourceLanguage::Css);
}

AnalysisResult JsAnalyzer::analyzeDirectory(const QString &rootDir)
{
    return analyzeKind(rootDir, SourceLanguage::JavaScript);
}

AnalysisResult WebAnalyzer::analyzeDirectory(const QString &rootDir)
{
    return analyzeBackends(rootDir);
}

