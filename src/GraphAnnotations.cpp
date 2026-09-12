#include "GraphAnnotations.h"

#include "AppConfig.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {

QString normalizeRoot(const QString &rootDir)
{
    return QDir(rootDir).absolutePath();
}

} // namespace

void GraphAnnotations::setRoot(const QString &rootDir)
{
    m_root = normalizeRoot(rootDir);
    m_deleted.clear();
    m_comments.clear();
    m_fictitious.clear();
    m_hidden.clear();
    m_languageFilter = QStringLiteral("all");
    m_groups.clear();
    if (!m_root.isEmpty())
        load();
}

bool GraphAnnotations::isDeleted(const QString &path) const
{
    return m_deleted.contains(path);
}

void GraphAnnotations::deleteFile(const QString &path)
{
    if (path.isEmpty())
        return;
    if (!m_deleted.contains(path))
        m_deleted << path;
    m_comments.remove(path);
    QVector<FictitiousLink> kept;
    for (const FictitiousLink &link : m_fictitious) {
        if (link.fromPath != path && link.toPath != path)
            kept.push_back(link);
    }
    m_fictitious = kept;
    m_hidden.removeAll(path);
    for (FileGroup &g : m_groups)
        g.files.removeAll(path);
    QVector<FileGroup> groups;
    for (const FileGroup &g : m_groups) {
        if (!g.files.isEmpty())
            groups.push_back(g);
    }
    m_groups = groups;
    save();
}

QString GraphAnnotations::comment(const QString &path) const
{
    return m_comments.value(path);
}

void GraphAnnotations::setComment(const QString &path, const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        m_comments.remove(path);
    else
        m_comments.insert(path, trimmed);
    save();
}

void GraphAnnotations::addFictitious(const QString &fromPath, const QString &toPath, const QString &comment)
{
    if (fromPath.isEmpty() || toPath.isEmpty() || fromPath == toPath)
        return;
    for (FictitiousLink &link : m_fictitious) {
        if (link.fromPath == fromPath && link.toPath == toPath) {
            link.comment = comment.trimmed();
            save();
            return;
        }
    }
    FictitiousLink link;
    link.fromPath = fromPath;
    link.toPath = toPath;
    link.comment = comment.trimmed();
    m_fictitious.push_back(link);
    save();
}

void GraphAnnotations::setFictitiousComment(const QString &fromPath, const QString &toPath, const QString &comment)
{
    addFictitious(fromPath, toPath, comment);
}

void GraphAnnotations::removeFictitious(const QString &fromPath, const QString &toPath)
{
    QVector<FictitiousLink> kept;
    for (const FictitiousLink &link : m_fictitious) {
        if (link.fromPath == fromPath && link.toPath == toPath)
            continue;
        kept.push_back(link);
    }
    m_fictitious = kept;
    save();
}

QString GraphAnnotations::filePath() const
{
    const QByteArray hash = QCryptographicHash::hash(m_root.toUtf8(), QCryptographicHash::Sha1).toHex();
    return AppConfig::configDir() + QStringLiteral("/annotations/") + QString::fromLatin1(hash) + QStringLiteral(".json");
}

void GraphAnnotations::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    const QJsonArray deleted = obj.value(QStringLiteral("deleted")).toArray();
    for (const QJsonValue &v : deleted) {
        const QString path = v.toString();
        if (!path.isEmpty())
            m_deleted << path;
    }
    const QJsonObject comments = obj.value(QStringLiteral("comments")).toObject();
    for (auto it = comments.begin(); it != comments.end(); ++it) {
        const QString text = it.value().toString();
        if (!it.key().isEmpty() && !text.isEmpty())
            m_comments.insert(it.key(), text);
    }
    const QJsonArray fictitious = obj.value(QStringLiteral("fictitious")).toArray();
    for (const QJsonValue &v : fictitious) {
        const QJsonObject o = v.toObject();
        FictitiousLink link;
        link.fromPath = o.value(QStringLiteral("from")).toString();
        link.toPath = o.value(QStringLiteral("to")).toString();
        link.comment = o.value(QStringLiteral("comment")).toString();
        if (!link.fromPath.isEmpty() && !link.toPath.isEmpty() && link.fromPath != link.toPath)
            m_fictitious.push_back(link);
    }
    const QJsonArray hidden = obj.value(QStringLiteral("hiddenFiles")).toArray();
    for (const QJsonValue &v : hidden) {
        const QString path = v.toString();
        if (!path.isEmpty())
            m_hidden << path;
    }
    m_languageFilter = obj.value(QStringLiteral("languageFilter")).toString(QStringLiteral("all"));
    if (m_languageFilter.isEmpty())
        m_languageFilter = QStringLiteral("all");
    const QJsonArray groups = obj.value(QStringLiteral("groups")).toArray();
    for (const QJsonValue &v : groups) {
        const QJsonObject o = v.toObject();
        FileGroup g;
        g.id = o.value(QStringLiteral("id")).toString();
        g.name = o.value(QStringLiteral("name")).toString();
        g.color = o.value(QStringLiteral("color")).toString();
        g.border = o.value(QStringLiteral("border")).toString();
        const QJsonArray files = o.value(QStringLiteral("files")).toArray();
        for (const QJsonValue &f : files) {
            const QString path = f.toString();
            if (!path.isEmpty())
                g.files << path;
        }
        if (g.id.isEmpty())
            g.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!g.files.isEmpty())
            m_groups.push_back(g);
    }
}

