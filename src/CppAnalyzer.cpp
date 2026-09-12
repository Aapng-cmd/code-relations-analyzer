#include "CppAnalyzer.h"
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

struct IncludeBinding {
    QString spec;
    QString resolvedPath;
};

struct LocalType {
    QString className;
    QString providerPath;
};

enum class ScopeKind { File, Namespace, Class, Function, Enum, Block };

struct Scope {
    ScopeKind kind = ScopeKind::File;
    QString name;
    QString qualified;
    int depth = -1;
};

struct ParsedFile {
    FileNode node;
    QVector<IncludeBinding> includes;
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
    static const QSet<QString> k = {QStringLiteral("if"),
                                    QStringLiteral("else"),
                                    QStringLiteral("for"),
                                    QStringLiteral("while"),
                                    QStringLiteral("do"),
                                    QStringLiteral("switch"),
                                    QStringLiteral("case"),
                                    QStringLiteral("default"),
                                    QStringLiteral("catch"),
                                    QStringLiteral("try"),
                                    QStringLiteral("return"),
                                    QStringLiteral("goto"),
                                    QStringLiteral("sizeof"),
                                    QStringLiteral("typeof"),
                                    QStringLiteral("decltype"),
                                    QStringLiteral("alignof"),
                                    QStringLiteral("typeid"),
                                    QStringLiteral("static_assert"),
                                    QStringLiteral("co_await"),
                                    QStringLiteral("co_return"),
                                    QStringLiteral("co_yield"),
                                    QStringLiteral("new"),
                                    QStringLiteral("delete"),
                                    QStringLiteral("throw"),
                                    QStringLiteral("using"),
                                    QStringLiteral("typedef"),
                                    QStringLiteral("namespace"),
                                    QStringLiteral("class"),
                                    QStringLiteral("struct"),
                                    QStringLiteral("union"),
                                    QStringLiteral("enum"),
                                    QStringLiteral("template"),
                                    QStringLiteral("typename"),
                                    QStringLiteral("public"),
                                    QStringLiteral("private"),
                                    QStringLiteral("protected"),
                                    QStringLiteral("concept"),
                                    QStringLiteral("requires")};
    return k.contains(n);
}

bool isSpecOrTypeKeyword(const QString &n)
{
    static const QSet<QString> k = {
        QStringLiteral("int"),       QStringLiteral("char"),     QStringLiteral("void"),
        QStringLiteral("bool"),      QStringLiteral("float"),    QStringLiteral("double"),
        QStringLiteral("long"),      QStringLiteral("short"),    QStringLiteral("signed"),
        QStringLiteral("unsigned"),  QStringLiteral("const"),    QStringLiteral("volatile"),
        QStringLiteral("static"),    QStringLiteral("extern"),   QStringLiteral("inline"),
        QStringLiteral("virtual"),   QStringLiteral("constexpr"), QStringLiteral("consteval"),
        QStringLiteral("constinit"), QStringLiteral("mutable"),  QStringLiteral("register"),
        QStringLiteral("restrict"),  QStringLiteral("explicit"), QStringLiteral("friend"),
        QStringLiteral("auto"),      QStringLiteral("wchar_t"),  QStringLiteral("char8_t"),
        QStringLiteral("char16_t"),  QStringLiteral("char32_t"), QStringLiteral("size_t"),
        QStringLiteral("ssize_t"),   QStringLiteral("ptrdiff_t"), QStringLiteral("nullptr_t"),
        QStringLiteral("struct"),    QStringLiteral("class"),    QStringLiteral("enum"),
        QStringLiteral("union"),     QStringLiteral("typename"), QStringLiteral("template"),
        QStringLiteral("override"),  QStringLiteral("final"),    QStringLiteral("noexcept"),
        QStringLiteral("thread_local"), QStringLiteral("alignas"), QStringLiteral("static_assert")};
    return k.contains(n);
}

