#include "JavaAnalyzer.h"
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
    enum Type { Name, Number, String, Op, End };
    Type type = End;
    QString text;
    int line = 1;
};

struct ImportBinding {
    QString spec;
    QString simpleName;
    QString symbol;
    bool wildcard = false;
    bool isStatic = false;
    QString resolvedPath;
};

struct LocalType {
    QString className;
    QString providerPath;
};

enum class ScopeKind { File, Type, Function, Block };

struct Scope {
    ScopeKind kind = ScopeKind::File;
    QString qualified;
    int depth = -1;
};

struct ParsedFile {
    FileNode node;
    QString packageName;
    QVector<ImportBinding> imports;
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

bool isIdentPart(QChar c)
{
    return c.isLetterOrNumber() || c == '_';
}

bool isControlKeyword(const QString &n)
{
    static const QSet<QString> k = {
        QStringLiteral("if"),       QStringLiteral("else"),     QStringLiteral("for"),
        QStringLiteral("while"),    QStringLiteral("do"),       QStringLiteral("switch"),
        QStringLiteral("case"),     QStringLiteral("default"),  QStringLiteral("catch"),
        QStringLiteral("try"),      QStringLiteral("finally"),  QStringLiteral("return"),
        QStringLiteral("throw"),    QStringLiteral("new"),      QStringLiteral("instanceof"),
        QStringLiteral("assert"),   QStringLiteral("break"),    QStringLiteral("continue"),
        QStringLiteral("package"),  QStringLiteral("import"),   QStringLiteral("class"),
        QStringLiteral("interface"), QStringLiteral("enum"),    QStringLiteral("record"),
        QStringLiteral("extends"),  QStringLiteral("implements"), QStringLiteral("throws"),
        QStringLiteral("this"),     QStringLiteral("super"),    QStringLiteral("var"),
        QStringLiteral("yield"),    QStringLiteral("synchronized")};
    return k.contains(n);
}

bool isModifierOrType(const QString &n)
{
    static const QSet<QString> k = {
        QStringLiteral("public"),    QStringLiteral("private"),   QStringLiteral("protected"),
        QStringLiteral("static"),    QStringLiteral("final"),     QStringLiteral("abstract"),
        QStringLiteral("native"),    QStringLiteral("strictfp"),  QStringLiteral("transient"),
        QStringLiteral("volatile"),  QStringLiteral("synchronized"), QStringLiteral("default"),
        QStringLiteral("sealed"),    QStringLiteral("non-sealed"), QStringLiteral("void"),
        QStringLiteral("int"),       QStringLiteral("long"),      QStringLiteral("short"),
        QStringLiteral("byte"),      QStringLiteral("char"),      QStringLiteral("boolean"),
        QStringLiteral("float"),     QStringLiteral("double"),    QStringLiteral("var"),
        QStringLiteral("class"),     QStringLiteral("interface"), QStringLiteral("enum"),
        QStringLiteral("record"),    QStringLiteral("throws")};
    return k.contains(n);
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

void addUsedWithAncestors(QVector<DefinedSymbol> &used, const FileNode &provider, const DefinedSymbol &sym, int useLine)
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

QString joinQualified(const QString &parent, const QString &name)
{
    if (parent.isEmpty())
        return name;
    return parent + QLatin1Char('.') + name;
}

QString stripComments(const QString &src)
{
    QString out;
    out.reserve(src.size());
    bool inLine = false;
    bool inBlock = false;
    bool inString = false;
    bool inChar = false;
    bool inText = false;
    QChar strQ;
    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src[i];
        const QChar next = (i + 1 < src.size()) ? src[i + 1] : QChar();
        const QChar next2 = (i + 2 < src.size()) ? src[i + 2] : QChar();
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
        if (inText) {
            out += c;
            if (c == '"' && next == '"' && next2 == '"') {
                inText = false;
                out += "\"\"";
                i += 2;
            }
            continue;
        }
        if (inString || inChar) {
            out += c;
            if (c == '\\' && i + 1 < src.size()) {
                out += src[++i];
                continue;
            }
            if (c == strQ) {
                inString = false;
                inChar = false;
            }
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
        if (c == '"' && next == '"' && next2 == '"') {
            inText = true;
            i += 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            inString = c == '"';
            inChar = c == '\'';
            strQ = c;
            out += c;
            continue;
        }
        out += c;
    }
    return out;
}

QVector<Token> tokenize(const QString &src)
{
    QVector<Token> tokens;
    const QString cleaned = stripComments(src);
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
        if (c == '"' || c == '\'') {
            const int start = i++;
            while (i < cleaned.size() && cleaned[i] != c) {
                if (cleaned[i] == '\\')
                    i += 2;
                else
                    ++i;
            }
            if (i < cleaned.size())
                ++i;
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
        if (cleaned.mid(i, 2) == QLatin1String("::")) {
            push(Token::Op, QStringLiteral("::"));
            i += 2;
            continue;
        }
        push(Token::Op, QString(c));
        ++i;
    }
    push(Token::End, QString());
    return tokens;
}

int skipBalanced(const QVector<Token> &tokens, int i, const QString &open, const QString &close)
{
    if (i >= tokens.size() || tokens[i].text != open)
        return i;
    int depth = 0;
    for (; i < tokens.size() && tokens[i].type != Token::End; ++i) {
        if (tokens[i].text == open)
            ++depth;
        else if (tokens[i].text == close) {
            --depth;
            if (depth == 0)
                return i + 1;
        }
    }
    return i;
}

QStringList parseParamNames(const QVector<Token> &tokens, int open, int close)
{
    QStringList names;
    QVector<Token> cur;
    int depth = 0;
    auto flush = [&]() {
        QString name;
        for (int k = cur.size() - 1; k >= 0; --k) {
            if (cur[k].type != Token::Name)
                continue;
            if (isModifierOrType(cur[k].text) || isControlKeyword(cur[k].text))
                continue;
            name = cur[k].text;
            break;
        }
        if (!name.isEmpty())
            names << name;
        cur.clear();
    };
    for (int i = open + 1; i < close && i < tokens.size(); ++i) {
        if (tokens[i].text == QLatin1String("(") || tokens[i].text == QLatin1String("<") || tokens[i].text == QLatin1String("["))
            ++depth;
        else if (tokens[i].text == QLatin1String(")") || tokens[i].text == QLatin1String(">") || tokens[i].text == QLatin1String("]"))
            --depth;
        if (tokens[i].text == QLatin1String(",") && depth == 0) {
            flush();
            continue;
        }
        cur.push_back(tokens[i]);
    }
    if (!cur.isEmpty())
        flush();
    return names;
}

QString currentType(const QVector<Scope> &stack)
{
    for (int i = stack.size() - 1; i >= 0; --i) {
        if (stack[i].kind == ScopeKind::Type)
            return stack[i].qualified;
    }
    return {};
}

ScopeKind currentKind(const QVector<Scope> &stack)
{
    return stack.isEmpty() ? ScopeKind::File : stack.back().kind;
}

void extractHeader(const QString &src, ParsedFile &parsed)
{
    const QStringList lines = stripComments(src).split('\n');
    for (const QString &raw : lines) {
        const QString t = raw.trimmed();
        if (t.startsWith(QLatin1String("package "))) {
            QString rest = t.mid(8).trimmed();
            if (rest.endsWith(';'))
                rest.chop(1);
            parsed.packageName = rest.trimmed();
            continue;
        }
        if (!t.startsWith(QLatin1String("import ")))
            continue;
        QString rest = t.mid(7).trimmed();
        if (rest.endsWith(';'))
            rest.chop(1);
        rest = rest.trimmed();
        ImportBinding b;
        if (rest.startsWith(QLatin1String("static "))) {
            b.isStatic = true;
            rest = rest.mid(7).trimmed();
        }
        b.spec = rest;
        if (rest.endsWith(QLatin1String(".*"))) {
            b.wildcard = true;
            b.spec = rest.left(rest.size() - 2);
        } else {
            const int dot = rest.lastIndexOf('.');
            b.simpleName = (dot >= 0) ? rest.mid(dot + 1) : rest;
            if (b.isStatic) {
                b.symbol = b.simpleName;
                const QString cls = (dot >= 0) ? rest.left(dot) : rest;
                const int cdot = cls.lastIndexOf('.');
                b.simpleName = (cdot >= 0) ? cls.mid(cdot + 1) : cls;
                b.spec = cls;
            }
        }
        parsed.imports.push_back(b);
    }
}

ParsedFile parseJavaFile(const QString &path)
{
    ParsedFile parsed;
    QFileInfo info(path);
    parsed.node.path = info.absoluteFilePath();
    parsed.node.fileName = info.fileName();
    parsed.node.moduleName = info.completeBaseName();
    const QString src = readFile(path);
    extractHeader(src, parsed);

    const QVector<Token> tokens = tokenize(src);
    QVector<Scope> stack;
    stack.push_back(Scope{});
    Scope pending;
    bool hasPending = false;
    int braceDepth = 0;

    int i = 0;
    while (i < tokens.size() && tokens[i].type != Token::End) {
        const Token &t = tokens[i];
        if (t.text == QLatin1String("{")) {
            if (hasPending) {
                pending.depth = braceDepth;
                stack.push_back(pending);
                hasPending = false;
                pending = Scope{};
            } else {
                Scope blk;
                blk.kind = currentKind(stack) == ScopeKind::Type ? ScopeKind::Block : ScopeKind::Block;
                blk.qualified = currentType(stack);
                blk.depth = braceDepth;
                stack.push_back(blk);
            }
            ++braceDepth;
            ++i;
            continue;
        }
        if (t.text == QLatin1String("}")) {
            --braceDepth;
            while (stack.size() > 1 && stack.back().depth >= braceDepth)
                stack.pop_back();
            ++i;
            continue;
        }
        if (t.text == QLatin1String("@")) {
            ++i;
            if (i < tokens.size() && tokens[i].type == Token::Name)
                ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String("("))
                i = skipBalanced(tokens, i, QStringLiteral("("), QStringLiteral(")"));
            continue;
        }
        if (currentKind(stack) == ScopeKind::Function || currentKind(stack) == ScopeKind::Block) {
            ++i;
            continue;
        }
        if (t.type != Token::Name) {
            ++i;
            continue;
        }
        if (t.text == QLatin1String("public") || t.text == QLatin1String("private")
            || t.text == QLatin1String("protected") || t.text == QLatin1String("static")
            || t.text == QLatin1String("final") || t.text == QLatin1String("abstract")
            || t.text == QLatin1String("native") || t.text == QLatin1String("strictfp")
            || t.text == QLatin1String("transient") || t.text == QLatin1String("volatile")
            || t.text == QLatin1String("synchronized") || t.text == QLatin1String("sealed")
            || t.text == QLatin1String("default")) {
            ++i;
            continue;
        }
        if (t.text == QLatin1String("package") || t.text == QLatin1String("import")) {
            while (i < tokens.size() && tokens[i].text != QLatin1String(";") && tokens[i].type != Token::End)
                ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String(";"))
                ++i;
            continue;
        }
        if (t.text == QLatin1String("class") || t.text == QLatin1String("interface") || t.text == QLatin1String("enum")
            || t.text == QLatin1String("record")) {
            const QString kw = t.text;
            ++i;
            if (i >= tokens.size() || tokens[i].type != Token::Name)
                continue;
            const QString name = tokens[i].text;
            const int line = tokens[i].line;
            ++i;
            QStringList recordParams;
            if (kw == QLatin1String("record") && i < tokens.size() && tokens[i].text == QLatin1String("(")) {
                const int close = skipBalanced(tokens, i, QStringLiteral("("), QStringLiteral(")")) - 1;
                recordParams = parseParamNames(tokens, i, close);
                i = close + 1;
            }
            while (i < tokens.size() && tokens[i].text != QLatin1String("{") && tokens[i].text != QLatin1String(";"))
                ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String(";")) {
                ++i;
                continue;
            }
            const QString parent = currentType(stack);
            DefinedSymbol sym;
            sym.name = name;
            sym.parentQualified = parent;
            sym.qualifiedName = joinQualified(parent, name);
            sym.kind = SymbolKind::Class;
            sym.display = kw + QLatin1Char(' ') + sym.qualifiedName;
            sym.line = line;
            addSymbol(parsed.node, sym);
            for (const QString &p : recordParams) {
                DefinedSymbol var;
                var.name = p;
                var.parentQualified = sym.qualifiedName;
                var.qualifiedName = joinQualified(sym.qualifiedName, p);
                var.kind = SymbolKind::Variable;
                var.display = p;
                var.line = line;
                addSymbol(parsed.node, var);
            }
            pending.kind = ScopeKind::Type;
            pending.qualified = sym.qualifiedName;
            hasPending = true;
            continue;
        }
        if (isControlKeyword(t.text) && t.text != QLatin1String("default")) {
            ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String("("))
                i = skipBalanced(tokens, i, QStringLiteral("("), QStringLiteral(")"));
            continue;
        }

        int j = i;
        int paren = 0;
        int firstParen = -1;
        while (j < tokens.size() && tokens[j].type != Token::End) {
            if (tokens[j].text == QLatin1String("(")) {
                if (paren == 0 && firstParen < 0)
                    firstParen = j;
                ++paren;
            } else if (tokens[j].text == QLatin1String(")")) {
                --paren;
            } else if (paren == 0 && (tokens[j].text == QLatin1String("{") || tokens[j].text == QLatin1String(";"))) {
                break;
            }
            ++j;
        }
        if (j >= tokens.size())
            break;
        const bool hasBody = tokens[j].text == QLatin1String("{");
        if (firstParen >= 0 && currentKind(stack) == ScopeKind::Type) {
            int nameIdx = firstParen - 1;
            while (nameIdx >= i && tokens[nameIdx].text == QLatin1String("]")) {
                --nameIdx;
                if (nameIdx >= i && tokens[nameIdx].text == QLatin1String("["))
                    --nameIdx;
            }
            if (nameIdx >= i && tokens[nameIdx].type == Token::Name && !isModifierOrType(tokens[nameIdx].text)
                && !isControlKeyword(tokens[nameIdx].text)) {
                const QString fn = tokens[nameIdx].text;
                const int close = skipBalanced(tokens, firstParen, QStringLiteral("("), QStringLiteral(")")) - 1;
                const QStringList params = parseParamNames(tokens, firstParen, close);
                const QString parent = currentType(stack);
                DefinedSymbol sym;
                sym.name = fn;
                sym.parentQualified = parent;
                sym.qualifiedName = joinQualified(parent, fn);
                sym.kind = SymbolKind::Function;
                sym.parameters = params;
                sym.display = fn + QLatin1Char('(') + params.join(QLatin1String(", ")) + QLatin1Char(')');
                sym.line = tokens[nameIdx].line;
                addSymbol(parsed.node, sym);
                for (const QString &p : params) {
                    DefinedSymbol var;
                    var.name = p;
                    var.parentQualified = sym.qualifiedName;
                    var.qualifiedName = joinQualified(sym.qualifiedName, p);
                    var.kind = SymbolKind::Variable;
                    var.display = p;
                    var.line = sym.line;
                    addSymbol(parsed.node, var);
                }
                if (hasBody) {
                    pending.kind = ScopeKind::Function;
                    pending.qualified = sym.qualifiedName;
                    hasPending = true;
                    i = j;
                    continue;
                }
                i = j + 1;
                continue;
            }
        }
        if (currentKind(stack) == ScopeKind::Type && tokens[j].text == QLatin1String(";")) {
            QString name;
            for (int k = i; k < j; ++k) {
                if (tokens[k].text == QLatin1String("=") || tokens[k].text == QLatin1String("("))
                    break;
                if (tokens[k].type == Token::Name && !isModifierOrType(tokens[k].text) && !isControlKeyword(tokens[k].text))
                    name = tokens[k].text;
            }
            if (!name.isEmpty()) {
                const QString parent = currentType(stack);
                DefinedSymbol var;
                var.name = name;
                var.parentQualified = parent;
                var.qualifiedName = joinQualified(parent, name);
                var.kind = SymbolKind::Variable;
                var.display = name;
                var.line = tokens[i].line;
                addSymbol(parsed.node, var);
            }
            i = j + 1;
            continue;
        }
        i = j + (tokens[j].text == QLatin1String(";") ? 1 : 0);
        if (hasBody)
            i = j;
    }
    return parsed;
}

