#pragma once

#include "DocumentManager.h"

#include <QMainWindow>

class DocumentOutline;
class MarkdownRenderer;
class QSplitter;
class QToolButton;
class QUrl;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);

private:
    void chooseFile();
    void setOutlineVisible(bool visible);
    void followLink(const QUrl &link);

    DocumentManager manager_;
    MarkdownRenderer *renderer_;
    DocumentOutline *outline_;
    QSplitter *splitter_;
    QToolButton *outlineToggle_;
    int outlineWidth_ = 225;
};
