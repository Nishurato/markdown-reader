#pragma once

#include "DocumentManager.h"

#include <QScrollArea>

class MarkdownRenderer;

class DocumentTab final : public QScrollArea
{
    Q_OBJECT

public:
    explicit DocumentTab(QWidget *parent = nullptr);

    bool open(const QString &path, QString &error) { return manager_.open(path, error); }
    const QString &path() const { return manager_.activePath(); }
    MarkdownRenderer *renderer() const { return renderer_; }
    void navigateTo(int position);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    DocumentManager manager_;
    MarkdownRenderer *renderer_;
};
