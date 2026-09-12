#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

struct FictitiousLink {
    QString fromPath;
    QString toPath;
    QString comment;
};

struct FileGroup {
    QString id;
    QString name;
    QString color;
    QString border;
    QStringList files;
};

class GraphAnnotations {
public:
    void setRoot(const QString &rootDir);
    const QString &root() const { return m_root; }

    bool isDeleted(const QString &path) const;
    void deleteFile(const QString &path);

    QString comment(const QString &path) const;
    void setComment(const QString &path, const QString &text);

    QVector<FictitiousLink> fictitious() const { return m_fictitious; }
    void addFictitious(const QString &fromPath, const QString &toPath, const QString &comment);
    void setFictitiousComment(const QString &fromPath, const QString &toPath, const QString &comment);
    void removeFictitious(const QString &fromPath, const QString &toPath);

    bool isFileHidden(const QString &path) const;
    void setHiddenFiles(const QStringList &paths);
    QStringList hiddenFiles() const { return m_hidden; }

    QString languageFilter() const { return m_languageFilter; }
    void setLanguageFilter(const QString &key);

    QVector<FileGroup> groups() const { return m_groups; }
    const FileGroup *groupOf(const QString &path) const;
    FileGroup *groupOf(const QString &path);
    FileGroup createGroup(const QString &name, const QStringList &paths, const QString &color, const QString &border);
    void addToGroup(const QString &groupId, const QString &path);
    void removeFromGroup(const QString &path);

private:
    void load();
    void save() const;
    QString filePath() const;

    QString m_root;
    QStringList m_deleted;
    QMap<QString, QString> m_comments;
    QVector<FictitiousLink> m_fictitious;
    QStringList m_hidden;
    QString m_languageFilter = QStringLiteral("all");
    QVector<FileGroup> m_groups;
};
