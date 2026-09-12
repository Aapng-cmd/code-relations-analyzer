#include "PythonAnalyzer.h"
#include "AnalysisUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace {

struct Token {
    enum Type { Name, Number, String, Op, Newline, End };
    Type type = End;
    QString text;
    int line = 1;
};

struct ImportBinding {
    QString moduleSpec;
    QString moduleName;
    QString symbol;
    QString resolvedPath;
};

struct LocalType {
    QString moduleName;
    QString className;
};

struct Scope {
    int indent = -1;
    QString qualified;
    SymbolKind kind = SymbolKind::Class;
    bool isModule = true;
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

QString stripLineComment(const QString &raw)
{
    QString line = raw;
    line.replace(QLatin1Char('\r'), QString());
    bool inSingle = false;
    bool inDouble = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == '\\' && (inSingle || inDouble)) {
            ++i;
            continue;
        }
        if (!inDouble && c == '\'')
            inSingle = !inSingle;
        else if (!inSingle && c == '"')
            inDouble = !inDouble;
        else if (!inSingle && !inDouble && c == '#')
            return line.left(i);
    }
    return line;
}

QString dropImportLines(const QString &src)
{
    QStringList out;
    const QStringList lines = src.split('\n');
    for (const QString &raw : lines) {
        const QString trimmed = stripLineComment(raw).trimmed();
        if (trimmed.startsWith(QLatin1String("import ")) || trimmed.startsWith(QLatin1String("from ")))
            out << QString();
        else
            out << raw;
    }
    return out.join('\n');
}

int indentOf(const QString &line)
{
    int n = 0;
    for (const QChar c : line) {
        if (c == ' ')
            ++n;
        else if (c == '\t')
            n += 4;
        else
            break;
    }
    return n;
}

bool isIdentStart(QChar c)
{
    return c.isLetter() || c == '_';
}

bool isIdentPart(QChar c)
{
    return c.isLetterOrNumber() || c == '_';
}

QString takeIdent(const QString &s, int &pos)
{
    if (pos >= s.size() || !isIdentStart(s[pos]))
        return {};
    const int start = pos;
    ++pos;
    while (pos < s.size() && isIdentPart(s[pos]))
        ++pos;
    return s.mid(start, pos - start);
}

void skipSpaces(const QString &s, int &pos)
{
    while (pos < s.size() && s[pos].isSpace())
        ++pos;
}

QStringList parseParamList(const QString &inside)
{
    QStringList params;
    QString current;
    int depth = 0;
    for (const QChar c : inside) {
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
            current += c;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
            current += c;
        } else if (c == ',' && depth == 0) {
            if (!current.trimmed().isEmpty())
                params << current.trimmed();
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.trimmed().isEmpty())
        params << current.trimmed();

    QStringList names;
    for (QString p : params) {
        p = p.trimmed();
        if (p.startsWith(QLatin1String("**")))
            p = p.mid(2);
        else if (p.startsWith(QLatin1Char('*')))
            p = p.mid(1);
        const int eq = p.indexOf('=');
        if (eq >= 0)
            p = p.left(eq).trimmed();
        const int colon = p.indexOf(':');
        if (colon >= 0)
            p = p.left(colon).trimmed();
        if (p.isEmpty() || p == QLatin1String("self") || p == QLatin1String("cls"))
            continue;
        names << p;
    }
    return names;
}

QString formatFunctionDisplay(const QString &qualified, const QStringList &params)
{
    return QStringLiteral("def %1(%2)").arg(qualified, params.join(QLatin1String(", ")));
}

QString extractCallArgs(const QStringList &lines, int lineIndex, int openParenPos, QString &consumedLine)
{
    QString args;
    int depth = 0;
    for (int li = lineIndex; li < lines.size(); ++li) {
        const QString ln = stripLineComment(lines[li]);
        const int start = (li == lineIndex) ? openParenPos : 0;
        for (int k = start; k < ln.size(); ++k) {
            const QChar c = ln[k];
            if (c == '(') {
                ++depth;
                if (depth == 1)
                    continue;
            } else if (c == ')') {
                --depth;
                if (depth == 0) {
                    consumedLine = ln;
                    return args;
                }
            }
            if (depth >= 1)
                args += c;
        }
        args += ' ';
    }
    consumedLine = stripLineComment(lines[lineIndex]);
    return args;
}

