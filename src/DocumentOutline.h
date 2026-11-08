#pragma once

#include <QTreeWidget>

class QTextDocument;

class DocumentOutline final : public QTreeWidget
{
    Q_OBJECT

public:
    explicit DocumentOutline(QWidget *parent = nullptr);

    void rebuild(const QTextDocument &document);

signals:
    void headingSelected(int position);
};
