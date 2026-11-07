#pragma once

#include <QTreeWidget>

class QTextBrowser;
class QTextDocument;

class DocumentOutline final : public QTreeWidget
{
    Q_OBJECT

public:
    explicit DocumentOutline(QWidget *parent = nullptr);

    void rebuild(const QTextDocument &document);
    static void navigate(QTextBrowser &browser, int position);

signals:
    void headingSelected(int position);
};