void addSymbol(FileNode &node, const DefinedSymbol &sym)
{
    for (const DefinedSymbol &existing : node.symbols) {
        if (existing.kind == sym.kind && existing.qualifiedName == sym.qualifiedName)
            return;
    }
    node.symbols.push_back(sym);
}

QStringList splitImportList(QString rest)
{
    rest.replace('(', ' ');
    rest.replace(')', ' ');
    rest.replace('\\', ' ');
    return rest.split(',', Qt::SkipEmptyParts);
}

int countLeadingDots(const QString &s)
{
    int n = 0;
    while (n < s.size() && s[n] == '.')
        ++n;
    return n;
}

QString lastModuleComponent(const QString &spec)
{
    QString s = spec;
    while (s.startsWith(QLatin1Char('.')))
        s.remove(0, 1);
    if (s.isEmpty())
        return {};
    return s.split(QLatin1Char('.')).last();
}

QString resolveRelativePyPath(const QString &consumerPath, const QString &spec)
{
    if (!spec.startsWith(QLatin1Char('.')))
        return {};
    const int dots = countLeadingDots(spec);
    const QString rest = spec.mid(dots);
    if (rest.isEmpty())
        return {};
    QDir dir(QFileInfo(consumerPath).absolutePath());
    for (int i = 1; i < dots; ++i) {
        if (!dir.cdUp())
            break;
    }
    QString rel = rest;
    rel.replace(QLatin1Char('.'), QLatin1Char('/'));
    return QFileInfo(dir.filePath(rel + QStringLiteral(".py"))).absoluteFilePath();
}

QString sameDirPyPath(const QString &consumerPath, const QString &moduleName)
{
    if (moduleName.isEmpty())
        return {};
    QDir dir(QFileInfo(consumerPath).absolutePath());
    return QFileInfo(dir.filePath(moduleName + QStringLiteral(".py"))).absoluteFilePath();
}

struct ParsedFile {
    FileNode node;
    QMap<QString, ImportBinding> imports;
};