bool isReservedName(const QString &n)
{
    static const QSet<QString> k = {QStringLiteral("true"),
                                    QStringLiteral("false"),
                                    QStringLiteral("nullptr"),
                                    QStringLiteral("NULL"),
                                    QStringLiteral("this"),
                                    QStringLiteral("continue"),
                                    QStringLiteral("break"),
                                    QStringLiteral("default")};
    return k.contains(n);
}

void addSymbol(FileNode &node, const DefinedSymbol &sym)
{
    if (sym.name.isEmpty() || isReservedName(sym.name))
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

QString joinQualified(const QString &parent, const QString &name)
{
    if (parent.isEmpty())
        return name;
    return parent + QLatin1Char('.') + name;
}

QString stripCommentsAndPreprocessor(const QString &src)
{
    QString out;
    out.reserve(src.size());
    bool inLine = false;
    bool inBlock = false;
    bool inString = false;
    bool inChar = false;
    QChar strQ;
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
        if (inString || inChar) {
            out += ' ';
            if (c == '\\' && i + 1 < src.size()) {
                ++i;
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
        if (c == '"' || c == '\'') {
            inString = c == '"';
            inChar = c == '\'';
            strQ = c;
            out += c;
            continue;
        }
        if (c == '#') {
            int j = out.size() - 1;
            while (j >= 0 && (out[j] == ' ' || out[j] == '\t'))
                --j;
            if (j < 0 || out[j] == '\n') {
                while (i < src.size() && src[i] != '\n') {
                    if (src[i] == '\\' && i + 1 < src.size() && src[i + 1] == '\n') {
                        out += '\n';
                        i += 2;
                        continue;
                    }
                    ++i;
                }
                if (i < src.size())
                    out += '\n';
                continue;
            }
        }
        out += c;
    }
    return out;
}

QString stripCommentsKeepQuotes(const QString &src)
{
    QString out;
    out.reserve(src.size());
    bool inLine = false;
    bool inBlock = false;
    bool inString = false;
    bool inChar = false;
    QChar strQ;
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

QVector<IncludeBinding> extractIncludes(const QString &src)
{
    QVector<IncludeBinding> out;
    const QStringList lines = stripCommentsKeepQuotes(src).split('\n');
    for (QString raw : lines) {
        QString t = raw.trimmed();
        if (!t.startsWith(QLatin1Char('#')))
            continue;
        t = t.mid(1).trimmed();
        if (!t.startsWith(QLatin1String("include")))
            continue;
        t = t.mid(7).trimmed();
        if (t.size() < 3)
            continue;
        const QChar open = t[0];
        if (open != '"' && open != '<')
            continue;
        const QChar close = (open == '"' ? QChar('"') : QChar('>'));
        const int end = t.indexOf(close, 1);
        if (end <= 1)
            continue;
        IncludeBinding b;
        b.spec = t.mid(1, end - 1).trimmed();
        if (!b.spec.isEmpty())
            out.push_back(b);
    }
    return out;
}

QVector<Token> tokenize(const QString &src)
{
    QVector<Token> tokens;
    const QString cleaned = stripCommentsAndPreprocessor(src);
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
            while (i < cleaned.size()) {
                if (cleaned[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (cleaned[i] == c) {
                    ++i;
                    break;
                }
                if (cleaned[i] == '\n')
                    break;
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
            while (i < cleaned.size() && (cleaned[i].isLetterOrNumber() || cleaned[i] == '.' || cleaned[i] == '\''))
                ++i;
            push(Token::Number, cleaned.mid(start, i - start));
            continue;
        }
        static const char *ops[] = {"...", "<=>", "<<", ">>", "::", "->", "&&", "||", "++", "--", "==", "!=", "<=", ">=",
                                    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", nullptr};
        bool multi = false;
        for (int oi = 0; ops[oi]; ++oi) {
            const int n = int(qstrlen(ops[oi]));
            if (cleaned.mid(i, n) == QLatin1String(ops[oi])) {
                push(Token::Op, QString::fromLatin1(ops[oi]));
                i += n;
                multi = true;
                break;
            }
        }
        if (multi)
            continue;
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
        } else if (open == QLatin1String("<") && tokens[i].text == QLatin1String(">>")) {
            depth -= 2;
            if (depth <= 0)
                return i + 1;
        }
    }
    return i;
}

int skipParens(const QVector<Token> &tokens, int i)
{
    return skipBalanced(tokens, i, QStringLiteral("("), QStringLiteral(")"));
}

QString currentParent(const QVector<Scope> &stack)
{
    for (int i = stack.size() - 1; i >= 0; --i) {
        if (stack[i].kind == ScopeKind::Class || stack[i].kind == ScopeKind::Namespace)
            return stack[i].qualified;
    }
    return {};
}

ScopeKind currentKind(const QVector<Scope> &stack)
{
    return stack.isEmpty() ? ScopeKind::File : stack.back().kind;
}

QStringList parseParamNames(const QVector<Token> &tokens, int open, int close)
{
    QStringList names;
    QVector<Token> cur;
    int depth = 0;
    bool skipDefault = false;
    auto flush = [&]() {
        QString name;
        for (int k = cur.size() - 1; k >= 0; --k) {
            if (cur[k].type != Token::Name)
                continue;
            if (isSpecOrTypeKeyword(cur[k].text) || isControlKeyword(cur[k].text) || isReservedName(cur[k].text))
                continue;
            name = cur[k].text;
            break;
        }
        if (!name.isEmpty())
            names << name;
        cur.clear();
        skipDefault = false;
    };
    for (int i = open + 1; i < close && i < tokens.size(); ++i) {
        if (tokens[i].text == QLatin1String("(") || tokens[i].text == QLatin1String("<"))
            ++depth;
        else if (tokens[i].text == QLatin1String(")") || tokens[i].text == QLatin1String(">"))
            --depth;
        if (tokens[i].text == QLatin1String("=") && depth == 0) {
            skipDefault = true;
            continue;
        }
        if (tokens[i].text == QLatin1String(",") && depth == 0) {
            flush();
            continue;
        }
        if (!skipDefault)
            cur.push_back(tokens[i]);
    }
    if (!cur.isEmpty())
        flush();
    return names;
}

QString formatParams(const QStringList &names)
{
    return names.join(QLatin1String(", "));
}

bool looksLikeTypeParams(const QVector<Token> &tokens, int open, int close)
{
    if (close <= open + 1)
        return true;
    bool sawTypeKw = false;
    int consecutiveNames = 0;
    int maxConsecutive = 0;
    bool sawLiteral = false;
    for (int i = open + 1; i < close; ++i) {
        const Token &t = tokens[i];
        if (t.text == QLatin1String(",") || t.text == QLatin1String("::") || t.text == QLatin1String("*")
            || t.text == QLatin1String("&") || t.text == QLatin1String("&&") || t.text == QLatin1String("...")) {
            consecutiveNames = 0;
            if (t.text == QLatin1String("*") || t.text == QLatin1String("&") || t.text == QLatin1String("&&")
                || t.text == QLatin1String("..."))
                sawTypeKw = true;
            continue;
        }
        if (t.type == Token::Number || t.type == Token::String) {
            sawLiteral = true;
            consecutiveNames = 0;
            continue;
        }
        if (t.type == Token::Name) {
            if (isSpecOrTypeKeyword(t.text)) {
                sawTypeKw = true;
                consecutiveNames = 0;
            } else {
                ++consecutiveNames;
                maxConsecutive = qMax(maxConsecutive, consecutiveNames);
            }
            continue;
        }
        consecutiveNames = 0;
    }
    if (sawTypeKw || maxConsecutive >= 2)
        return true;
    if (sawLiteral)
        return false;
    return true;
}

struct FuncName {
    QString name;
    QString qualifier;
    int nameIndex = -1;
};

FuncName functionNameAtParen(const QVector<Token> &tokens, int open)
{
    FuncName fn;
    int k = open - 1;
    if (k < 0)
        return fn;
    if (tokens[k].text == QLatin1String(">")) {
        int depth = 1;
        --k;
        while (k >= 0 && depth > 0) {
            if (tokens[k].text == QLatin1String(">"))
                ++depth;
            else if (tokens[k].text == QLatin1String("<"))
                --depth;
            --k;
        }
    }
    if (k < 0)
        return fn;
    QString opSuffix;
    while (k >= 0 && tokens[k].type == Token::Op && tokens[k].text != QLatin1String("::")
           && tokens[k].text != QLatin1String("~")) {
        opSuffix.prepend(tokens[k].text);
        --k;
    }
    if (k >= 0 && tokens[k].type == Token::Name && tokens[k].text == QLatin1String("operator")) {
        fn.name = QStringLiteral("operator") + opSuffix;
        fn.nameIndex = k;
    } else if (k >= 0 && tokens[k].type == Token::Name) {
        fn.name = tokens[k].text;
        fn.nameIndex = k;
        if (isControlKeyword(fn.name) || isSpecOrTypeKeyword(fn.name))
            return {};
    } else {
        return {};
    }
    int q = fn.nameIndex - 1;
    if (q >= 0 && tokens[q].text == QLatin1String("~")) {
        fn.name = QLatin1Char('~') + fn.name;
        --q;
    }
    QStringList parts;
    while (q >= 1 && tokens[q].text == QLatin1String("::") && tokens[q - 1].type == Token::Name) {
        parts.push_front(tokens[q - 1].text);
        q -= 2;
    }
    fn.qualifier = parts.join(QLatin1Char('.'));
    return fn;
}

QString displayKeyword(const QString &kw, const QString &qualified)
{
    QString shown = qualified;
    shown.replace(QLatin1Char('.'), QLatin1String("::"));
    return kw + QLatin1Char(' ') + shown;
}

void addParamsAsVars(FileNode &node, const QString &fnQualified, const QStringList &params, int line)
{
    for (const QString &param : params) {
        DefinedSymbol var;
        var.name = param;
        var.parentQualified = fnQualified;
        var.qualifiedName = joinQualified(fnQualified, param);
        var.kind = SymbolKind::Variable;
        var.display = param;
        var.line = line;
        addSymbol(node, var);
    }
}

void addFunctionSymbol(FileNode &node, const QVector<Scope> &stack, const FuncName &fn, const QStringList &params,
                       int line)
{
    QString parent = fn.qualifier;
    if (parent.isEmpty() && currentKind(stack) == ScopeKind::Class)
        parent = currentParent(stack);
    const bool classInFile = !parent.isEmpty() && findByNameParent(node, parent.split('.').last(),
                                                                   parent.contains('.') ? parent.section('.', 0, -2)
                                                                                        : QString());
    QString parentForTree = parent;
    if (!parent.isEmpty() && !classInFile && currentKind(stack) != ScopeKind::Class)
        parentForTree.clear();
    DefinedSymbol sym;
    sym.name = fn.name;
    sym.parentQualified = parentForTree;
    sym.qualifiedName = joinQualified(parentForTree.isEmpty() ? parent : parentForTree, fn.name);
    if (parentForTree.isEmpty() && !parent.isEmpty())
        sym.qualifiedName = joinQualified(parent, fn.name);
    if (parentForTree.isEmpty() && parent.isEmpty())
        sym.qualifiedName = fn.name;
    sym.kind = SymbolKind::Function;
    const QString call = fn.name + QLatin1Char('(') + formatParams(params) + QLatin1Char(')');
    if (parentForTree.isEmpty() && !parent.isEmpty()) {
        QString shown = parent;
        shown.replace(QLatin1Char('.'), QLatin1String("::"));
        sym.display = shown + QLatin1String("::") + call;
    } else
        sym.display = call;
    sym.parameters = params;
    sym.line = line;
    addSymbol(node, sym);
    addParamsAsVars(node, sym.qualifiedName, params, line);
}

void addVariableNames(FileNode &node, const QVector<Scope> &stack, const QVector<Token> &stmt, int line)
{
    if (currentKind(stack) == ScopeKind::Function || currentKind(stack) == ScopeKind::Block
        || currentKind(stack) == ScopeKind::Enum)
        return;
    QVector<Token> cur;
    int depth = 0;
    auto flush = [&]() {
        QString name;
        for (int k = 0; k < cur.size(); ++k) {
            if (cur[k].text == QLatin1String("=") || cur[k].text == QLatin1String("{")
                || cur[k].text == QLatin1String("["))
                break;
            if (cur[k].type == Token::Name && !isSpecOrTypeKeyword(cur[k].text) && !isControlKeyword(cur[k].text))
                name = cur[k].text;
        }
        if (name.isEmpty() || name == QLatin1String("operator"))
            return;
        const QString parent = currentKind(stack) == ScopeKind::Class ? currentParent(stack) : QString();
        DefinedSymbol var;
        var.name = name;
        var.parentQualified = parent;
        var.qualifiedName = joinQualified(parent, name);
        var.kind = SymbolKind::Variable;
        var.display = name;
        var.line = line;
        addSymbol(node, var);
        cur.clear();
    };
    for (const Token &t : stmt) {
        if (t.text == QLatin1String("(") || t.text == QLatin1String("<") || t.text == QLatin1String("{"))
            ++depth;
        else if (t.text == QLatin1String(")") || t.text == QLatin1String(">") || t.text == QLatin1String("}"))
            --depth;
        if (t.text == QLatin1String(",") && depth == 0) {
            flush();
            continue;
        }
        cur.push_back(t);
    }
    if (!cur.isEmpty())
        flush();
}

ParsedFile parseCppFile(const QString &path)
{
    ParsedFile parsed;
    QFileInfo info(path);
    parsed.node.path = info.absoluteFilePath();
    parsed.node.fileName = info.fileName();
    parsed.node.moduleName = info.completeBaseName();
    parsed.includes = extractIncludes(readFile(path));

    const QVector<Token> tokens = tokenize(readFile(path));
    QVector<Scope> stack;
    stack.push_back(Scope{});
    Scope pending;
    bool hasPending = false;
    int braceDepth = 0;

    auto pushPending = [&]() {
        pending.depth = braceDepth;
        stack.push_back(pending);
        hasPending = false;
        pending = Scope{};
    };

    int i = 0;
    while (i < tokens.size() && tokens[i].type != Token::End) {
        const Token &t = tokens[i];

        if (currentKind(stack) == ScopeKind::Enum) {
            if (t.text == QLatin1String("}")) {
                --braceDepth;
                while (stack.size() > 1 && stack.back().depth >= braceDepth)
                    stack.pop_back();
                ++i;
                continue;
            }
            if (t.type == Token::Name && !isSpecOrTypeKeyword(t.text)) {
                const QString parent = currentParent(stack);
                DefinedSymbol var;
                var.name = t.text;
                var.parentQualified = stack.back().qualified;
                var.qualifiedName = joinQualified(stack.back().qualified, t.text);
                var.kind = SymbolKind::Variable;
                var.display = t.text;
                var.line = t.line;
                addSymbol(parsed.node, var);
                ++i;
                while (i < tokens.size() && tokens[i].text != QLatin1String(",") && tokens[i].text != QLatin1String("}"))
                    ++i;
                if (i < tokens.size() && tokens[i].text == QLatin1String(","))
                    ++i;
                continue;
            }
            ++i;
            continue;
        }

        if (t.text == QLatin1String("{")) {
            if (hasPending)
                pushPending();
            else {
                Scope blk;
                blk.kind = ScopeKind::Block;
                blk.qualified = currentParent(stack);
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

        if (currentKind(stack) == ScopeKind::Function || currentKind(stack) == ScopeKind::Block) {
            ++i;
            continue;
        }

        if (t.type != Token::Name) {
            ++i;
            continue;
        }

        if (t.text == QLatin1String("template")) {
            ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String("<"))
                i = skipBalanced(tokens, i, QStringLiteral("<"), QStringLiteral(">"));
            continue;
        }
        if (t.text == QLatin1String("using") || t.text == QLatin1String("typedef")
            || t.text == QLatin1String("static_assert") || t.text == QLatin1String("concept")) {
            while (i < tokens.size() && tokens[i].text != QLatin1String(";") && tokens[i].type != Token::End)
                ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String(";"))
                ++i;
            continue;
        }
        if (t.text == QLatin1String("public") || t.text == QLatin1String("private")
            || t.text == QLatin1String("protected")) {
            ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String(":"))
                ++i;
            continue;
        }
        if (t.text == QLatin1String("namespace")) {
            ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String("inline"))
                ++i;
            QString name;
            if (i < tokens.size() && tokens[i].type == Token::Name) {
                name = tokens[i].text;
                ++i;
            }
            while (i < tokens.size() && tokens[i].text != QLatin1String("{") && tokens[i].text != QLatin1String(";"))
                ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String(";")) {
                ++i;
                continue;
            }
            pending.kind = ScopeKind::Namespace;
            pending.name = name;
            pending.qualified = joinQualified(currentParent(stack), name);
            hasPending = true;
            continue;
        }
        if (t.text == QLatin1String("class") || t.text == QLatin1String("struct") || t.text == QLatin1String("union")
            || t.text == QLatin1String("enum")) {
            const QString kw = t.text;
            ++i;
            if (kw == QLatin1String("enum") && i < tokens.size()
                && (tokens[i].text == QLatin1String("class") || tokens[i].text == QLatin1String("struct")))
                ++i;
            while (i < tokens.size() && tokens[i].text == QLatin1String("alignas")) {
                ++i;
                if (i < tokens.size() && tokens[i].text == QLatin1String("("))
                    i = skipParens(tokens, i);
            }
            QString name;
            if (i < tokens.size() && tokens[i].type == Token::Name && tokens[i].text != QLatin1String("final")) {
                name = tokens[i].text;
                ++i;
            }
            while (i < tokens.size() && tokens[i].text != QLatin1String("{") && tokens[i].text != QLatin1String(";"))
                ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String(";")) {
                ++i;
                continue;
            }
            if (name.isEmpty()) {
                hasPending = true;
                pending.kind = kw == QLatin1String("enum") ? ScopeKind::Enum : ScopeKind::Class;
                pending.qualified = currentParent(stack);
                continue;
            }
            const QString parent = currentParent(stack);
            DefinedSymbol sym;
            sym.name = name;
            sym.parentQualified = parent;
            sym.qualifiedName = joinQualified(parent, name);
            sym.kind = SymbolKind::Class;
            sym.display = displayKeyword(kw, sym.qualifiedName);
            sym.line = t.line;
            addSymbol(parsed.node, sym);
            pending.kind = kw == QLatin1String("enum") ? ScopeKind::Enum : ScopeKind::Class;
            pending.name = name;
            pending.qualified = sym.qualifiedName;
            hasPending = true;
            continue;
        }
        if (t.text == QLatin1String("Q_OBJECT") || t.text == QLatin1String("Q_GADGET")
            || t.text == QLatin1String("Q_SIGNALS") || t.text == QLatin1String("Q_SLOTS")
            || t.text == QLatin1String("signals") || t.text == QLatin1String("slots")
            || t.text == QLatin1String("emit")) {
            ++i;
            if (i < tokens.size() && tokens[i].text == QLatin1String(":"))
                ++i;
            continue;
        }
        if (t.text.startsWith(QLatin1String("Q_")) && i + 1 < tokens.size()
            && tokens[i + 1].text == QLatin1String("(")) {
            i = skipParens(tokens, i + 1);
            continue;
        }
        if (isControlKeyword(t.text)) {
            const QString kw = t.text;
            ++i;
            if (kw != QLatin1String("else") && kw != QLatin1String("do") && kw != QLatin1String("try")
                && kw != QLatin1String("default")) {
                if (i < tokens.size() && tokens[i].text == QLatin1String("("))
                    i = skipParens(tokens, i);
            }
            continue;
        }

        int j = i;
        int paren = 0;
        int firstParen = -1;
        int firstEq = -1;
        bool isFuncPtr = false;
        while (j < tokens.size() && tokens[j].type != Token::End) {
            const QString &tx = tokens[j].text;
            if (tx == QLatin1String("(")) {
                if (paren == 0 && firstParen < 0)
                    firstParen = j;
                ++paren;
            } else if (tx == QLatin1String(")")) {
                --paren;
            } else if (paren == 0 && tx == QLatin1String("=") && firstEq < 0) {
                firstEq = j;
            } else if (paren == 0 && tx == QLatin1String("{")) {
                break;
            } else if (paren == 0 && tx == QLatin1String(";")) {
                break;
            }
            if (firstParen == j && j + 1 < tokens.size()
                && (tokens[j + 1].text == QLatin1String("*") || tokens[j + 1].text == QLatin1String("&")))
                isFuncPtr = true;
            ++j;
        }
        if (j >= tokens.size())
            break;

        const bool hasBody = (j < tokens.size() && tokens[j].text == QLatin1String("{"));
        const bool functionLike = firstParen >= 0 && !isFuncPtr && (firstEq < 0 || firstParen < firstEq)
                                  && (hasBody || looksLikeTypeParams(tokens, firstParen, skipParens(tokens, firstParen) - 1));
        if (functionLike) {
            const int close = skipParens(tokens, firstParen) - 1;
            const FuncName fn = functionNameAtParen(tokens, firstParen);
            if (!fn.name.isEmpty()) {
                const QStringList params = parseParamNames(tokens, firstParen, close);
                addFunctionSymbol(parsed.node, stack, fn, params, tokens[firstParen].line);
                if (hasBody) {
                    pending.kind = ScopeKind::Function;
                    pending.name = fn.name;
                    pending.qualified = joinQualified(fn.qualifier.isEmpty() ? currentParent(stack) : fn.qualifier,
                                                      fn.name);
                    hasPending = true;
                    i = j;
                    continue;
                }
            }
            i = j + (tokens[j].text == QLatin1String(";") ? 1 : 0);
            continue;
        }

        QVector<Token> stmt;
        for (int k = i; k < j; ++k)
            stmt.push_back(tokens[k]);
        addVariableNames(parsed.node, stack, stmt, tokens[i].line);
        i = j + (j < tokens.size() && tokens[j].text == QLatin1String(";") ? 1 : 0);
        if (hasBody)
            i = j;
    }

    return parsed;
}

