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

private:
    void load();
    void save() const;
    QString filePath() const;

    QString m_root;
    QStringList m_deleted;
    QMap<QString, QString> m_comments;
    QVector<FictitiousLink> m_fictitious;
};
