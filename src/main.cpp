#include "MainWindow.h"
#include "AppConfig.h"
#include "I18n.h"
#include "PythonAnalyzer.h"
#include "Theme.h"
#include "UiConfig.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMap>
#include <QTextStream>

#include <functional>

static void dumpSymbols(QTextStream &out, const QVector<DefinedSymbol> &symbols, const QString &indent)
{
    QMap<QString, QVector<DefinedSymbol>> children;
    for (const DefinedSymbol &s : symbols)
        children[s.parentQualified].push_back(s);

    std::function<void(const QString &, const QString &)> walk = [&](const QString &parent, const QString &pad) {
        QVector<DefinedSymbol> classes;
        QVector<DefinedSymbol> functions;
        QVector<DefinedSymbol> variables;
        for (const DefinedSymbol &s : children.value(parent)) {
            if (s.kind == SymbolKind::Class)
                classes.push_back(s);
            else if (s.kind == SymbolKind::Function)
                functions.push_back(s);
            else
                variables.push_back(s);
        }
        if (!classes.isEmpty()) {
            out << pad << "Classes\n";
            for (const DefinedSymbol &s : classes) {
                out << pad << "  " << s.display << '\n';
                walk(s.qualifiedName, pad + QStringLiteral("    "));
            }
        }
        if (!functions.isEmpty()) {
            out << pad << "Functions\n";
            for (const DefinedSymbol &s : functions) {
                out << pad << "  " << s.display << '\n';
                walk(s.qualifiedName, pad + QStringLiteral("    "));
            }
        }
        if (!variables.isEmpty()) {
            out << pad << "Variables\n";
            for (const DefinedSymbol &s : variables)
                out << pad << "  " << s.display << '\n';
        }
    };
    walk(QString(), indent);
}

static int dumpAnalysis(const QString &dir)
{
    const AnalysisResult result = PythonAnalyzer::analyzeDirectory(dir);
    QTextStream out(stdout);
    out << "=== files ===\n";
    for (const FileNode &file : result.files) {
        out << file.fileName << '\n';
        dumpSymbols(out, file.symbols, QStringLiteral("  "));
    }
    out << "=== arrows ===\n";
    for (const FileRelation &rel : result.relations) {
        out << rel.fromFileName << " -> " << rel.toFileName << '\n';
        dumpSymbols(out, rel.used, QStringLiteral("  "));
    }
    out.flush();
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--dump")) {
        QCoreApplication app(argc, argv);
        UiConfig::load();
        return dumpAnalysis(QString::fromLocal8Bit(argv[2]));
    }

    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));
    UiConfig::load();
    AppConfig::instance().load();
    Theme::setCurrent(AppConfig::instance().theme());
    I18n::load(AppConfig::instance().language());
    MainWindow window;
    window.show();
    if (argc >= 2) {
        const QString path = QString::fromLocal8Bit(argv[1]);
        if (QFileInfo(path).isDir())
            window.loadDirectory(path);
    }
    return app.exec();
}