void GraphAnnotations::save() const
{
    if (m_root.isEmpty())
        return;
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return;
    QJsonObject obj;
    obj.insert(QStringLiteral("root"), m_root);
    QJsonArray deleted;
    for (const QString &p : m_deleted)
        deleted.append(p);
    obj.insert(QStringLiteral("deleted"), deleted);
    QJsonObject comments;
    for (auto it = m_comments.begin(); it != m_comments.end(); ++it)
        comments.insert(it.key(), it.value());
    obj.insert(QStringLiteral("comments"), comments);
    QJsonArray fictitious;
    for (const FictitiousLink &link : m_fictitious) {
        QJsonObject o;
        o.insert(QStringLiteral("from"), link.fromPath);
        o.insert(QStringLiteral("to"), link.toPath);
        o.insert(QStringLiteral("comment"), link.comment);
        fictitious.append(o);
    }
    obj.insert(QStringLiteral("fictitious"), fictitious);
    QJsonArray hidden;
    for (const QString &p : m_hidden)
        hidden.append(p);
    obj.insert(QStringLiteral("hiddenFiles"), hidden);
    obj.insert(QStringLiteral("languageFilter"), m_languageFilter);
    QJsonArray groups;
    for (const FileGroup &g : m_groups) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), g.id);
        o.insert(QStringLiteral("name"), g.name);
        o.insert(QStringLiteral("color"), g.color);
        o.insert(QStringLiteral("border"), g.border);
        QJsonArray files;
        for (const QString &p : g.files)
            files.append(p);
        o.insert(QStringLiteral("files"), files);
        groups.append(o);
    }
    obj.insert(QStringLiteral("groups"), groups);
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

bool GraphAnnotations::isFileHidden(const QString &path) const
{
    return m_hidden.contains(path);
}

void GraphAnnotations::setHiddenFiles(const QStringList &paths)
{
    m_hidden.clear();
    for (const QString &path : paths) {
        if (!path.isEmpty() && !m_hidden.contains(path))
            m_hidden << path;
    }
    save();
}

void GraphAnnotations::setLanguageFilter(const QString &key)
{
    m_languageFilter = key.isEmpty() ? QStringLiteral("all") : key;
    save();
}

const FileGroup *GraphAnnotations::groupOf(const QString &path) const
{
    for (const FileGroup &g : m_groups) {
        if (g.files.contains(path))
            return &g;
    }
    return nullptr;
}

FileGroup *GraphAnnotations::groupOf(const QString &path)
{
    for (FileGroup &g : m_groups) {
        if (g.files.contains(path))
            return &g;
    }
    return nullptr;
}

void GraphAnnotations::removeFromGroup(const QString &path)
{
    if (path.isEmpty())
        return;
    bool changed = false;
    for (FileGroup &g : m_groups) {
        if (g.files.removeAll(path) > 0)
            changed = true;
    }
    if (!changed)
        return;
    QVector<FileGroup> kept;
    for (const FileGroup &g : m_groups) {
        if (!g.files.isEmpty())
            kept.push_back(g);
    }
    m_groups = kept;
    save();
}

FileGroup GraphAnnotations::createGroup(const QString &name, const QStringList &paths, const QString &color,
                                        const QString &border)
{
    QStringList unique;
    for (const QString &path : paths) {
        if (!path.isEmpty() && !unique.contains(path))
            unique << path;
    }
    for (const QString &path : unique) {
        for (FileGroup &g : m_groups)
            g.files.removeAll(path);
    }
    QVector<FileGroup> kept;
    for (const FileGroup &g : m_groups) {
        if (!g.files.isEmpty())
            kept.push_back(g);
    }
    m_groups = kept;

    FileGroup g;
    g.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    g.name = name.trimmed();
    if (g.name.isEmpty())
        g.name = QStringLiteral("Group");
    g.color = color;
    g.border = border;
    g.files = unique;
    if (!g.files.isEmpty())
        m_groups.push_back(g);
    save();
    return g;
}

void GraphAnnotations::addToGroup(const QString &groupId, const QString &path)
{
    if (groupId.isEmpty() || path.isEmpty())
        return;
    FileGroup *target = nullptr;
    for (FileGroup &g : m_groups) {
        if (g.id == groupId) {
            target = &g;
            break;
        }
    }
    if (!target)
        return;
    if (target->files.contains(path))
        return;
    removeFromGroup(path);
    for (FileGroup &g : m_groups) {
        if (g.id == groupId) {
            g.files << path;
            save();
            return;
        }
    }
}