ParsedFile parsePythonFile(const QString &path)
{
    ParsedFile parsed;
    QFileInfo info(path);
    parsed.node.path = info.absoluteFilePath();
    parsed.node.fileName = info.fileName();
    if (info.fileName() == QLatin1String("__init__.py"))
        parsed.node.moduleName = info.dir().dirName();
    else
        parsed.node.moduleName = info.completeBaseName();

    const QStringList lines = readFile(path).split('\n');
    QVector<Scope> stack;
    stack.push_back(Scope{});

    auto current = [&]() -> const Scope & { return stack.back(); };

    for (int li = 0; li < lines.size(); ++li) {
        QString line = stripLineComment(lines[li]);
        if (line.trimmed().isEmpty())
            continue;

        const int indent = indentOf(line);
        while (stack.size() > 1 && indent <= stack.back().indent)
            stack.pop_back();

        const QString trimmed = line.trimmed();
        int pos = 0;
        while (pos < line.size() && line[pos].isSpace())
            ++pos;

        if (trimmed.startsWith(QLatin1String("from ")) && current().isModule) {
            int p = pos + 4;
            skipSpaces(line, p);
            QString mod;
            if (p < line.size() && line[p] == '.') {
                while (p < line.size() && (line[p] == '.' || isIdentPart(line[p])))
                    mod += line[p++];
            } else {
                mod = takeIdent(line, p);
                while (p < line.size() && line[p] == '.') {
                    ++p;
                    const QString part = takeIdent(line, p);
                    if (!part.isEmpty())
                        mod += QLatin1Char('.') + part;
                }
            }
            skipSpaces(line, p);
            if (line.mid(p, 6) != QLatin1String("import"))
                continue;
            p += 6;
            const QString rest = line.mid(p);
            const bool fromDotOnly = countLeadingDots(mod) == mod.size() && !mod.isEmpty();
            for (QString part : splitImportList(rest)) {
                part = part.trimmed();
                if (part.isEmpty() || part == QLatin1String("*"))
                    continue;
                QString orig = part;
                QString local = part;
                const int asIdx = part.indexOf(QLatin1String(" as "));
                if (asIdx >= 0) {
                    orig = part.left(asIdx).trimmed();
                    local = part.mid(asIdx + 4).trimmed();
                }
                ImportBinding b;
                b.moduleSpec = fromDotOnly ? (mod + orig) : mod;
                if (fromDotOnly) {
                    b.moduleName = orig.split(QLatin1Char('.')).last();
                    b.symbol = QString();
                    b.resolvedPath = resolveRelativePyPath(path, b.moduleSpec);
                } else {
                    b.moduleName = lastModuleComponent(mod);
                    b.symbol = orig;
                    if (mod.startsWith(QLatin1Char('.')))
                        b.resolvedPath = resolveRelativePyPath(path, mod);
                    else
                        b.resolvedPath = sameDirPyPath(path, b.moduleName);
                }
                parsed.imports.insert(local, b);
            }
            continue;
        }

        if (trimmed.startsWith(QLatin1String("import ")) && current().isModule) {
            const QString rest = trimmed.mid(7);
            for (QString part : splitImportList(rest)) {
                part = part.trimmed();
                if (part.isEmpty())
                    continue;
                QString spec = part;
                QString local;
                const int asIdx = part.indexOf(QLatin1String(" as "));
                if (asIdx >= 0) {
                    spec = part.left(asIdx).trimmed();
                    local = part.mid(asIdx + 4).trimmed();
                } else {
                    local = spec.split('.').last();
                }
                ImportBinding b;
                b.moduleSpec = spec;
                b.moduleName = spec.split('.').last();
                b.symbol = QString();
                if (spec.startsWith(QLatin1Char('.')))
                    b.resolvedPath = resolveRelativePyPath(path, spec);
                else
                    b.resolvedPath = sameDirPyPath(path, b.moduleName);
                parsed.imports.insert(local, b);
            }
            continue;
        }

        if (trimmed.startsWith(QLatin1String("class ")) && pos + 6 <= line.size()) {
            int p = pos + 6;
            skipSpaces(line, p);
            const QString name = takeIdent(line, p);
            if (name.isEmpty())
                continue;
            DefinedSymbol sym;
            sym.name = name;
            sym.parentQualified = current().isModule ? QString() : current().qualified;
            sym.qualifiedName = sym.parentQualified.isEmpty() ? name : (sym.parentQualified + QLatin1Char('.') + name);
            sym.kind = SymbolKind::Class;
            sym.display = QStringLiteral("class %1").arg(sym.qualifiedName);
            sym.line = li + 1;
            addSymbol(parsed.node, sym);
            Scope sc;
            sc.indent = indent;
            sc.qualified = sym.qualifiedName;
            sc.kind = SymbolKind::Class;
            sc.isModule = false;
            stack.push_back(sc);
            continue;
        }

        if (trimmed.startsWith(QLatin1String("def ")) && pos + 4 <= line.size()) {
            int p = pos + 4;
            skipSpaces(line, p);
            const QString name = takeIdent(line, p);
            skipSpaces(line, p);
            QString params;
            if (p < line.size() && line[p] == '(') {
                QString dummy;
                params = extractCallArgs(lines, li, p, dummy);
            }
            DefinedSymbol sym;
            sym.name = name;
            const bool parentIsClass = !current().isModule && current().kind == SymbolKind::Class;
            const bool parentIsFunc = !current().isModule && current().kind == SymbolKind::Function;
            if (parentIsClass || parentIsFunc)
                sym.parentQualified = current().qualified;
            sym.qualifiedName = sym.parentQualified.isEmpty() ? name : (sym.parentQualified + QLatin1Char('.') + name);
            sym.kind = SymbolKind::Function;
            sym.parameters = parseParamList(params);
            sym.display = formatFunctionDisplay(sym.qualifiedName, sym.parameters);
            sym.line = li + 1;
            addSymbol(parsed.node, sym);
            for (const QString &param : sym.parameters) {
                DefinedSymbol var;
                var.name = param;
                var.parentQualified = sym.qualifiedName;
                var.qualifiedName = sym.qualifiedName + QLatin1Char('.') + param;
                var.kind = SymbolKind::Variable;
                var.display = param;
                var.line = li + 1;
                addSymbol(parsed.node, var);
            }
            Scope sc;
            sc.indent = indent;
            sc.qualified = sym.qualifiedName;
            sc.kind = SymbolKind::Function;
            sc.isModule = false;
            stack.push_back(sc);
            continue;
        }

        auto addAssignment = [&](const QString &name, const QString &display) {
            if (name.isEmpty() || name == QLatin1String("self") || name == QLatin1String("cls"))
                return;
            DefinedSymbol var;
            var.name = name;
            var.parentQualified = current().isModule ? QString() : current().qualified;
            var.qualifiedName = var.parentQualified.isEmpty() ? name : (var.parentQualified + QLatin1Char('.') + name);
            var.kind = SymbolKind::Variable;
            var.display = display;
            var.line = li + 1;
            addSymbol(parsed.node, var);
        };

        // self.x = ...  or  name = ...
        int p = pos;
        if (p + 5 <= line.size() && line.mid(p, 5) == QLatin1String("self.")) {
            p += 5;
            const QString name = takeIdent(line, p);
            skipSpaces(line, p);
            if (!name.isEmpty() && p < line.size() && line[p] == '=')
                addAssignment(QStringLiteral("self.%1").arg(name), QStringLiteral("self.%1").arg(name));
            continue;
        }

        if (p < line.size() && isIdentStart(line[p])) {
            const QString name = takeIdent(line, p);
            skipSpaces(line, p);
            if (p < line.size() && line[p] == '=' && (p + 1 >= line.size() || line[p + 1] != '='))
                addAssignment(name, name);
        }
    }

    return parsed;
}