QString typeKey(const QString &packageName, const QString &className)
{
    if (packageName.isEmpty())
        return className;
    return packageName + QLatin1Char('.') + className;
}

FileNode *findTypeFile(const QString &spec, const QMap<QString, FileNode *> &byType,
                       const QMap<QString, FileNode *> &bySimple)
{
    if (spec.isEmpty())
        return nullptr;
    if (FileNode *n = byType.value(spec, nullptr))
        return n;
    const int dot = spec.lastIndexOf('.');
    const QString simple = (dot >= 0) ? spec.mid(dot + 1) : spec;
    return bySimple.value(simple, nullptr);
}

void resolveImports(ParsedFile &consumer, const QMap<QString, FileNode *> &byType,
                    const QMap<QString, FileNode *> &bySimple, const QMap<QString, QVector<FileNode *>> &byPackage)
{
    for (ImportBinding &b : consumer.imports) {
        if (b.wildcard)
            continue;
        FileNode *n = findTypeFile(b.spec, byType, bySimple);
        if (n)
            b.resolvedPath = n->path;
    }
    Q_UNUSED(byPackage);
}

bool parentIsFunction(const FileNode &file, const DefinedSymbol &sym)
{
    if (sym.parentQualified.isEmpty())
        return false;
    const DefinedSymbol *p = findByQualified(file, sym.parentQualified);
    return p && p->kind == SymbolKind::Function;
}

