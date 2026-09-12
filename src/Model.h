#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

enum class SymbolKind { Class, Function, Variable };

struct DefinedSymbol {
    QString name;
    QString qualifiedName;
    QString display;
    SymbolKind kind = SymbolKind::Function;
    QString parentQualified;
    QStringList parameters;
    int line = 0;
    QVector<int> useLines;
};

struct FileNode {
    QString path;
    QString fileName;
    QString moduleName;
    QString comment;
    QVector<DefinedSymbol> symbols;
};

struct FileRelation {
    QString fromPath;
    QString toPath;
    QString fromFileName;
    QString toFileName;
    QVector<DefinedSymbol> used;
    bool fictitious = false;
    QString comment;
};

struct AnalysisResult {
    QVector<FileNode> files;
    QVector<FileRelation> relations;
};
