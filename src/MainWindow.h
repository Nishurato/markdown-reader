#pragma once

#include <QMainWindow>

class DocumentOutline;
class DocumentTab;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QUrl;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);

private:
    DocumentTab *currentTab() const;
    DocumentTab *loadFile(const QString &path, DocumentTab *tab);
    void addTab(DocumentTab *tab);
    void newTab();
    void closeTab(int index);
    void switchTab(int offset);
    void updateCurrentTab();
    void chooseFile();
    void setOutlineVisible(bool visible);
    void followLink(DocumentTab &tab, const QUrl &link);

    QTabBar *tabs_;
    QStackedWidget *pages_;
    QWidget *sidebar_;
    DocumentOutline *outline_;
    QSplitter *splitter_;
    QToolButton *outlineToggle_;
    int outlineWidth_ = 225;
};