QString stripCommentsAndKeepStrings(const QString &src)
{
    QString out;
    out.reserve(src.size());
    bool inTripleSingle = false;
    bool inTripleDouble = false;
    bool inSingle = false;
    bool inDouble = false;
    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src[i];
        const QChar next = (i + 1 < src.size()) ? src[i + 1] : QChar();
        const QChar next2 = (i + 2 < src.size()) ? src[i + 2] : QChar();
        auto startsTriple = [&](QChar q) { return c == q && next == q && next2 == q; };

        if (!inSingle && !inDouble && !inTripleSingle && !inTripleDouble) {
            if (startsTriple('\'')) {
                inTripleSingle = true;
                out += "'''";
                i += 2;
                continue;
            }
            if (startsTriple('"')) {
                inTripleDouble = true;
                out += "\"\"\"";
                i += 2;
                continue;
            }
            if (c == '\'') {
                inSingle = true;
                out += c;
                continue;
            }
            if (c == '"') {
                inDouble = true;
                out += c;
                continue;
            }
            if (c == '#') {
                while (i < src.size() && src[i] != '\n')
                    ++i;
                if (i < src.size())
                    out += '\n';
                continue;
            }
            out += c;
            continue;
        }
        out += c;
        if (c == '\\' && i + 1 < src.size()) {
            out += src[++i];
            continue;
        }
        if (inTripleSingle && startsTriple('\'')) {
            out += "''";
            i += 2;
            inTripleSingle = false;
        } else if (inTripleDouble && startsTriple('"')) {
            out += "\"\"";
            i += 2;
            inTripleDouble = false;
        } else if (inSingle && c == '\'')
            inSingle = false;
        else if (inDouble && c == '"')
            inDouble = false;
    }
    return out;
}