void analyzeUsages(const ParsedFile &consumer, const QMap<QString, FileNode *> &byPath,
                   const QMap<QString, FileNode *> &byType, const QMap<QString, FileNode *> &bySimple,
                   const QMap<QString, QVector<FileNode *>> &byPackage,
                   QMap<QString, QVector<DefinedSymbol>> &usedByProvider)
{
    QVector<FileNode *> providers;
    QSet<QString> seen;
    auto addProvider = [&](FileNode *p) {
        if (!p || p->path == consumer.node.path || seen.contains(p->path))
            return;
        seen.insert(p->path);
        providers.push_back(p);
    };
    for (FileNode *p : byPackage.value(consumer.packageName))
        addProvider(p);
    for (const ImportBinding &b : consumer.imports) {
        if (b.wildcard) {
            for (FileNode *p : byPackage.value(b.spec))
                addProvider(p);
            continue;
        }
        FileNode *p = byPath.value(b.resolvedPath, nullptr);
        if (!p)
            p = findTypeFile(b.spec, byType, bySimple);
        addProvider(p);
    }
    if (providers.isEmpty())
        return;

    QMap<QString, QVector<QPair<FileNode *, const DefinedSymbol *>>> topLevel;
    QMap<QString, QVector<QPair<FileNode *, const DefinedSymbol *>>> classes;
    for (FileNode *p : providers) {
        for (const DefinedSymbol &s : p->symbols) {
            if (parentIsFunction(*p, s))
                continue;
            if (s.kind == SymbolKind::Class) {
                classes[s.name].push_back(qMakePair(p, &s));
                topLevel[s.name].push_back(qMakePair(p, &s));
            } else if (s.parentQualified.isEmpty()) {
                topLevel[s.name].push_back(qMakePair(p, &s));
            }
        }
    }
    for (const ImportBinding &b : consumer.imports) {
        if (!b.isStatic || b.symbol.isEmpty())
            continue;
        FileNode *p = byPath.value(b.resolvedPath, nullptr);
        if (!p)
            continue;
        if (const DefinedSymbol *sym = findByNameParent(*p, b.symbol, b.simpleName))
            topLevel[b.symbol].push_back(qMakePair(p, sym));
        else if (const DefinedSymbol *sym = findByNameParent(*p, b.symbol, QString()))
            topLevel[b.symbol].push_back(qMakePair(p, sym));
    }

    const QVector<Token> tokens = tokenize(readFile(consumer.node.path));
    QMap<QString, LocalType> locals;

    auto markTop = [&](const QString &name, int line) {
        for (const auto &hit : topLevel.value(name))
            addUsedWithAncestors(usedByProvider[hit.first->path], *hit.first, *hit.second, line);
    };
    auto markMember = [&](FileNode *provider, const QString &className, const QString &attr, int line) {
        if (!provider)
            return;
        if (const DefinedSymbol *cls = findByNameParent(*provider, className.contains('.') ? className.section('.', -1)
                                                                                           : className,
                                                        className.contains('.') ? className.section('.', 0, -2)
                                                                                : QString()))
            addUsedWithAncestors(usedByProvider[provider->path], *provider, *cls, 0);
        else if (const DefinedSymbol *clsQ = findByQualified(*provider, className))
            addUsedWithAncestors(usedByProvider[provider->path], *provider, *clsQ, 0);
        if (const DefinedSymbol *mem = findByNameParent(*provider, attr, className))
            addUsedWithAncestors(usedByProvider[provider->path], *provider, *mem, line);
        else {
            for (const DefinedSymbol &s : provider->symbols) {
                if (s.name == attr && (s.parentQualified == className || s.parentQualified.endsWith(QLatin1Char('.') + className))) {
                    addUsedWithAncestors(usedByProvider[provider->path], *provider, s, line);
                    break;
                }
            }
        }
    };

    for (int i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name || !classes.contains(tokens[i].text))
            continue;
        int j = i + 1;
        while (j < tokens.size() && (tokens[j].text == QLatin1String("[") || tokens[j].text == QLatin1String("]")))
            ++j;
        if (j < tokens.size() && tokens[j].type == Token::Name) {
            const QString var = tokens[j].text;
            if (!isModifierOrType(var) && !isControlKeyword(var)) {
                const auto hits = classes.value(tokens[i].text);
                if (!hits.isEmpty())
                    locals.insert(var, LocalType{hits.first().second->qualifiedName, hits.first().first->path});
            }
        }
    }

    for (int i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name || tokens[i + 1].text != QLatin1String(".") || tokens[i + 2].type != Token::Name)
            continue;
        const QString base = tokens[i].text;
        const QString attr = tokens[i + 2].text;
        if (classes.contains(base)) {
            for (const auto &hit : classes.value(base))
                markMember(hit.first, hit.second->qualifiedName, attr, tokens[i].line);
        } else if (locals.contains(base)) {
            const LocalType lt = locals.value(base);
            markMember(byPath.value(lt.providerPath, nullptr), lt.className, attr, tokens[i].line);
        }
    }

    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name)
            continue;
        if (isControlKeyword(tokens[i].text) || isModifierOrType(tokens[i].text))
            continue;
        if (i + 1 < tokens.size() && tokens[i + 1].text == QLatin1String("."))
            continue;
        if (i >= 2 && tokens[i - 1].text == QLatin1String("."))
            continue;
        if (topLevel.contains(tokens[i].text))
            markTop(tokens[i].text, tokens[i].line);
    }
}

} // namespace

