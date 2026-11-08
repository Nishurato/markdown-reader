#pragma once

#include "DocumentManager.h"

#include <QHash>
#include <QScrollArea>

class MarkdownRenderer;
class QUrl;

class DocumentTab final : public QScrollArea
{
    Q_OBJECT

public:
    explicit DocumentTab(QWidget *parent = nullptr);

    bool open(const QString &path, QString &error) { return manager_.open(path, error); }
    const QString &path() const { return manager_.activePath(); }
    MarkdownRenderer *renderer() const { return renderer_; }
    void navigateTo(int position);
    bool navigateToAnchor(const QString &fragment);

signals:
    void openRequested();
    void linkActivated(const QUrl &link);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createStartPage();
    void showDocument(const QString &markdown, const QString &path);

    DocumentManager manager_;
    MarkdownRenderer *renderer_ = nullptr;
    QHash<QString, int> anchors_;
};