QVector<Token> tokenize(const QString &src)
{
    QVector<Token> tokens;
    const QString cleaned = stripCommentsAndKeepStrings(src);
    int i = 0;
    int line = 1;
    auto push = [&](Token::Type t, const QString &text) { tokens.push_back({t, text, line}); };
    while (i < cleaned.size()) {
        const QChar c = cleaned[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (c == '\'' || c == '"') {
            const bool triple = (i + 2 < cleaned.size() && cleaned[i + 1] == c && cleaned[i + 2] == c);
            const int start = i;
            i += triple ? 3 : 1;
            while (i < cleaned.size()) {
                if (cleaned[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (triple) {
                    if (i + 2 < cleaned.size() && cleaned[i] == c && cleaned[i + 1] == c && cleaned[i + 2] == c) {
                        i += 3;
                        break;
                    }
                } else if (cleaned[i] == c) {
                    ++i;
                    break;
                }
                ++i;
            }
            push(Token::String, cleaned.mid(start, i - start));
            continue;
        }
        if (isIdentStart(c)) {
            int start = i++;
            while (i < cleaned.size() && isIdentPart(cleaned[i]))
                ++i;
            push(Token::Name, cleaned.mid(start, i - start));
            continue;
        }
        if (c.isDigit()) {
            int start = i++;
            while (i < cleaned.size() && (cleaned[i].isLetterOrNumber() || cleaned[i] == '.' || cleaned[i] == '_'))
                ++i;
            push(Token::Number, cleaned.mid(start, i - start));
            continue;
        }
        push(Token::Op, QString(c));
        ++i;
    }
    push(Token::End, QString());
    return tokens;
}

const DefinedSymbol *findByNameParent(const FileNode &file, const QString &name, const QString &parentQualified)
{
    for (const DefinedSymbol &s : file.symbols) {
        if (s.name == name && s.parentQualified == parentQualified)
            return &s;
    }
    return nullptr;
}

const DefinedSymbol *findByQualified(const FileNode &file, const QString &qualified)
{
    for (const DefinedSymbol &s : file.symbols) {
        if (s.qualifiedName == qualified)
            return &s;
    }
    return nullptr;
}

void addUsed(QVector<DefinedSymbol> &used, const DefinedSymbol &sym, int useLine, bool recordUse)
{
    auto record = [&](DefinedSymbol &u) {
        if (!recordUse || useLine <= 0)
            return;
        if (u.kind != SymbolKind::Class && u.kind != SymbolKind::Function)
            return;
        if (!u.useLines.contains(useLine))
            u.useLines.push_back(useLine);
    };
    for (DefinedSymbol &u : used) {
        if (u.qualifiedName == sym.qualifiedName && u.kind == sym.kind) {
            record(u);
            return;
        }
    }
    DefinedSymbol copy = sym;
    copy.useLines.clear();
    record(copy);
    used.push_back(copy);
}

void addUsedWithAncestors(QVector<DefinedSymbol> &used, const FileNode &provider, const DefinedSymbol &sym,
                          int useLine)
{
    addUsed(used, sym, useLine, true);
    QString parent = sym.parentQualified;
    while (!parent.isEmpty()) {
        const DefinedSymbol *anc = findByQualified(provider, parent);
        if (!anc)
            break;
        addUsed(used, *anc, 0, false);
        parent = anc->parentQualified;
    }
}

FileNode *providerFor(const ImportBinding &b, const QMap<QString, FileNode *> &byPath,
                      const QMap<QString, FileNode *> &byName)
{
    if (!b.resolvedPath.isEmpty()) {
        FileNode *n = byPath.value(b.resolvedPath, nullptr);
        if (n)
            return n;
    }
    return byName.value(b.moduleName, nullptr);
}

bool pathIsUnder(const QString &filePath, const QString &dirPath)
{
    const QString file = QFileInfo(filePath).absoluteFilePath();
    const QString dir = QDir(dirPath).absolutePath();
    return file.startsWith(dir + QLatin1Char('/'));
}

QString findModuleOrPackage(const QString &consumerPath, const QString &rootDir, const QString &spec)
{
    if (spec.isEmpty() || spec.startsWith(QLatin1Char('.')))
        return {};
    QString rel = spec;
    rel.replace(QLatin1Char('.'), QDir::separator());
    QDir dir(QFileInfo(consumerPath).absolutePath());
    const QString root = QDir(rootDir).absolutePath();
    while (true) {
        const QString py = QFileInfo(dir.filePath(rel + QStringLiteral(".py"))).absoluteFilePath();
        if (QFileInfo::exists(py) && QFileInfo(py).isFile())
            return py;
        const QString pkg = QFileInfo(dir.filePath(rel)).absoluteFilePath();
        if (QFileInfo(pkg).isDir())
            return pkg;
        if (dir.absolutePath() == root)
            break;
        if (!dir.cdUp())
            break;
    }
    return {};
}

FileNode *findDefiningFile(const QString &packageDir, const QString &symbol, QVector<ParsedFile> &parsed)
{
    if (symbol.isEmpty())
        return nullptr;
    FileNode *initHit = nullptr;
    for (ParsedFile &p : parsed) {
        if (!pathIsUnder(p.node.path, packageDir))
            continue;
        if (!findByNameParent(p.node, symbol, QString()))
            continue;
        if (p.node.fileName == QLatin1String("__init__.py"))
            initHit = &p.node;
        else
            return &p.node;
    }
    return initHit;
}

void refineImports(ParsedFile &consumer, const QString &rootDir, QVector<ParsedFile> &parsed,
                   const QMap<QString, FileNode *> &byPath)
{
    for (auto it = consumer.imports.begin(); it != consumer.imports.end(); ++it) {
        ImportBinding &b = it.value();
        const QString spec = b.moduleSpec.isEmpty() ? b.moduleName : b.moduleSpec;
        if (spec.startsWith(QLatin1Char('.'))) {
            if (!b.symbol.isEmpty() && !b.resolvedPath.isEmpty()) {
                const QFileInfo fi(b.resolvedPath);
                if (fi.fileName() == QLatin1String("__init__.py")) {
                    if (FileNode *def = findDefiningFile(fi.absolutePath(), b.symbol, parsed))
                        b.resolvedPath = def->path;
                }
            }
            continue;
        }
        const QString found = findModuleOrPackage(consumer.node.path, rootDir, spec);
        if (found.isEmpty())
            continue;
        if (found.endsWith(QLatin1String(".py"))) {
            b.resolvedPath = QFileInfo(found).absoluteFilePath();
            continue;
        }
        if (!b.symbol.isEmpty()) {
            if (FileNode *def = findDefiningFile(found, b.symbol, parsed))
                b.resolvedPath = def->path;
        } else {
            const QString initPath =
                QFileInfo(QDir(found).filePath(QStringLiteral("__init__.py"))).absoluteFilePath();
            if (byPath.contains(initPath))
                b.resolvedPath = initPath;
        }
    }
}

void analyzeUsages(const ParsedFile &consumer, const QMap<QString, FileNode *> &byPath,
                   const QMap<QString, FileNode *> &byName, QMap<QString, QVector<DefinedSymbol>> &usedByProvider)
{
    const QVector<Token> tokens = tokenize(dropImportLines(readFile(consumer.node.path)));
    QMap<QString, LocalType> locals;

    auto resolveRhsType = [&](const QString &rhs) -> LocalType {
        const QStringList parts = rhs.split('.');
        if (parts.size() == 1) {
            auto it = consumer.imports.find(parts[0]);
            if (it != consumer.imports.end() && !it->symbol.isEmpty())
                return LocalType{it->moduleName, it->symbol};
            if (locals.contains(parts[0]))
                return locals.value(parts[0]);
            return {};
        }
        if (parts.size() >= 2) {
            auto it = consumer.imports.find(parts[0]);
            if (it != consumer.imports.end() && it->symbol.isEmpty())
                return LocalType{it->moduleName, parts[1]};
        }
        return {};
    };

    for (int i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name)
            continue;
        if (tokens[i + 1].type == Token::Op && tokens[i + 1].text == QLatin1String("=")) {
            QString rhs;
            int j = i + 2;
            if (j < tokens.size() && tokens[j].type == Token::Name) {
                rhs = tokens[j].text;
                ++j;
                while (j + 1 < tokens.size() && tokens[j].type == Token::Op && tokens[j].text == QLatin1String(".")
                       && tokens[j + 1].type == Token::Name) {
                    rhs += QLatin1Char('.') + tokens[j + 1].text;
                    j += 2;
                }
                const LocalType t = resolveRhsType(rhs);
                if (!t.className.isEmpty())
                    locals.insert(tokens[i].text, t);
            }
        }
    }

    auto markAttr = [&](const QString &base, const QString &attr, int line) {
        auto imp = consumer.imports.find(base);
        if (imp != consumer.imports.end()) {
            FileNode *provider = providerFor(*imp, byPath, byName);
            if (!provider)
                return;
            if (imp->symbol.isEmpty()) {
                if (const DefinedSymbol *sym = findByNameParent(*provider, attr, QString()))
                    addUsedWithAncestors(usedByProvider[provider->path], *provider, *sym, line);
            }
            return;
        }
        if (locals.contains(base)) {
            const LocalType t = locals.value(base);
            FileNode *provider = byName.value(t.moduleName, nullptr);
            if (!provider) {
                for (auto it = consumer.imports.begin(); it != consumer.imports.end(); ++it) {
                    if (it->moduleName == t.moduleName) {
                        provider = providerFor(*it, byPath, byName);
                        break;
                    }
                }
            }
            if (!provider)
                return;
            if (const DefinedSymbol *cls = findByNameParent(*provider, t.className, QString()))
                addUsedWithAncestors(usedByProvider[provider->path], *provider, *cls, 0);
            if (const DefinedSymbol *method = findByNameParent(*provider, attr, t.className))
                addUsedWithAncestors(usedByProvider[provider->path], *provider, *method, line);
            else if (const DefinedSymbol *var = findByNameParent(*provider, attr, t.className))
                addUsedWithAncestors(usedByProvider[provider->path], *provider, *var, line);
        }
    };

    for (auto it = consumer.imports.begin(); it != consumer.imports.end(); ++it) {
        if (it->symbol.isEmpty())
            continue;
        FileNode *provider = providerFor(*it, byPath, byName);
        if (!provider)
            continue;
        for (const Token &tk : tokens) {
            if (tk.type == Token::Name && tk.text == it.key()) {
                if (const DefinedSymbol *sym = findByNameParent(*provider, it->symbol, QString()))
                    addUsedWithAncestors(usedByProvider[provider->path], *provider, *sym, tk.line);
            }
        }
    }

    for (int i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens[i].type == Token::Name && tokens[i + 1].type == Token::Op && tokens[i + 1].text == QLatin1String(".")
            && tokens[i + 2].type == Token::Name)
            markAttr(tokens[i].text, tokens[i + 2].text, tokens[i].line);
    }

    for (int i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name)
            continue;
        if (!(tokens[i + 1].type == Token::Op && tokens[i + 1].text == QLatin1String("(")))
            continue;
        if (i >= 2 && tokens[i - 1].type == Token::Op && tokens[i - 1].text == QLatin1String(".")
            && tokens[i - 2].type == Token::Name) {
            auto imp = consumer.imports.find(tokens[i - 2].text);
            if (imp != consumer.imports.end() && imp->symbol.isEmpty()) {
                FileNode *provider = providerFor(*imp, byPath, byName);
                if (provider) {
                    if (const DefinedSymbol *cls = findByNameParent(*provider, tokens[i].text, QString()))
                        addUsedWithAncestors(usedByProvider[provider->path], *provider, *cls, tokens[i].line);
                }
            }
        }
    }
}

} // namespace