QString resolveInclude(const QString &consumerPath, const QString &spec, const QString &rootDir,
                       const QMap<QString, FileNode *> &byPath)
{
    if (spec.isEmpty())
        return {};
    const QString root = QDir(rootDir).absolutePath();
    QDir dir(QFileInfo(consumerPath).absolutePath());
    while (true) {
        const QString cand = QFileInfo(dir.filePath(spec)).absoluteFilePath();
        if (byPath.contains(cand))
            return cand;
        if (dir.absolutePath() == root)
            break;
        if (!dir.cdUp())
            break;
    }
    QString best;
    int bestScore = -1;
    const QString needle = QDir::fromNativeSeparators(spec);
    for (auto it = byPath.begin(); it != byPath.end(); ++it) {
        const QString path = QDir::fromNativeSeparators(it.key());
        if (!path.endsWith(QLatin1Char('/') + needle) && QFileInfo(path).fileName() != QFileInfo(spec).fileName())
            continue;
        const bool suffix = path.endsWith(QLatin1Char('/') + needle);
        const int score = suffix ? (1000 + needle.size()) : QFileInfo(spec).fileName().size();
        if (score > bestScore) {
            bestScore = score;
            best = it.key();
        }
    }
    return best;
}

bool parentIsFunction(const FileNode &file, const DefinedSymbol &sym)
{
    if (sym.parentQualified.isEmpty())
        return false;
    const DefinedSymbol *p = findByQualified(file, sym.parentQualified);
    return p && p->kind == SymbolKind::Function;
}

