#include "AppConfig.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

AppConfig &AppConfig::instance()
{
    static AppConfig cfg;
    return cfg;
}

QString AppConfig::configDir()
{
    return QDir::homePath() + QStringLiteral("/.CoReAnalyzer");
}

QString AppConfig::configFilePath()
{
    return configDir() + QStringLiteral("/config.json");
}

void AppConfig::load()
{
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    const QString theme = obj.value(QStringLiteral("theme")).toString(QStringLiteral("light"));
    m_theme = (theme == QLatin1String("dark")) ? AppTheme::Dark : AppTheme::Light;
    const QString lang = obj.value(QStringLiteral("language")).toString(QStringLiteral("en"));
    m_language = (lang == QLatin1String("ru")) ? QStringLiteral("ru") : QStringLiteral("en");
}

void AppConfig::save() const
{
    QDir().mkpath(configDir());
    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return;
    QJsonObject obj;
    obj.insert(QStringLiteral("theme"), m_theme == AppTheme::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    obj.insert(QStringLiteral("language"), m_language);
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

void AppConfig::setTheme(AppTheme theme)
{
    if (m_theme == theme)
        return;
    m_theme = theme;
    save();
    emit themeChanged();
}

void AppConfig::setLanguage(const QString &language)
{
    const QString lang = (language == QLatin1String("ru")) ? QStringLiteral("ru") : QStringLiteral("en");
    if (m_language == lang)
        return;
    m_language = lang;
    save();
    emit languageChanged();
}
