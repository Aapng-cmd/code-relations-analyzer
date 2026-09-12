#include "PhpAnalyzer.h"
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
    QString ns;
    QMap<QString, QString> aliases;
    QStringList includes;
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

bool isIdentStart(QChar c)
{
    return c.isLetter() || c == '_';
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

QString readIdent(const QString &s, int &pos)
{
    while (pos < s.size() && s[pos].isSpace())
        ++pos;
    if (pos >= s.size() || (!s[pos].isLetter() && s[pos] != '_'))
        return {};
    const int start = pos++;
    while (pos < s.size() && (s[pos].isLetterOrNumber() || s[pos] == '_' || s[pos] == '\\'))
        ++pos;
    return s.mid(start, pos - start);
}

bool isPhpKeyword(const QString &n)
{
    static const QSet<QString> k = {
        QStringLiteral("if"),        QStringLiteral("else"),      QStringLiteral("elseif"),
        QStringLiteral("for"),       QStringLiteral("foreach"),   QStringLiteral("while"),
        QStringLiteral("do"),        QStringLiteral("switch"),    QStringLiteral("case"),
        QStringLiteral("function"),  QStringLiteral("class"),     QStringLiteral("interface"),
        QStringLiteral("trait"),     QStringLiteral("namespace"), QStringLiteral("use"),
        QStringLiteral("return"),    QStringLiteral("new"),       QStringLiteral("echo"),
        QStringLiteral("print"),     QStringLiteral("public"),    QStringLiteral("private"),
        QStringLiteral("protected"), QStringLiteral("static"),    QStringLiteral("abstract"),
        QStringLiteral("final"),     QStringLiteral("readonly"),  QStringLiteral("const"),
        QStringLiteral("var"),       QStringLiteral("extends"),   QStringLiteral("implements"),
        QStringLiteral("require"),   QStringLiteral("include"),   QStringLiteral("require_once"),
        QStringLiteral("include_once"), QStringLiteral("true"),   QStringLiteral("false"),
        QStringLiteral("null"),      QStringLiteral("this"),      QStringLiteral("self"),
        QStringLiteral("parent"),    QStringLiteral("as"),        QStringLiteral("try"),
        QStringLiteral("catch"),     QStringLiteral("finally"),   QStringLiteral("throw"),
        QStringLiteral("match"),     QStringLiteral("enum"),      QStringLiteral("fn")};
    return k.contains(n);
}

QString lastSegment(const QString &fqcn)
{
    const int slash = fqcn.lastIndexOf(QLatin1Char('\\'));
    return slash >= 0 ? fqcn.mid(slash + 1) : fqcn;
}

void skipString(const QString &src, int &i, QChar quote)
{
    ++i;
    while (i < src.size()) {
        if (src[i] == '\\') {
            i += 2;
            continue;
        }
        if (src[i] == quote)
            break;
        ++i;
    }
}

ParsedFile parsePhpFile(const QString &path)
{
    ParsedFile parsed;
    parsed.node.path = QFileInfo(path).absoluteFilePath();
    parsed.node.fileName = QFileInfo(path).fileName();
    parsed.node.moduleName = QFileInfo(path).completeBaseName();
    parsed.node.language = SourceLanguage::Php;

    const QString src = readFile(path);
    const bool hasTag = src.contains(QLatin1String("<?"));
    bool inPhp = !hasTag;
    QString parent;
    int braceDepth = 0;
    int classDepth = -1;

    auto skipWs = [&](int &i) {
        while (i < src.size() && src[i].isSpace())
            ++i;
    };

    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src[i];
        const QChar next = (i + 1 < src.size()) ? src[i + 1] : QChar();
        if (!inPhp) {
            if (c == '<' && next == '?') {
                inPhp = true;
                i += 1;
                if (i + 1 < src.size() && src.mid(i + 1, 3).compare(QLatin1String("php"), Qt::CaseInsensitive) == 0)
                    i += 3;
            }
            continue;
        }
        if (c == '?' && next == '>') {
            inPhp = false;
            ++i;
            continue;
        }
        if (c == '/' && next == '/') {
            while (i < src.size() && src[i] != '\n')
                ++i;
            continue;
        }
        if (c == '#') {
            while (i < src.size() && src[i] != '\n')
                ++i;
            continue;
        }
        if (c == '/' && next == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/'))
                ++i;
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            skipString(src, i, c);
            continue;
        }
        if (c == '{') {
            ++braceDepth;
            continue;
        }
        if (c == '}') {
            --braceDepth;
            if (classDepth >= 0 && braceDepth < classDepth) {
                parent.clear();
                classDepth = -1;
            }
            continue;
        }
        if (!isIdentStart(c) || c == '\\')
            continue;

        int pos = i;
        const QString word = readIdent(src, pos);
        if (word.isEmpty())
            continue;

        if (word == QLatin1String("namespace")) {
            parsed.ns = readIdent(src, pos);
            i = pos - 1;
            continue;
        }
        if (word == QLatin1String("use")) {
            skipWs(pos);
            if (src.mid(pos, 8) == QLatin1String("function") || src.mid(pos, 5) == QLatin1String("const")) {
                i = pos;
                continue;
            }
            const QString fqcn = readIdent(src, pos);
            skipWs(pos);
            if (pos < src.size() && src[pos] == '{') {
                i = pos;
                continue;
            }
            QString alias = lastSegment(fqcn);
            if (pos + 1 < src.size() && src.mid(pos, 2) == QLatin1String("as")) {
                pos += 2;
                alias = readIdent(src, pos);
            }
            if (!fqcn.isEmpty())
                parsed.aliases.insert(alias, fqcn.startsWith(QLatin1Char('\\')) ? fqcn.mid(1) : fqcn);
            i = pos - 1;
            continue;
        }
        if (word == QLatin1String("require") || word == QLatin1String("include")
            || word == QLatin1String("require_once") || word == QLatin1String("include_once")) {
            skipWs(pos);
            if (pos < src.size() && src[pos] == '(')
                ++pos;
            skipWs(pos);
            if (src.mid(pos, 6) == QLatin1String("__DIR__")) {
                pos += 6;
                skipWs(pos);
                if (pos < src.size() && src[pos] == '.')
                    ++pos;
                skipWs(pos);
            }
            if (pos < src.size() && (src[pos] == '"' || src[pos] == '\'')) {
                const QChar q = src[pos];
                ++pos;
                const int start = pos;
                while (pos < src.size() && src[pos] != q)
                    ++pos;
                parsed.includes << src.mid(start, pos - start);
            }
            i = pos;
            continue;
        }
        if (word == QLatin1String("class") || word == QLatin1String("interface") || word == QLatin1String("trait")
            || word == QLatin1String("enum")) {
            const QString name = readIdent(src, pos);
            DefinedSymbol sym;
            sym.name = name;
            sym.qualifiedName = parsed.ns.isEmpty() ? name : (parsed.ns + QLatin1Char('\\') + name);
            sym.kind = SymbolKind::Class;
            sym.display = QStringLiteral("class ") + name;
            sym.line = lineAt(src, i);
            addSymbol(parsed.node, sym);
            parent = name;
            classDepth = braceDepth + 1;
            i = pos - 1;
            continue;
        }
        if (word == QLatin1String("function")) {
            const QString name = readIdent(src, pos);
            if (name.isEmpty() || isPhpKeyword(name)) {
                i = pos - 1;
                continue;
            }
            DefinedSymbol sym;
            sym.name = name;
            sym.parentQualified = parent;
            sym.qualifiedName = parent.isEmpty() ? name : (parent + QLatin1Char('.') + name);
            sym.kind = SymbolKind::Function;
            sym.display = parent.isEmpty() ? (QStringLiteral("function ") + name + QStringLiteral("()"))
                                           : (parent + QStringLiteral(".") + name + QStringLiteral("()"));
            sym.line = lineAt(src, i);
            addSymbol(parsed.node, sym);
            i = pos - 1;
            continue;
        }
        i = pos - 1;
    }

    static const QRegularExpression incRe(
        QStringLiteral("\\b(?:require|include)(?:_once)?\\s*\\(?\\s*(?:__DIR__\\s*\\.\\s*)?['\"]([^'\"]+)['\"]"));
    auto iit = incRe.globalMatch(src);
    while (iit.hasNext()) {
        const QString spec = iit.next().captured(1);
        if (!spec.isEmpty() && !parsed.includes.contains(spec))
            parsed.includes << spec;
    }

    return parsed;
}

