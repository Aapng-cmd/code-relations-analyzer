#pragma once

#include <QMap>
#include <QString>

class I18n {
public:
    static void load(const QString &language);
    static QString t(const QString &key);
    static QString language();

private:
    static bool loadFile(const QString &path, QMap<QString, QString> *out);

    static QString s_language;
    static QMap<QString, QString> s_en;
    static QMap<QString, QString> s_current;
};
