#pragma once

#include "Model.h"

#include <QMainWindow>

class GraphView;
class InfoPanel;
class QAction;
class QLabel;
class QMenu;
class QShowEvent;
class QSplitter;
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
    void rebuildViewMenu();
    void fillViewMenu(QMenu *menu);
    void updateViewButton();
    QString filterTitle(const QString &key) const;
    void showEvent(QShowEvent *event) override;

    QToolBar *m_toolbar = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_viewAction = nullptr;
    QMenu *m_fileMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_toolbarViewMenu = nullptr;
    QSplitter *m_hSplitter = nullptr;
    QSplitter *m_vSplitter = nullptr;
    GraphView *m_graph = nullptr;
    InfoPanel *m_info = nullptr;
    QLabel *m_reserve = nullptr;
    QString m_lastDir;
    QString m_statusKind = QStringLiteral("hint");
    bool m_didInitialLayout = false;
};