bool nameOccurs(const QString &src, const QString &name)
{
    if (name.isEmpty())
        return false;
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

} // namespace

AnalysisResult PhpAnalyzer::analyzeDirectory(const QString &rootDir)
{
    AnalysisResult result;
    QVector<ParsedFile> parsed;
    for (const QString &path : AnalysisUtil::scanFiles(rootDir)) {
        if (!AnalysisUtil::isPhpFile(path) || AnalysisUtil::isMinifiedAsset(path))
            continue;
        parsed.push_back(parsePhpFile(path));
    }

    QMap<QString, FileNode *> byPath;
    QMap<QString, FileNode *> byFqcn;
    QMap<QString, FileNode *> byClass;
    for (ParsedFile &p : parsed) {
        byPath.insert(p.node.path, &p.node);
        for (const DefinedSymbol &s : p.node.symbols) {
            if (s.kind != SymbolKind::Class || !s.parentQualified.isEmpty())
                continue;
            byFqcn.insert(s.qualifiedName, &p.node);
            byClass.insert(s.name, &p.node);
        }
    }

    QMap<QPair<QString, QString>, QVector<DefinedSymbol>> edges;
    for (const ParsedFile &consumer : parsed) {
        const QString src = readFile(consumer.node.path);
        for (const QString &spec : consumer.includes) {
            const QString resolved = AnalysisUtil::resolveRelative(consumer.node.path, spec, rootDir);
            if (resolved.isEmpty())
                continue;
            QVector<DefinedSymbol> used;
            if (FileNode *provider = byPath.value(resolved)) {
                for (const DefinedSymbol &s : provider->symbols) {
                    if (s.parentQualified.isEmpty() && nameOccurs(src, s.name))
                        addUsed(used, s, 0);
                }
            }
            addEdge(edges, resolved, consumer.node.path, used);
        }
        for (auto it = consumer.aliases.begin(); it != consumer.aliases.end(); ++it) {
            FileNode *provider = byFqcn.value(it.value(), nullptr);
            if (!provider)
                provider = byClass.value(lastSegment(it.value()), nullptr);
            if (!provider || provider->path == consumer.node.path)
                continue;
            QVector<DefinedSymbol> used;
            for (const DefinedSymbol &s : provider->symbols) {
                if (s.kind == SymbolKind::Class && (s.qualifiedName == it.value() || s.name == it.key()))
                    addUsed(used, s, 0);
            }
            addEdge(edges, provider->path, consumer.node.path, used);
        }
        QRegularExpression word(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*\\b"));
        auto mit = word.globalMatch(src);
        QSet<QString> seen;
        while (mit.hasNext()) {
            const QString name = mit.next().captured();
            if (seen.contains(name) || isPhpKeyword(name.toLower()))
                continue;
            seen.insert(name);
            FileNode *provider = nullptr;
            if (consumer.aliases.contains(name))
                provider = byFqcn.value(consumer.aliases.value(name));
            if (!provider)
                provider = byClass.value(name);
            if (!provider || provider->path == consumer.node.path)
                continue;
            QVector<DefinedSymbol> used;
            for (const DefinedSymbol &sym : provider->symbols) {
                if (sym.kind == SymbolKind::Class && sym.name == name)
                    addUsed(used, sym, 0);
            }
            addEdge(edges, provider->path, consumer.node.path, used);
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
