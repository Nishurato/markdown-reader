#pragma once

#include <QTextBrowser>

class MarkdownRenderer final : public QTextBrowser
{
    Q_OBJECT

public:
    explicit MarkdownRenderer(QWidget *parent = nullptr);

    void render(const QString &markdown, const QString &path);
    QUrl resolveLink(const QUrl &link) const;

#ifdef Q_OS_LINUX
protected:
    void changeEvent(QEvent *event) override;

private:
    void updateSelectionPalette();
#endif
};