void analyzeUsages(const ParsedFile &consumer, const QMap<QString, FileNode *> &byPath,
                   QMap<QString, QVector<DefinedSymbol>> &usedByProvider)
{
    QVector<FileNode *> providers;
    QSet<QString> seen;
    for (const IncludeBinding &inc : consumer.includes) {
        if (inc.resolvedPath.isEmpty())
            continue;
        FileNode *p = byPath.value(inc.resolvedPath, nullptr);
        if (!p || p->path == consumer.node.path || seen.contains(p->path))
            continue;
        seen.insert(p->path);
        providers.push_back(p);
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

    const QVector<Token> tokens = tokenize(readFile(consumer.node.path));
    QMap<QString, LocalType> locals;

    auto markTop = [&](const QString &name, int line) {
        const auto hits = topLevel.value(name);
        for (const auto &hit : hits)
            addUsedWithAncestors(usedByProvider[hit.first->path], *hit.first, *hit.second, line);
    };

    auto markMember = [&](FileNode *provider, const QString &className, const QString &attr, int line) {
        if (!provider)
            return;
        if (const DefinedSymbol *cls = findByNameParent(*provider, className, QString()))
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
        if (tokens[i].type != Token::Name)
            continue;
        if (!classes.contains(tokens[i].text))
            continue;
        int j = i + 1;
        while (j < tokens.size() && (tokens[j].text == QLatin1String("*") || tokens[j].text == QLatin1String("&")
                                     || tokens[j].text == QLatin1String("&&") || tokens[j].text == QLatin1String("const")))
            ++j;
        if (j < tokens.size() && tokens[j].type == Token::Name) {
            const QString var = tokens[j].text;
            if (!isSpecOrTypeKeyword(var) && !isControlKeyword(var)) {
                const auto hits = classes.value(tokens[i].text);
                if (!hits.isEmpty())
                    locals.insert(var, LocalType{tokens[i].text, hits.first().first->path});
            }
        }
    }

    for (int i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name)
            continue;
        const QString op = tokens[i + 1].text;
        if (tokens[i + 2].type != Token::Name)
            continue;
        if (op != QLatin1String(".") && op != QLatin1String("->") && op != QLatin1String("::"))
            continue;
        const QString base = tokens[i].text;
        const QString attr = tokens[i + 2].text;
        if (classes.contains(base)) {
            for (const auto &hit : classes.value(base))
                markMember(hit.first, hit.second->qualifiedName, attr, tokens[i].line);
            continue;
        }
        if (topLevel.contains(base) && op == QLatin1String("::")) {
            markTop(attr, tokens[i + 2].line);
            continue;
        }
        if (locals.contains(base)) {
            const LocalType lt = locals.value(base);
            markMember(byPath.value(lt.providerPath, nullptr), lt.className, attr, tokens[i].line);
        }
    }

    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name)
            continue;
        if (isControlKeyword(tokens[i].text) || isSpecOrTypeKeyword(tokens[i].text))
            continue;
        if (i + 1 < tokens.size() && (tokens[i + 1].text == QLatin1String(".") || tokens[i + 1].text == QLatin1String("->")
                                      || tokens[i + 1].text == QLatin1String("::")))
            continue;
        if (i >= 2 && (tokens[i - 1].text == QLatin1String(".") || tokens[i - 1].text == QLatin1String("->")
                       || tokens[i - 1].text == QLatin1String("::")))
            continue;
        if (topLevel.contains(tokens[i].text))
            markTop(tokens[i].text, tokens[i].line);
    }
}

} // namespace

AnalysisResult CppAnalyzer::analyzeDirectory(const QString &rootDir)
{
    AnalysisResult result;
    QVector<ParsedFile> parsed;
    for (const QString &path : AnalysisUtil::scanFiles(rootDir)) {
        if (!AnalysisUtil::isCppFile(path))
            continue;
        parsed.push_back(parseCppFile(path));
    }

    QMap<QString, FileNode *> byPath;
    for (ParsedFile &p : parsed)
        byPath.insert(p.node.path, &p.node);

    for (ParsedFile &p : parsed) {
        for (IncludeBinding &inc : p.includes)
            inc.resolvedPath = resolveInclude(p.node.path, inc.spec, rootDir, byPath);
    }

    QMap<QPair<QString, QString>, QVector<DefinedSymbol>> edgeUsed;
    for (const ParsedFile &consumer : parsed) {
        QMap<QString, QVector<DefinedSymbol>> usedByProvider;
        analyzeUsages(consumer, byPath, usedByProvider);
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
