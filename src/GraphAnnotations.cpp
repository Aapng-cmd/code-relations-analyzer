#include "GraphAnnotations.h"

#include "AppConfig.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}
