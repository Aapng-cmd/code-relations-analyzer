#include "RAnalyzer.h"
#include "AnalysisUtil.h"

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
    QStringList sources;
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

QString stripRComments(const QString &src)
{
    QString out;
    out.reserve(src.size());
    bool inStr = false;
    QChar quote;
    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src[i];
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
        if (c == '#') {
            while (i < src.size() && src[i] != '\n')
                ++i;
            if (i < src.size())
                out += src[i];
            continue;
        }
        out += c;
    }
    return out;
}

bool nameOccurs(const QString &src, const QString &name)
{
    QRegularExpression re(QStringLiteral("\\b") + QRegularExpression::escape(name) + QStringLiteral("\\b"));
    return re.match(src).hasMatch();
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

ParsedFile parseRFile(const QString &path)
{
    ParsedFile parsed;
    parsed.node.path = QFileInfo(path).absoluteFilePath();
    parsed.node.fileName = QFileInfo(path).fileName();
    parsed.node.moduleName = QFileInfo(path).completeBaseName();
    parsed.node.language = SourceLanguage::R;
    const QString src = stripRComments(readFile(path));

    static const QRegularExpression fnRe(
        QStringLiteral("(`[^`]+`|[A-Za-z.][A-Za-z0-9._]*)\\s*(?:<-|=)\\s*function\\s*\\("));
    auto it = fnRe.globalMatch(src);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        DefinedSymbol sym;
        sym.name = m.captured(1);
        if (sym.name.startsWith(QLatin1Char('`')))
            sym.name = sym.name.mid(1, sym.name.size() - 2);
        if (sym.name.isEmpty())
            continue;
        sym.qualifiedName = sym.name;
        sym.kind = SymbolKind::Function;
        sym.display = QStringLiteral("function ") + sym.name + QStringLiteral("()");
        sym.line = lineAt(src, m.capturedStart());
        addSymbol(parsed.node, sym);
    }

    static const QRegularExpression classRe(
        QStringLiteral("setClass\\s*\\(\\s*['\"]([^'\"]+)['\"]"));
    it = classRe.globalMatch(src);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        DefinedSymbol sym;
        sym.name = m.captured(1);
        sym.qualifiedName = sym.name;
        sym.kind = SymbolKind::Class;
        sym.display = QStringLiteral("class ") + sym.name;
        sym.line = lineAt(src, m.capturedStart());
        addSymbol(parsed.node, sym);
    }

    static const QRegularExpression srcRe(
        QStringLiteral("(?:sys\\.)?source\\s*\\(\\s*['\"]([^'\"]+)['\"]"));
    it = srcRe.globalMatch(src);
    while (it.hasNext()) {
        const QString spec = AnalysisUtil::cleanRef(it.next().captured(1));
        if (!spec.isEmpty() && !parsed.sources.contains(spec))
            parsed.sources << spec;
    }
    return parsed;
}

} // namespace

AnalysisResult RAnalyzer::analyzeDirectory(const QString &rootDir)
{
    AnalysisResult result;
    QVector<ParsedFile> parsed;
    for (const QString &path : AnalysisUtil::scanFiles(rootDir)) {
        if (!AnalysisUtil::isRFile(path))
            continue;
        parsed.push_back(parseRFile(path));
    }

    QMap<QString, FileNode *> byPath;
    QMap<QString, FileNode *> byBase;
    for (ParsedFile &p : parsed) {
        byPath.insert(p.node.path, &p.node);
        byBase.insert(p.node.fileName.toLower(), &p.node);
        byBase.insert(p.node.moduleName.toLower(), &p.node);
    }

    QMap<QPair<QString, QString>, QVector<DefinedSymbol>> edges;
    for (const ParsedFile &consumer : parsed) {
        const QString src = stripRComments(readFile(consumer.node.path));
        for (const QString &spec : consumer.sources) {
            QString resolved = AnalysisUtil::resolveRelative(consumer.node.path, spec, rootDir);
            if (resolved.isEmpty()) {
                const QString name = QFileInfo(spec).fileName().toLower();
                if (FileNode *hit = byBase.value(name))
                    resolved = hit->path;
            }
            if (resolved.isEmpty() || !byPath.contains(resolved))
                continue;
            QVector<DefinedSymbol> used;
            FileNode *provider = byPath.value(resolved);
            for (const DefinedSymbol &s : provider->symbols) {
                if (s.parentQualified.isEmpty() && nameOccurs(src, s.name))
                    addUsed(used, s, 0);
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
