#include "GoAnalyzer.h"
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
    QString path;
    QString alias;
    QString dir;
};

struct ParsedFile {
    FileNode node;
    QString packageName;
    QString dir;
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
        QStringLiteral("if"),     QStringLiteral("else"),    QStringLiteral("for"),
        QStringLiteral("switch"), QStringLiteral("case"),    QStringLiteral("default"),
        QStringLiteral("select"), QStringLiteral("go"),      QStringLiteral("defer"),
        QStringLiteral("return"), QStringLiteral("break"),   QStringLiteral("continue"),
        QStringLiteral("goto"),   QStringLiteral("fallthrough"), QStringLiteral("range"),
        QStringLiteral("package"), QStringLiteral("import"), QStringLiteral("func"),
        QStringLiteral("type"),   QStringLiteral("var"),     QStringLiteral("const"),
        QStringLiteral("struct"), QStringLiteral("interface"), QStringLiteral("map"),
        QStringLiteral("chan"),   QStringLiteral("make"),    QStringLiteral("new"),
        QStringLiteral("len"),    QStringLiteral("cap"),     QStringLiteral("append"),
        QStringLiteral("copy"),   QStringLiteral("delete"),  QStringLiteral("panic"),
        QStringLiteral("recover"), QStringLiteral("nil"),    QStringLiteral("true"),
        QStringLiteral("false"),  QStringLiteral("iota")};
    return k.contains(n);
}

