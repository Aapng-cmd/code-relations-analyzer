#pragma once

#include "Theme.h"

#include <QObject>
#include <QString>

class AppConfig : public QObject {
    Q_OBJECT
public:
    static AppConfig &instance();

    static QString configDir();
    static QString configFilePath();

    void load();
    void save() const;

    AppTheme theme() const { return m_theme; }
    QString language() const { return m_language; }

    void setTheme(AppTheme theme);
    void setLanguage(const QString &language);

signals:
    void themeChanged();
    void languageChanged();

private:
    AppConfig() = default;

    AppTheme m_theme = AppTheme::Light;
    QString m_language = QStringLiteral("en");
};
