#include "I18n.h"

#include "ConfigPaths.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

QString I18n::s_language = QStringLiteral("en");
QMap<QString, QString> I18n::s_en;
QMap<QString, QString> I18n::s_current;

bool I18n::loadFile(const QString &path, QMap<QString, QString> *out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        (*out)[it.key()] = it.value().toString();
    return !out->isEmpty();
}

void I18n::load(const QString &language)
{
    s_en.clear();
    s_current.clear();
    s_language = (language == QLatin1String("ru")) ? QStringLiteral("ru") : QStringLiteral("en");

    loadFile(ConfigPaths::findFile(QStringLiteral("en.json")), &s_en);
    if (s_language == QLatin1String("en")) {
        s_current = s_en;
        return;
    }
    loadFile(ConfigPaths::findFile(QStringLiteral("ru.json")), &s_current);
}

QString I18n::t(const QString &key)
{
    if (s_current.contains(key))
        return s_current.value(key);
    if (s_en.contains(key))
        return s_en.value(key);
    return key;
}

QString I18n::language()
{
    return s_language;
}

QString I18n::languageName(SourceLanguage language)
{
    switch (language) {
    case SourceLanguage::Python:
        return t(QStringLiteral("lang_python"));
    case SourceLanguage::Cpp:
        return t(QStringLiteral("lang_cpp"));
    case SourceLanguage::Java:
        return t(QStringLiteral("lang_java"));
    case SourceLanguage::Go:
        return t(QStringLiteral("lang_go"));
    case SourceLanguage::Rust:
        return t(QStringLiteral("lang_rust"));
    case SourceLanguage::R:
        return t(QStringLiteral("lang_r"));
    case SourceLanguage::Php:
        return t(QStringLiteral("lang_php"));
    case SourceLanguage::Html:
        return t(QStringLiteral("lang_html"));
    case SourceLanguage::Css:
        return t(QStringLiteral("lang_css"));
    case SourceLanguage::JavaScript:
        return t(QStringLiteral("lang_js"));
    default:
        return t(QStringLiteral("lang_all"));
    }
}
