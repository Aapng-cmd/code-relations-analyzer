#pragma once

#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>

namespace ConfigPaths {

inline QStringList searchDirs()
{
    QStringList dirs;
    const QString exe = QCoreApplication::applicationDirPath();
    dirs << exe + QStringLiteral("/config");
    dirs << exe + QStringLiteral("/../config");
#ifdef TRANSLATIONS_DIR
    dirs << QStringLiteral(TRANSLATIONS_DIR);
#endif
    return dirs;
}

inline QString findFile(const QString &fileName)
{
    for (const QString &dir : searchDirs()) {
        const QString path = dir + QLatin1Char('/') + fileName;
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

} // namespace ConfigPaths
