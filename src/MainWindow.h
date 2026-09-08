#pragma once

#include "Model.h"

#include <QMainWindow>

class GraphView;
class InfoPanel;
class QAction;
class QLabel;
class QToolBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void loadDirectory(const QString &dir);

private slots:
    void openDirectory();
    void openSettings();
    void applyTheme();
    void retranslate();

private:
    void applyAnalysis(const AnalysisResult &result);

    QToolBar *m_toolbar = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_settingsAction = nullptr;
    GraphView *m_graph = nullptr;
    InfoPanel *m_info = nullptr;
    QLabel *m_reserve = nullptr;
    QString m_lastDir;
    QString m_statusKind = QStringLiteral("hint");
};
