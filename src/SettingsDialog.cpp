#include "SettingsDialog.h"

#include "AppConfig.h"
#include "I18n.h"
#include "Theme.h"

#include "UiConfig.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setModal(true);
    setMinimumWidth(UiConfig::get().settingsMinWidth);
    buildUi();
    retranslate();
    syncFromConfig();
}

void SettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_themeBox = new QGroupBox;
    auto *themeLayout = new QVBoxLayout(m_themeBox);
    m_light = new QRadioButton;
    m_dark = new QRadioButton;
    themeLayout->addWidget(m_light);
    themeLayout->addWidget(m_dark);

    m_langBox = new QGroupBox;
    auto *langLayout = new QVBoxLayout(m_langBox);
    m_en = new QRadioButton;
    m_ru = new QRadioButton;
    langLayout->addWidget(m_en);
    langLayout->addWidget(m_ru);

    m_close = new QPushButton;
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_close);

    root->addWidget(m_themeBox);
    root->addWidget(m_langBox);
    root->addLayout(buttons);

    connect(m_light, &QRadioButton::toggled, this, [this](bool on) {
        if (!on)
            return;
        Theme::setCurrent(AppTheme::Light);
        AppConfig::instance().setTheme(AppTheme::Light);
    });
    connect(m_dark, &QRadioButton::toggled, this, [this](bool on) {
        if (!on)
            return;
        Theme::setCurrent(AppTheme::Dark);
        AppConfig::instance().setTheme(AppTheme::Dark);
    });
    connect(m_en, &QRadioButton::toggled, this, [this](bool on) {
        if (!on)
            return;
        I18n::load(QStringLiteral("en"));
        AppConfig::instance().setLanguage(QStringLiteral("en"));
        retranslate();
    });
    connect(m_ru, &QRadioButton::toggled, this, [this](bool on) {
        if (!on)
            return;
        I18n::load(QStringLiteral("ru"));
        AppConfig::instance().setLanguage(QStringLiteral("ru"));
        retranslate();
    });
    connect(m_close, &QPushButton::clicked, this, &QDialog::accept);
}

void SettingsDialog::syncFromConfig()
{
    const bool dark = Theme::current() == AppTheme::Dark;
    m_dark->setChecked(dark);
    m_light->setChecked(!dark);
    const bool ru = I18n::language() == QLatin1String("ru");
    m_ru->setChecked(ru);
    m_en->setChecked(!ru);
}

void SettingsDialog::retranslate()
{
    setWindowTitle(I18n::t(QStringLiteral("settings_title")));
    m_themeBox->setTitle(I18n::t(QStringLiteral("theme")));
    m_light->setText(I18n::t(QStringLiteral("theme_light")));
    m_dark->setText(I18n::t(QStringLiteral("theme_dark")));
    m_langBox->setTitle(I18n::t(QStringLiteral("language")));
    m_en->setText(I18n::t(QStringLiteral("language_en")));
    m_ru->setText(I18n::t(QStringLiteral("language_ru")));
    m_close->setText(I18n::t(QStringLiteral("close")));
}
