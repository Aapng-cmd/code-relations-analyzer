#include "MainWindow.h"

#include "AnalysisUtil.h"
#include "AppConfig.h"
#include "GraphView.h"
#include "I18n.h"
#include "InfoPanel.h"
#include "ProjectAnalyzer.h"
#include "SettingsDialog.h"
#include "Theme.h"

#include "UiConfig.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    const UiConfig &u = UiConfig::get();
    resize(u.windowWidth, u.windowHeight);

    m_toolbar = addToolBar(QString());
    m_toolbar->setMovable(false);
    m_openAction = m_toolbar->addAction(QString(), this, &MainWindow::openDirectory);
    m_settingsAction = m_toolbar->addAction(QString(), this, &MainWindow::openSettings);
    m_viewAction = m_toolbar->addAction(QString());
    m_toolbarViewMenu = new QMenu(this);
    m_viewAction->setMenu(m_toolbarViewMenu);
    if (auto *btn = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(m_viewAction)))
        btn->setPopupMode(QToolButton::InstantPopup);

    m_fileMenu = menuBar()->addMenu(QString());
    m_fileMenu->addAction(m_openAction);
    m_fileMenu->addAction(m_settingsAction);
    m_viewMenu = menuBar()->addMenu(QString());

    m_graph = new GraphView;
    m_info = new InfoPanel;
    m_reserve = new QLabel;
    m_reserve->setAlignment(Qt::AlignCenter);
    m_reserve->setMinimumHeight(UiConfig::get().reserveHeight);

    auto *left = new QSplitter(Qt::Vertical);
    left->addWidget(m_graph);
    left->addWidget(m_reserve);
    left->setStretchFactor(0, 4);
    left->setStretchFactor(1, 1);
    left->setChildrenCollapsible(false);

    auto *root = new QSplitter(Qt::Horizontal);
    root->addWidget(left);
    root->addWidget(m_info);
    root->setStretchFactor(0, 5);
    root->setStretchFactor(1, 2);
    root->setChildrenCollapsible(false);
    setCentralWidget(root);

    connect(m_graph, &GraphView::relationSelected, m_info, &InfoPanel::showRelation);
    connect(m_graph, &GraphView::fileSelected, m_info, &InfoPanel::showFile);
    connect(m_graph, &GraphView::selectionCleared, m_info, &InfoPanel::clearInfo);
    connect(m_graph, &GraphView::statusMessage, this, [this](const QString &text) {
        if (!text.isEmpty())
            statusBar()->showMessage(text);
    });
    connect(m_graph, &GraphView::viewOptionsChanged, this, &MainWindow::rebuildViewMenu);
    connect(&AppConfig::instance(), &AppConfig::themeChanged, this, &MainWindow::applyTheme);
    connect(&AppConfig::instance(), &AppConfig::languageChanged, this, &MainWindow::retranslate);

    applyTheme();
    retranslate();
}

void MainWindow::openDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(this, I18n::t(QStringLiteral("open_directory")));
    if (dir.isEmpty())
        return;
    loadDirectory(dir);
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::loadDirectory(const QString &dir)
{
    m_lastDir = dir;
    applyAnalysis(ProjectAnalyzer::analyzeDirectory(dir));
    m_statusKind = QStringLiteral("scanned");
    retranslate();
}

void MainWindow::applyAnalysis(const AnalysisResult &result)
{
    m_graph->setAnalysis(result, m_lastDir);
    m_info->clearInfo();
    rebuildViewMenu();
}

void MainWindow::rebuildViewMenu()
{
    fillViewMenu(m_viewMenu);
    fillViewMenu(m_toolbarViewMenu);
    const bool hasProject = !m_lastDir.isEmpty();
    if (m_viewMenu)
        m_viewMenu->menuAction()->setVisible(hasProject);
    if (m_viewAction)
        m_viewAction->setEnabled(hasProject);
}

void MainWindow::fillViewMenu(QMenu *menu)
{
    if (!menu || !m_graph)
        return;
    menu->clear();
    const QVector<SourceLanguage> langs = m_graph->presentLanguages();
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    auto addLang = [&](const QString &key, const QString &title) {
        QAction *action = menu->addAction(title);
        action->setCheckable(true);
        action->setData(key);
        action->setChecked(m_graph->languageFilter() == key);
        group->addAction(action);
    };
    addLang(QStringLiteral("all"), I18n::t(QStringLiteral("lang_all")));
    for (SourceLanguage language : langs)
            addLang(AnalysisUtil::languageKey(language), I18n::languageName(language));
    connect(group, &QActionGroup::triggered, this, [this](QAction *action) {
        if (action)
            m_graph->setLanguageFilter(action->data().toString());
    });
    menu->addSeparator();
    menu->addAction(I18n::t(QStringLiteral("view_files")), m_graph, &GraphView::chooseVisibleFiles);
}

void MainWindow::applyTheme()
{
    Theme::applyTo(this);
    m_graph->applyTheme();
    m_info->applyTheme();
    const ThemeColors c = Theme::colors();
    m_reserve->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border-top: 1px solid %3; }")
                                 .arg(c.button.name(), c.muted.name(), c.border.name()));
}

void MainWindow::retranslate()
{
    setWindowTitle(I18n::t(QStringLiteral("app_title")));
    m_toolbar->setWindowTitle(I18n::t(QStringLiteral("toolbar_file")));
    m_openAction->setText(I18n::t(QStringLiteral("open_directory")));
    m_settingsAction->setText(I18n::t(QStringLiteral("settings")));
    if (m_fileMenu)
        m_fileMenu->setTitle(I18n::t(QStringLiteral("toolbar_file")));
    if (m_viewMenu)
        m_viewMenu->setTitle(I18n::t(QStringLiteral("view_menu")));
    if (m_viewAction)
        m_viewAction->setText(I18n::t(QStringLiteral("view_menu")));
    rebuildViewMenu();
    m_reserve->setText(I18n::t(QStringLiteral("reserved")));
    m_info->retranslate();
    m_graph->viewport()->update();
    if (m_statusKind == QLatin1String("scanned") && !m_lastDir.isEmpty())
        statusBar()->showMessage(I18n::t(QStringLiteral("status_scanned")).arg(m_lastDir));
    else
        statusBar()->showMessage(I18n::t(QStringLiteral("status_open_hint")));
}