AnalysisResult PythonAnalyzer::analyzeDirectory(const QString &rootDir)
{
    AnalysisResult result;
    QVector<ParsedFile> parsed;
    for (const QString &path : AnalysisUtil::scanFiles(rootDir)) {
        if (!AnalysisUtil::isPythonFile(path))
            continue;
        parsed.push_back(parsePythonFile(path));
    }

    QMap<QString, FileNode *> byName;
    QMap<QString, FileNode *> byPath;
    for (ParsedFile &p : parsed) {
        byPath.insert(p.node.path, &p.node);
        if (p.node.fileName == QLatin1String("__init__.py")) {
            if (!byName.contains(p.node.moduleName))
                byName.insert(p.node.moduleName, &p.node);
        } else {
            byName.insert(p.node.moduleName, &p.node);
        }
    }
    for (ParsedFile &p : parsed)
        refineImports(p, rootDir, parsed, byPath);

    QMap<QPair<QString, QString>, QVector<DefinedSymbol>> edgeUsed;
    for (const ParsedFile &consumer : parsed) {
        QMap<QString, QVector<DefinedSymbol>> usedByProvider;
        analyzeUsages(consumer, byPath, byName, usedByProvider);

        QSet<FileNode *> importedProviders;
        for (const ImportBinding &b : consumer.imports) {
            FileNode *provider = providerFor(b, byPath, byName);
            if (provider && provider->path != consumer.node.path)
                importedProviders.insert(provider);
        }

        for (FileNode *provider : importedProviders) {
            const QVector<DefinedSymbol> used = usedByProvider.value(provider->path);
            if (used.isEmpty())
                continue;
            edgeUsed[qMakePair(provider->path, consumer.node.path)] = used;
        }
    }

    for (const ParsedFile &p : parsed)
        result.files.push_back(p.node);

    for (auto it = edgeUsed.begin(); it != edgeUsed.end(); ++it) {
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