bool isBuiltinType(const QString &n)
{
    static const QSet<QString> k = {
        QStringLiteral("int"),     QStringLiteral("int8"),    QStringLiteral("int16"),
        QStringLiteral("int32"),   QStringLiteral("int64"),   QStringLiteral("uint"),
        QStringLiteral("uint8"),   QStringLiteral("uint16"),  QStringLiteral("uint32"),
        QStringLiteral("uint64"),  QStringLiteral("uintptr"), QStringLiteral("float32"),
        QStringLiteral("float64"), QStringLiteral("complex64"), QStringLiteral("complex128"),
        QStringLiteral("byte"),    QStringLiteral("rune"),    QStringLiteral("string"),
        QStringLiteral("bool"),    QStringLiteral("error"),   QStringLiteral("any")};
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
    bool inRaw = false;
    bool inRune = false;
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
        if (inRaw) {
            out += c;
            if (c == '`')
                inRaw = false;
            continue;
        }
        if (inString || inRune) {
            out += c;
            if (c == '\\' && i + 1 < src.size()) {
                out += src[++i];
                continue;
            }
            if ((inString && c == '"') || (inRune && c == '\'')) {
                inString = false;
                inRune = false;
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
        if (c == '`') {
            inRaw = true;
            continue;
        }
        if (c == '"' || c == '\'') {
            inString = c == '"';
            inRune = c == '\'';
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
        if (c == '"' || c == '\'' || c == '`') {
            const QChar q = c;
            const int start = i++;
            while (i < cleaned.size() && cleaned[i] != q) {
                if (q != '`' && cleaned[i] == '\\')
                    i += 2;
                else {
                    if (cleaned[i] == '\n')
                        ++line;
                    ++i;
                }
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
        if (cleaned.mid(i, 3) == QLatin1String("...")) {
            push(Token::Op, QStringLiteral("..."));
            i += 3;
            continue;
        }
        if (cleaned.mid(i, 2) == QLatin1String(":=")) {
            push(Token::Op, QStringLiteral(":="));
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

int skipGoType(const QVector<Token> &tokens, int j)
{
    while (j < tokens.size() && (tokens[j].text == QLatin1String("*") || tokens[j].text == QLatin1String("<-")))
        ++j;
    while (j < tokens.size() && tokens[j].text == QLatin1String("["))
        j = skipBalanced(tokens, j, QStringLiteral("["), QStringLiteral("]"));
    if (j >= tokens.size())
        return j;
    const QString tx = tokens[j].text;
    if (tx == QLatin1String("map")) {
        ++j;
        if (j < tokens.size() && tokens[j].text == QLatin1String("[")) {
            j = skipBalanced(tokens, j, QStringLiteral("["), QStringLiteral("]"));
            return skipGoType(tokens, j);
        }
        return j;
    }
    if (tx == QLatin1String("chan"))
        return skipGoType(tokens, j + 1);
    if (tx == QLatin1String("struct") || tx == QLatin1String("interface")) {
        ++j;
        if (j < tokens.size() && tokens[j].text == QLatin1String("{"))
            j = skipBalanced(tokens, j, QStringLiteral("{"), QStringLiteral("}"));
        return j;
    }
    if (tx == QLatin1String("func")) {
        ++j;
        if (j < tokens.size() && tokens[j].text == QLatin1String("("))
            j = skipBalanced(tokens, j, QStringLiteral("("), QStringLiteral(")"));
        return skipGoType(tokens, j);
    }
    if (tokens[j].type == Token::Name) {
        ++j;
        if (j + 1 < tokens.size() && tokens[j].text == QLatin1String(".") && tokens[j + 1].type == Token::Name)
            j += 2;
        while (j < tokens.size() && tokens[j].text == QLatin1String("["))
            j = skipBalanced(tokens, j, QStringLiteral("["), QStringLiteral("]"));
        return j;
    }
    return j;
}

QString unquote(const QString &s)
{
    if (s.size() >= 2 && ((s.startsWith('"') && s.endsWith('"')) || (s.startsWith('`') && s.endsWith('`'))))
        return s.mid(1, s.size() - 2);
    return s;
}

QString lastPathPart(const QString &path)
{
    const int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.mid(slash + 1) : path;
}

QStringList parseParamNames(const QVector<Token> &tokens, int open, int close)
{
    QStringList names;
    QVector<Token> cur;
    int depth = 0;
    auto flush = [&]() {
        QString name;
        for (const Token &t : cur) {
            if (t.type == Token::Name && !isControlKeyword(t.text) && !isBuiltinType(t.text)) {
                name = t.text;
                break;
            }
        }
        if (!name.isEmpty())
            names << name;
        cur.clear();
    };
    for (int i = open + 1; i < close && i < tokens.size(); ++i) {
        if (tokens[i].text == QLatin1String("(") || tokens[i].text == QLatin1String("[") || tokens[i].text == QLatin1String("{"))
            ++depth;
        else if (tokens[i].text == QLatin1String(")") || tokens[i].text == QLatin1String("]") || tokens[i].text == QLatin1String("}"))
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

void extractHeader(const QString &src, ParsedFile &parsed)
{
    const QVector<Token> tokens = tokenize(src);
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name)
            continue;
        if (tokens[i].text == QLatin1String("package") && i + 1 < tokens.size() && tokens[i + 1].type == Token::Name) {
            parsed.packageName = tokens[i + 1].text;
            continue;
        }
        if (tokens[i].text != QLatin1String("import"))
            continue;
        auto addImp = [&](const QString &alias, const QString &path) {
            if (path.isEmpty())
                return;
            ImportBinding b;
            b.path = path;
            b.alias = alias.isEmpty() ? lastPathPart(path) : alias;
            if (b.alias == QLatin1String(".") || b.alias == QLatin1String("_"))
                b.alias = lastPathPart(path);
            parsed.imports.push_back(b);
        };
        if (i + 1 < tokens.size() && tokens[i + 1].type == Token::String) {
            addImp(QString(), unquote(tokens[i + 1].text));
            continue;
        }
        if (i + 2 < tokens.size() && tokens[i + 1].type == Token::Name && tokens[i + 2].type == Token::String) {
            addImp(tokens[i + 1].text, unquote(tokens[i + 2].text));
            continue;
        }
        if (i + 1 < tokens.size() && tokens[i + 1].text == QLatin1String("(")) {
            int j = i + 2;
            while (j < tokens.size() && tokens[j].text != QLatin1String(")")) {
                if (tokens[j].type == Token::String) {
                    addImp(QString(), unquote(tokens[j].text));
                    ++j;
                    continue;
                }
                if (tokens[j].type == Token::Name && j + 1 < tokens.size() && tokens[j + 1].type == Token::String) {
                    addImp(tokens[j].text, unquote(tokens[j + 1].text));
                    j += 2;
                    continue;
                }
                ++j;
            }
        }
    }
}

ParsedFile parseGoFile(const QString &path)
{
    ParsedFile parsed;
    QFileInfo info(path);
    parsed.node.path = info.absoluteFilePath();
    parsed.node.fileName = info.fileName();
    parsed.node.moduleName = info.completeBaseName();
    parsed.dir = info.absolutePath();
    const QString src = readFile(path);
    extractHeader(src, parsed);

    const QVector<Token> tokens = tokenize(src);
    int braceDepth = 0;
    QString currentType;
    int typeDepth = -1;
    bool inFunc = false;
    int funcDepth = -1;

    int i = 0;
    while (i < tokens.size() && tokens[i].type != Token::End) {
        const Token &t = tokens[i];
        if (t.text == QLatin1String("{")) {
            ++braceDepth;
            ++i;
            continue;
        }
        if (t.text == QLatin1String("}")) {
            --braceDepth;
            if (inFunc && braceDepth <= funcDepth) {
                inFunc = false;
                funcDepth = -1;
            }
            if (!currentType.isEmpty() && braceDepth <= typeDepth) {
                currentType.clear();
                typeDepth = -1;
            }
            ++i;
            continue;
        }
        if (inFunc) {
            ++i;
            continue;
        }
        if (t.type != Token::Name) {
            ++i;
            continue;
        }
        if (t.text == QLatin1String("package") || t.text == QLatin1String("import")) {
            if (i + 1 < tokens.size() && tokens[i + 1].text == QLatin1String("("))
                i = skipBalanced(tokens, i + 1, QStringLiteral("("), QStringLiteral(")"));
            else
                i += 2;
            continue;
        }
        if (t.text == QLatin1String("type") && braceDepth == 0) {
            ++i;
            if (i >= tokens.size() || tokens[i].type != Token::Name)
                continue;
            const QString name = tokens[i].text;
            const int line = tokens[i].line;
            ++i;
            QString kw = QStringLiteral("type");
            if (i < tokens.size() && (tokens[i].text == QLatin1String("struct") || tokens[i].text == QLatin1String("interface")))
                kw = tokens[i].text;
            DefinedSymbol sym;
            sym.name = name;
            sym.qualifiedName = name;
            sym.kind = SymbolKind::Class;
            sym.display = kw + QLatin1Char(' ') + name;
            sym.line = line;
            addSymbol(parsed.node, sym);
            currentType = name;
            typeDepth = braceDepth;
            continue;
        }
        if (t.text == QLatin1String("func") && braceDepth == 0) {
            ++i;
            QString recv;
            if (i < tokens.size() && tokens[i].text == QLatin1String("(")) {
                const int close = skipBalanced(tokens, i, QStringLiteral("("), QStringLiteral(")")) - 1;
                for (int k = i + 1; k < close; ++k) {
                    if (tokens[k].type == Token::Name && !isBuiltinType(tokens[k].text) && !isControlKeyword(tokens[k].text))
                        recv = tokens[k].text;
                }
                i = close + 1;
            }
            if (i >= tokens.size() || tokens[i].type != Token::Name)
                continue;
            const QString name = tokens[i].text;
            const int line = tokens[i].line;
            ++i;
            QStringList params;
            if (i < tokens.size() && tokens[i].text == QLatin1String("(")) {
                const int close = skipBalanced(tokens, i, QStringLiteral("("), QStringLiteral(")")) - 1;
                params = parseParamNames(tokens, i, close);
                i = close + 1;
            }
            DefinedSymbol sym;
            sym.name = name;
            sym.parentQualified = recv;
            sym.qualifiedName = joinQualified(recv, name);
            sym.kind = SymbolKind::Function;
            sym.parameters = params;
            const QString call = name + QLatin1Char('(') + params.join(QLatin1String(", ")) + QLatin1Char(')');
            sym.display = recv.isEmpty() ? (QStringLiteral("func ") + call) : (QStringLiteral("func ") + recv + QLatin1Char('.') + call);
            sym.line = line;
            addSymbol(parsed.node, sym);
            for (const QString &p : params) {
                DefinedSymbol var;
                var.name = p;
                var.parentQualified = sym.qualifiedName;
                var.qualifiedName = joinQualified(sym.qualifiedName, p);
                var.kind = SymbolKind::Variable;
                var.display = p;
                var.line = line;
                addSymbol(parsed.node, var);
            }
            inFunc = true;
            funcDepth = braceDepth;
            continue;
        }
        if ((t.text == QLatin1String("var") || t.text == QLatin1String("const")) && braceDepth == 0) {
            const int line = t.line;
            ++i;
            auto addVar = [&](const QString &name) {
                DefinedSymbol var;
                var.name = name;
                var.qualifiedName = name;
                var.kind = SymbolKind::Variable;
                var.display = name;
                var.line = line;
                addSymbol(parsed.node, var);
            };
            if (i < tokens.size() && tokens[i].text == QLatin1String("(")) {
                int j = i + 1;
                while (j < tokens.size() && tokens[j].text != QLatin1String(")")) {
                    if (tokens[j].type == Token::Name && !isControlKeyword(tokens[j].text) && !isBuiltinType(tokens[j].text)) {
                        addVar(tokens[j].text);
                        while (j < tokens.size() && tokens[j].text != QLatin1String(")") && tokens[j].type != Token::Name)
                            ++j;
                    }
                    ++j;
                }
                i = j + (j < tokens.size() ? 1 : 0);
                continue;
            }
            if (i < tokens.size() && tokens[i].type == Token::Name)
                addVar(tokens[i].text);
            continue;
        }
        if (!currentType.isEmpty() && braceDepth == typeDepth + 1 && t.type == Token::Name && !isControlKeyword(t.text)
            && !isBuiltinType(t.text) && t.text != QLatin1String("struct") && t.text != QLatin1String("interface")) {
            QStringList names;
            names << t.text;
            int j = i + 1;
            while (j + 1 < tokens.size() && tokens[j].text == QLatin1String(",") && tokens[j + 1].type == Token::Name) {
                names << tokens[j + 1].text;
                j += 2;
            }
            for (const QString &name : names) {
                DefinedSymbol var;
                var.name = name;
                var.parentQualified = currentType;
                var.qualifiedName = joinQualified(currentType, name);
                var.kind = SymbolKind::Variable;
                var.display = name;
                var.line = t.line;
                addSymbol(parsed.node, var);
            }
            i = skipGoType(tokens, j);
            continue;
        }
        ++i;
    }
    return parsed;
}

QString findGoModModule(const QString &startDir, const QString &rootDir, QString *modDirOut)
{
    QDir dir(startDir);
    const QString root = QDir(rootDir).absolutePath();
    while (true) {
        const QString modPath = dir.filePath(QStringLiteral("go.mod"));
        if (QFileInfo::exists(modPath)) {
            if (modDirOut)
                *modDirOut = dir.absolutePath();
            const QStringList lines = readFile(modPath).split('\n');
            for (QString line : lines) {
                line = line.trimmed();
                if (line.startsWith(QLatin1String("module ")))
                    return line.mid(7).trimmed();
            }
            return {};
        }
        if (dir.absolutePath() == root)
            break;
        if (!dir.cdUp())
            break;
    }
    return {};
}

QString resolveImportDir(const QString &importPath, const QString &consumerDir, const QString &rootDir,
                         const QSet<QString> &dirs)
{
    if (importPath.startsWith(QLatin1String("./")) || importPath.startsWith(QLatin1String("../"))) {
        const QString cand = QFileInfo(QDir(consumerDir).filePath(importPath)).absoluteFilePath();
        if (dirs.contains(cand))
            return cand;
        return {};
    }
    QString modDir;
    const QString module = findGoModModule(consumerDir, rootDir, &modDir);
    if (!module.isEmpty() && (importPath == module || importPath.startsWith(module + QLatin1Char('/')))) {
        const QString rel = importPath.mid(module.size());
        const QString cand = QFileInfo(modDir + rel).absoluteFilePath();
        if (dirs.contains(cand))
            return cand;
    }
    QString best;
    int bestScore = -1;
    const QString needle = importPath;
    for (const QString &dir : dirs) {
        const QString norm = QDir::fromNativeSeparators(dir);
        if (norm.endsWith(QLatin1Char('/') + needle) || norm.endsWith(needle)) {
            const int score = needle.size();
            if (score > bestScore) {
                bestScore = score;
                best = dir;
            }
        } else if (QFileInfo(dir).fileName() == lastPathPart(importPath)) {
            if (1 > bestScore) {
                bestScore = 1;
                best = dir;
            }
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
                   const QMap<QString, QVector<FileNode *>> &byDir,
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
    for (FileNode *p : byDir.value(consumer.dir))
        addProvider(p);

    QMap<QString, QVector<FileNode *>> importedDirs;
    for (const ImportBinding &b : consumer.imports) {
        if (b.dir.isEmpty())
            continue;
        for (FileNode *p : byDir.value(b.dir)) {
            addProvider(p);
            importedDirs[b.alias].push_back(p);
        }
    }
    if (providers.isEmpty())
        return;

    QMap<QString, QVector<QPair<FileNode *, const DefinedSymbol *>>> exported;
    for (FileNode *p : providers) {
        const bool samePkg = (QFileInfo(p->path).absolutePath() == consumer.dir);
        for (const DefinedSymbol &s : p->symbols) {
            if (parentIsFunction(*p, s))
                continue;
            if (!samePkg && !s.name.isEmpty() && s.name[0].isLower())
                continue;
            if (s.parentQualified.isEmpty())
                exported[s.name].push_back(qMakePair(p, &s));
        }
    }

    const QVector<Token> tokens = tokenize(readFile(consumer.node.path));
    QMap<QString, QPair<QString, QString>> locals;

    for (int i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name || tokens[i + 1].text != QLatin1String(":="))
            continue;
        if (i + 4 < tokens.size() && tokens[i + 2].type == Token::Name && tokens[i + 3].text == QLatin1String(".")
            && tokens[i + 4].type == Token::Name && importedDirs.contains(tokens[i + 2].text)) {
            const QString typeName = tokens[i + 4].text;
            for (FileNode *p : importedDirs.value(tokens[i + 2].text)) {
                if (findByNameParent(*p, typeName, QString())) {
                    locals.insert(tokens[i].text, qMakePair(typeName, p->path));
                    break;
                }
            }
        }
    }

    auto mark = [&](FileNode *provider, const DefinedSymbol &sym, int line) {
        addUsedWithAncestors(usedByProvider[provider->path], *provider, sym, line);
    };

    for (int i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name || tokens[i + 1].text != QLatin1String(".") || tokens[i + 2].type != Token::Name)
            continue;
        const QString base = tokens[i].text;
        const QString attr = tokens[i + 2].text;
        if (importedDirs.contains(base)) {
            for (FileNode *p : importedDirs.value(base)) {
                if (const DefinedSymbol *sym = findByNameParent(*p, attr, QString()))
                    mark(p, *sym, tokens[i].line);
                else {
                    for (const DefinedSymbol &s : p->symbols) {
                        if (s.name == attr && !parentIsFunction(*p, s)) {
                            mark(p, s, tokens[i].line);
                            break;
                        }
                    }
                }
            }
            continue;
        }
        if (locals.contains(base)) {
            const auto lt = locals.value(base);
            FileNode *p = byPath.value(lt.second, nullptr);
            if (p) {
                if (const DefinedSymbol *cls = findByNameParent(*p, lt.first, QString()))
                    mark(p, *cls, 0);
                if (const DefinedSymbol *mem = findByNameParent(*p, attr, lt.first))
                    mark(p, *mem, tokens[i].line);
            }
            continue;
        }
        if (exported.contains(base)) {
            for (const auto &hit : exported.value(base)) {
                mark(hit.first, *hit.second, 0);
                if (const DefinedSymbol *mem = findByNameParent(*hit.first, attr, hit.second->qualifiedName))
                    mark(hit.first, *mem, tokens[i].line);
            }
        }
    }

    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type != Token::Name)
            continue;
        if (isControlKeyword(tokens[i].text) || isBuiltinType(tokens[i].text))
            continue;
        if (i + 1 < tokens.size() && tokens[i + 1].text == QLatin1String("."))
            continue;
        if (i >= 2 && tokens[i - 1].text == QLatin1String("."))
            continue;
        if (importedDirs.contains(tokens[i].text))
            continue;
        for (const auto &hit : exported.value(tokens[i].text)) {
            if (QFileInfo(hit.first->path).absolutePath() == consumer.dir)
                mark(hit.first, *hit.second, tokens[i].line);
        }
    }
}

} // namespace

AnalysisResult GoAnalyzer::analyzeDirectory(const QString &rootDir)
{
    AnalysisResult result;
    QVector<ParsedFile> parsed;
    for (const QString &path : AnalysisUtil::scanFiles(rootDir)) {
        if (!AnalysisUtil::isGoFile(path))
            continue;
        parsed.push_back(parseGoFile(path));
    }

    QSet<QString> dirs;
    QMap<QString, FileNode *> byPath;
    QMap<QString, QVector<FileNode *>> byDir;
    for (ParsedFile &p : parsed) {
        dirs.insert(p.dir);
        byPath.insert(p.node.path, &p.node);
        byDir[p.dir].push_back(&p.node);
    }
    for (ParsedFile &p : parsed) {
        for (ImportBinding &b : p.imports)
            b.dir = resolveImportDir(b.path, p.dir, rootDir, dirs);
    }

    QMap<QPair<QString, QString>, QVector<DefinedSymbol>> edgeUsed;
    for (const ParsedFile &consumer : parsed) {
        QMap<QString, QVector<DefinedSymbol>> usedByProvider;
        analyzeUsages(consumer, byPath, byDir, usedByProvider);
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
    return result;
}