AnalysisResult JavaAnalyzer::analyzeDirectory(const QString &rootDir)
{
    AnalysisResult result;
    QVector<ParsedFile> parsed;
    for (const QString &path : AnalysisUtil::scanFiles(rootDir)) {
        if (!AnalysisUtil::isJavaFile(path))
            continue;
        parsed.push_back(parseJavaFile(path));
    }

    QMap<QString, FileNode *> byPath;
    QMap<QString, FileNode *> byType;
    QMap<QString, FileNode *> bySimple;
    QMap<QString, QVector<FileNode *>> byPackage;
    for (ParsedFile &p : parsed) {
        byPath.insert(p.node.path, &p.node);
        byPackage[p.packageName].push_back(&p.node);
        for (const DefinedSymbol &s : p.node.symbols) {
            if (s.kind != SymbolKind::Class || !s.parentQualified.isEmpty())
                continue;
            byType.insert(typeKey(p.packageName, s.name), &p.node);
            bySimple.insert(s.name, &p.node);
        }
        if (!p.node.moduleName.isEmpty())
            bySimple.insert(p.node.moduleName, &p.node);
    }
    for (ParsedFile &p : parsed)
        resolveImports(p, byType, bySimple, byPackage);

    QMap<QPair<QString, QString>, QVector<DefinedSymbol>> edgeUsed;
    for (const ParsedFile &consumer : parsed) {
        QMap<QString, QVector<DefinedSymbol>> usedByProvider;
        analyzeUsages(consumer, byPath, byType, bySimple, byPackage, usedByProvider);
        for (auto it = usedByProvider.begin(); it != usedByProvider.end(); ++it) {
            if (it.value().isEmpty() || it.key() == consumer.node.path)
                continue;
            edgeUsed[qMakePair(it.key(), consumer.node.path)] = it.value();
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
    AnalysisUtil::finalize(result);
    return result;
}
