#include "MainWindow.h"

#include "DocumentOutline.h"
#include "MarkdownRenderer.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      renderer_(new MarkdownRenderer),
      outline_(new DocumentOutline),
      splitter_(new QSplitter(Qt::Horizontal)),
      outlineToggle_(new QToolButton)
{
    setWindowTitle(tr("Markdown Reader"));
    resize(1020, 680);

    auto *fileMenu = menuBar()->addMenu(tr("&Файл"));
    auto *openAction = fileMenu->addAction(tr("&Открыть…"), this, &MainWindow::chooseFile);
    openAction->setShortcut(QKeySequence::Open);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QWidget(central);
    toolbar->setObjectName(QStringLiteral("readerToolbar"));
    toolbar->setStyleSheet(QStringLiteral(
        "#readerToolbar { background: #464646; color: #fff; }"
        "QLabel, QToolButton { color: #fff; }"
        "QToolButton { background: #464646; border: 1px solid #696969;"
        " border-radius: 3px; padding: 4px 8px; }"
        "QToolButton:hover, QToolButton:focus { background: #565656; }"));
    auto *toolbarLayout = new QGridLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    toolbarLayout->setColumnStretch(0, 1);
    toolbarLayout->setColumnStretch(2, 1);
    outlineToggle_->setObjectName(QStringLiteral("outlineToggle"));
    outlineToggle_->setCheckable(true);
    outlineToggle_->setChecked(true);
    outlineToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbarLayout->addWidget(outlineToggle_, 0, 0, Qt::AlignLeft);
    auto *bookmarks = new QLabel(tr("Закладки"));
    bookmarks->setAlignment(Qt::AlignCenter);
    toolbarLayout->addWidget(bookmarks, 0, 1);

    outline_->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: #282828; color: #f5f5f5; }"
        "QTreeWidget::item { padding: 4px; }"
        "QTreeWidget::item:selected { background: #4b5d71; color: white; }"));
    outline_->setMinimumWidth(125);
    splitter_->addWidget(outline_);
    splitter_->addWidget(renderer_);
    splitter_->setChildrenCollapsible(false);
    splitter_->setStretchFactor(1, 1);

    layout->addWidget(toolbar);
    layout->addWidget(splitter_, 1);
    setCentralWidget(central);
    setOutlineVisible(true);
    toolbarLayout->setColumnMinimumWidth(2, outlineToggle_->sizeHint().width());

    connect(outlineToggle_, &QToolButton::toggled, this, &MainWindow::setOutlineVisible);
    connect(&manager_, &DocumentManager::documentOpened, this,
            [this](const QString &markdown, const QString &path) {
        renderer_->render(markdown, path);
        outline_->rebuild(*renderer_->document());
        setWindowTitle(tr("%1 — Markdown Reader").arg(QFileInfo(path).fileName()));
    });
    connect(outline_, &DocumentOutline::headingSelected, this, [this](int position) {
        DocumentOutline::navigate(*renderer_, position);
    });
    connect(renderer_, &QTextBrowser::anchorClicked, this, &MainWindow::followLink);
    renderer_->setPlainText(tr("Откройте Markdown-документ через меню «Файл»."));
}

bool MainWindow::openFile(const QString &path)
{
    QString error;
    if (manager_.open(path, error))
        return true;

    QMessageBox::warning(this, tr("Ошибка открытия"), error);
    return false;
}

void MainWindow::chooseFile()
{
    const auto directory = manager_.activePath().isEmpty()
        ? QString() : QFileInfo(manager_.activePath()).absolutePath();
    const auto path = QFileDialog::getOpenFileName(this, tr("Открыть Markdown"), directory,
        tr("Markdown (*.md *.markdown);;Все файлы (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::setOutlineVisible(bool visible)
{
    if (!visible)
        outlineWidth_ = splitter_->sizes().first();

    outline_->setVisible(visible);
    if (visible) {
        const int available = width() - splitter_->handleWidth();
        splitter_->setSizes({outlineWidth_, qMax(0, available - outlineWidth_)});
    }

    outlineToggle_->setArrowType(visible ? Qt::LeftArrow : Qt::RightArrow);
    outlineToggle_->setText(tr("Заголовки"));
    const auto action = visible ? tr("Свернуть заголовки") : tr("Развернуть заголовки");
    outlineToggle_->setToolTip(action);
    outlineToggle_->setAccessibleName(action);
}

void MainWindow::followLink(const QUrl &link)
{
    const auto url = renderer_->resolveLink(link);
    if (url.isValid()) {
        const auto scheme = url.scheme();
        if ((scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
            && !url.host().isEmpty() && QDesktopServices::openUrl(url)) {
            return;
        }

        const auto path = url.toLocalFile();
        if (url.isLocalFile() && url.host().isEmpty()
            && !QDir::fromNativeSeparators(path).startsWith(QStringLiteral("//"))
            && DocumentManager::isMarkdownPath(path)) {
            if (path != manager_.activePath()) {
                openFile(path);
                return;
            }
            if (!url.hasFragment())
                return;
        }
    }

    statusBar()->showMessage(tr("Не удалось открыть ссылку."), 5000);
}
