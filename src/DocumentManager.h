#pragma once

#include <QObject>
#include <QString>

class DocumentManager final : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    bool open(const QString &path, QString &error);
    static bool isMarkdownPath(const QString &path);
    const QString &activePath() const { return activePath_; }

signals:
    void documentOpened(const QString &markdown, const QString &path);

private:
    QString activePath_;
    QString markdown_;
};
