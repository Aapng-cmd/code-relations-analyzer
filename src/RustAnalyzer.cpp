#include "RustAnalyzer.h"
#include "AnalysisUtil.h"

#include <QDir>
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
    QStringList mods;
    QStringList uses;
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

QString stripRustNoise(const QString &src)
{
    QString out;
    out.reserve(src.size());
    bool inLine = false;
    bool inBlock = false;
    bool inStr = false;
    bool inChar = false;
    int rawHashes = -1;
    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src[i];
        const QChar next = (i + 1 < src.size()) ? src[i + 1] : QChar();
        if (rawHashes >= 0) {
            out += c;
            if (c == '"' ) {
                int n = 0;
                while (i + 1 + n < src.size() && src[i + 1 + n] == '#')
                    ++n;
                if (n >= rawHashes) {
                    rawHashes = -1;
                    i += n;
                }
            }
            continue;
        }
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
        if (inStr || inChar) {
            out += c;
            if (c == '\\' && i + 1 < src.size()) {
                out += src[++i];
                continue;
            }
            if ((inStr && c == '"') || (inChar && c == '\'')) {
                inStr = false;
                inChar = false;
            }
            continue;
        }
        if (c == 'r' && (next == '"' || next == '#')) {
            int n = 0;
            int j = i + 1;
            while (j < src.size() && src[j] == '#') {
                ++n;
                ++j;
            }
            if (j < src.size() && src[j] == '"') {
                rawHashes = n;
                while (i <= j) {
                    out += src[i];
                    ++i;
                }
                --i;
                continue;
            }
        }
        if (c == '"') {
            inStr = true;
            out += c;
            continue;
        }
        if (c == '\'') {
            inChar = true;
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

void skipWs(const QString &s, int &i)
{
    while (i < s.size() && s[i].isSpace())
        ++i;
}

QString readIdent(const QString &s, int &i)
{
    skipWs(s, i);
    if (i >= s.size() || (!s[i].isLetter() && s[i] != '_'))
        return {};
    const int start = i++;
    while (i < s.size() && (s[i].isLetterOrNumber() || s[i] == '_'))
        ++i;
    return s.mid(start, i - start);
}

void expandUse(const QString &prefix, const QString &body, QStringList *out)
{
    QString s = body.trimmed();
    if (s.endsWith(QLatin1Char(';')))
        s.chop(1);
    int i = 0;
    skipWs(s, i);
    if (i < s.size() && s[i] == '{') {
        ++i;
        QString item;
        int depth = 1;
        while (i < s.size() && depth > 0) {
            const QChar c = s[i];
            if (c == '{') {
                ++depth;
                item += c;
            } else if (c == '}') {
                --depth;
                if (depth == 0)
                    break;
                item += c;
            } else if (c == ',' && depth == 1) {
                if (!item.trimmed().isEmpty())
                    expandUse(prefix, item, out);
                item.clear();
            } else {
                item += c;
            }
            ++i;
        }
        if (!item.trimmed().isEmpty())
            expandUse(prefix, item, out);
        return;
    }

    QString path;
    while (i < s.size()) {
        skipWs(s, i);
        if (i >= s.size())
            break;
        if (s.mid(i, 2) == QLatin1String("::")) {
            path += QLatin1String("::");
            i += 2;
            continue;
        }
        if (s[i] == '{') {
            const QString nextPrefix = prefix.isEmpty() ? path : (prefix + QLatin1String("::") + path);
            expandUse(nextPrefix, s.mid(i), out);
            return;
        }
        if (s.mid(i, 2) == QLatin1String("as") && (i + 2 >= s.size() || !s[i + 2].isLetterOrNumber())) {
            break;
        }
        if (s[i] == '*') {
            path += QLatin1Char('*');
            ++i;
            break;
        }
        const QString ident = readIdent(s, i);
        if (ident.isEmpty())
            break;
        if (!path.isEmpty() && !path.endsWith(QLatin1String("::")))
            path += QLatin1String("::");
        path += ident;
    }
    if (path.isEmpty())
        return;
    const QString full = prefix.isEmpty() ? path : (prefix + QLatin1String("::") + path);
    if (!out->contains(full))
        *out << full;
}

QString crateRootDir(const QString &file)
{
    QString dir = QFileInfo(file).absolutePath();
    for (int n = 0; n < 10; ++n) {
        if (QFileInfo::exists(dir + QStringLiteral("/lib.rs")) || QFileInfo::exists(dir + QStringLiteral("/main.rs")))
            return dir;
        const QString src = dir + QStringLiteral("/src");
        if (QFileInfo::exists(src + QStringLiteral("/lib.rs")) || QFileInfo::exists(src + QStringLiteral("/main.rs")))
            return src;
        const QString parent = QFileInfo(dir).absolutePath();
        if (parent == dir)
            break;
        dir = parent;
    }
    return QFileInfo(file).absolutePath();
}

QString childModDir(const QString &file)
{
    const QFileInfo fi(file);
    const QString base = fi.completeBaseName();
    if (fi.fileName() == QLatin1String("mod.rs") || base == QLatin1String("lib") || base == QLatin1String("main"))
        return fi.absolutePath();
    return fi.absolutePath() + QLatin1Char('/') + base;
}

QString resolveModIn(const QString &dir, const QString &name, const QMap<QString, FileNode *> &byPath)
{
    const QString a = QFileInfo(dir + QLatin1Char('/') + name + QStringLiteral(".rs")).absoluteFilePath();
    const QString b = QFileInfo(dir + QLatin1Char('/') + name + QStringLiteral("/mod.rs")).absoluteFilePath();
    if (byPath.contains(a))
        return a;
    if (byPath.contains(b))
        return b;
    return {};
}

QString parentModuleFile(const QString &file, const QMap<QString, FileNode *> &byPath)
{
    const QFileInfo fi(file);
    const QString dir = fi.absolutePath();
    if (fi.fileName() == QLatin1String("mod.rs")) {
        const QString parentDir = QFileInfo(dir).absolutePath();
        const QString name = QFileInfo(dir).fileName();
        const QStringList cands = {parentDir + QLatin1Char('/') + name + QStringLiteral(".rs"),
                                   parentDir + QStringLiteral("/mod.rs"), parentDir + QStringLiteral("/lib.rs"),
                                   parentDir + QStringLiteral("/main.rs")};
        for (const QString &cand : cands) {
            const QString abs = QFileInfo(cand).absoluteFilePath();
            if (byPath.contains(abs))
                return abs;
        }
        return {};
    }
    if (fi.completeBaseName() == QLatin1String("lib") || fi.completeBaseName() == QLatin1String("main"))
        return {};
    const QStringList cands = {dir + QStringLiteral("/mod.rs"), dir + QStringLiteral("/lib.rs"),
                               dir + QStringLiteral("/main.rs")};
    for (const QString &cand : cands) {
        const QString abs = QFileInfo(cand).absoluteFilePath();
        if (byPath.contains(abs))
            return abs;
    }
    return {};
}

QString resolveUsePath(const QString &fromFile, const QString &spec, const QMap<QString, FileNode *> &byPath,
                       QString *symbolOut)
{
    QStringList segs = spec.split(QStringLiteral("::"));
    segs.removeAll(QString());
    if (segs.isEmpty())
        return {};
    if (segs.last() == QLatin1String("*"))
        segs.removeLast();
    if (segs.isEmpty())
        return {};

    QString searchDir;
    int i = 0;
    if (segs[0] == QLatin1String("crate")) {
        searchDir = crateRootDir(fromFile);
        ++i;
    } else if (segs[0] == QLatin1String("super")) {
        const QString parent = parentModuleFile(fromFile, byPath);
        searchDir = parent.isEmpty() ? QFileInfo(fromFile).absolutePath() : childModDir(parent);
        ++i;
    } else if (segs[0] == QLatin1String("self")) {
        searchDir = childModDir(fromFile);
        ++i;
    } else {
        searchDir = childModDir(fromFile);
    }

    QString current;
    for (; i < segs.size(); ++i) {
        const QString hit = resolveModIn(searchDir, segs[i], byPath);
        if (hit.isEmpty())
            break;
        current = hit;
        searchDir = childModDir(hit);
    }
    if (current.isEmpty())
        return {};
    if (i < segs.size() && symbolOut)
        *symbolOut = segs[i];
    return current;
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

ParsedFile parseRustFile(const QString &path)
{
    ParsedFile parsed;
    parsed.node.path = QFileInfo(path).absoluteFilePath();
    parsed.node.fileName = QFileInfo(path).fileName();
    parsed.node.moduleName = QFileInfo(path).completeBaseName();
    parsed.node.language = SourceLanguage::Rust;
    const QString src = stripRustNoise(readFile(path));

    auto addKind = [&](const QRegularExpression &re, SymbolKind kind, const QString &prefix) {
        auto it = re.globalMatch(src);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            DefinedSymbol sym;
            sym.name = m.captured(1);
            sym.qualifiedName = sym.name;
            sym.kind = kind;
            sym.display = prefix + sym.name;
            if (kind == SymbolKind::Function)
                sym.display += QStringLiteral("()");
            sym.line = lineAt(src, m.capturedStart());
            addSymbol(parsed.node, sym);
        }
    };
    addKind(QRegularExpression(QStringLiteral(
                 "\\b(?:pub(?:\\([^)]*\\))?\\s+)?(?:async\\s+)?(?:unsafe\\s+)?(?:const\\s+)?fn\\s+([A-Za-z_][A-Za-z0-9_]*)")),
             SymbolKind::Function, QStringLiteral("fn "));
    addKind(QRegularExpression(QStringLiteral("\\b(?:pub(?:\\([^)]*\\))?\\s+)?struct\\s+([A-Za-z_][A-Za-z0-9_]*)")),
            SymbolKind::Class, QStringLiteral("struct "));
    addKind(QRegularExpression(QStringLiteral("\\b(?:pub(?:\\([^)]*\\))?\\s+)?enum\\s+([A-Za-z_][A-Za-z0-9_]*)")),
            SymbolKind::Class, QStringLiteral("enum "));
    addKind(QRegularExpression(QStringLiteral("\\b(?:pub(?:\\([^)]*\\))?\\s+)?trait\\s+([A-Za-z_][A-Za-z0-9_]*)")),
            SymbolKind::Class, QStringLiteral("trait "));
    addKind(QRegularExpression(QStringLiteral("\\b(?:pub(?:\\([^)]*\\))?\\s+)?type\\s+([A-Za-z_][A-Za-z0-9_]*)")),
            SymbolKind::Class, QStringLiteral("type "));
    addKind(QRegularExpression(QStringLiteral("\\b(?:pub(?:\\([^)]*\\))?\\s+)?(?:const|static)\\s+([A-Za-z_][A-Za-z0-9_]*)")),
            SymbolKind::Variable, QStringLiteral("const "));

    QRegularExpression modRe(QStringLiteral("\\b(?:pub(?:\\([^)]*\\))?\\s+)?mod\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*;"));
    auto mit = modRe.globalMatch(src);
    while (mit.hasNext()) {
        const QString name = mit.next().captured(1);
        if (!parsed.mods.contains(name))
            parsed.mods << name;
    }

    int pos = 0;
    while (pos < src.size()) {
        static const QRegularExpression useRe(QStringLiteral("\\b(?:pub(?:\\([^)]*\\))?\\s+)?use\\s+"));
        const QRegularExpressionMatch m = useRe.match(src, pos);
        if (!m.hasMatch())
            break;
        int i = m.capturedEnd();
        int depth = 0;
        const int start = i;
        while (i < src.size()) {
            if (src[i] == '{')
                ++depth;
            else if (src[i] == '}') {
                if (depth > 0)
                    --depth;
            } else if (src[i] == ';' && depth == 0)
                break;
            ++i;
        }
        expandUse(QString(), src.mid(start, i - start), &parsed.uses);
        pos = i + 1;
    }
    return parsed;
}

} // namespace

AnalysisResult RustAnalyzer::analyzeDirectory(const QString &rootDir)
{
    AnalysisResult result;
    QVector<ParsedFile> parsed;
    for (const QString &path : AnalysisUtil::scanFiles(rootDir)) {
        if (!AnalysisUtil::isRustFile(path))
            continue;
        parsed.push_back(parseRustFile(path));
    }

    QMap<QString, FileNode *> byPath;
    for (ParsedFile &p : parsed)
        byPath.insert(p.node.path, &p.node);

    QMap<QPair<QString, QString>, QVector<DefinedSymbol>> edges;
    for (const ParsedFile &consumer : parsed) {
        const QString fromDir = childModDir(consumer.node.path);
        for (const QString &name : consumer.mods) {
            const QString resolved = resolveModIn(fromDir, name, byPath);
            if (!resolved.isEmpty())
                addEdge(edges, resolved, consumer.node.path, {});
        }
        for (const QString &spec : consumer.uses) {
            QString symbol;
            const QString resolved = resolveUsePath(consumer.node.path, spec, byPath, &symbol);
            if (resolved.isEmpty())
                continue;
            QVector<DefinedSymbol> used;
            if (FileNode *provider = byPath.value(resolved)) {
                for (const DefinedSymbol &s : provider->symbols) {
                    if (!symbol.isEmpty() && s.name != symbol)
                        continue;
                    if (s.parentQualified.isEmpty())
                        addUsed(used, s, 0);
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
