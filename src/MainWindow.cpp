#include "MainWindow.h"

#include "DocumentOutline.h"
#include "DocumentTab.h"
#include "MarkdownRenderer.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMessageBox>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      tabs_(new QTabBar),
      pages_(new QStackedWidget),
      sidebar_(new QWidget),
      outline_(new DocumentOutline),
      splitter_(new QSplitter(Qt::Horizontal)),
      outlineToggle_(new QToolButton)
{
    setWindowTitle(tr("Markdown Reader"));
    resize(1020, 680);

    new QShortcut(QKeySequence::AddTab, this, this, &MainWindow::newTab);
    new QShortcut(QKeySequence::Open, this, this, &MainWindow::chooseFile);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this, [this] {
        closeTab(tabs_->currentIndex());
    });
#ifdef Q_OS_MACOS
    // Physical Control keeps Tab switching inside the window on macOS.
    constexpr auto tabModifier = Qt::MetaModifier;
#else
    constexpr auto tabModifier = Qt::ControlModifier;
#endif
    new QShortcut(QKeySequence(tabModifier | Qt::Key_Tab), this, [this] { switchTab(1); });
    new QShortcut(QKeySequence(tabModifier | Qt::ShiftModifier | Qt::Key_Tab),
                  this, [this] { switchTab(-1); });

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QWidget;
    toolbar->setObjectName(QStringLiteral("readerToolbar"));
    toolbar->setStyleSheet(QStringLiteral(
        "#readerToolbar { background: #464646; }"
        "QTabBar::tab { background: #535353; color: #fff; padding: 5px 8px; }"
        "QTabBar::tab:selected { background: #303030; }"
        "QTabBar::tab:hover { background: #626262; }"
        "#newTabButton { background: #464646; color: #fff; padding: 3px 8px; }"
        "#newTabButton:hover, #newTabButton:focus { background: #626262; }"));
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 3, 8, 0);
    toolbarLayout->setSpacing(4);
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    tabs_->setMovable(true);
    tabs_->setTabsClosable(true);
    tabs_->setUsesScrollButtons(true);
    tabs_->setElideMode(Qt::ElideMiddle);
    tabs_->setSelectionBehaviorOnRemove(QTabBar::SelectPreviousTab);
    tabs_->setAccessibleName(tr("Вкладки документов"));
    toolbarLayout->addWidget(tabs_);
    auto *newTabButton = new QToolButton;
    newTabButton->setObjectName(QStringLiteral("newTabButton"));
    newTabButton->setText(QStringLiteral("+"));
    newTabButton->setToolTip(tr("Новая вкладка"));
    newTabButton->setAccessibleName(tr("Новая вкладка"));
    toolbarLayout->addWidget(newTabButton);
    toolbarLayout->addStretch(1);

    sidebar_->setObjectName(QStringLiteral("readerSidebar"));
    auto *sidebarLayout = new QVBoxLayout(sidebar_);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    auto *outlineControls = new QHBoxLayout;
    outlineControls->setContentsMargins(8, 8, 8, 8);
    outlineToggle_->setObjectName(QStringLiteral("outlineToggle"));
    outlineToggle_->setCheckable(true);
    outlineToggle_->setStyleSheet(QStringLiteral(
        "QToolButton { color: #242424; background: #f3f3f3; border: 1px solid #888;"
        " border-radius: 3px; padding: 3px; }"
        "QToolButton:hover, QToolButton:focus { background: #fff; }"));
    outlineControls->addWidget(outlineToggle_);
    outlineControls->addStretch(1);
    sidebarLayout->addLayout(outlineControls);
    sidebarLayout->addWidget(outline_, 1);
    sidebarLayout->addStretch();

    outline_->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: #282828; color: #f5f5f5; }"
        "QTreeWidget::item { padding: 4px; }"
        "QTreeWidget::item:selected { background: #4b5d71; color: white; }"));
    splitter_->addWidget(sidebar_);
    splitter_->addWidget(pages_);
    splitter_->setChildrenCollapsible(false);
    splitter_->setStretchFactor(1, 1);

    layout->addWidget(toolbar);
    layout->addWidget(splitter_, 1);
    setCentralWidget(central);
    setOutlineVisible(false);

    connect(newTabButton, &QToolButton::clicked, this, &MainWindow::newTab);
    connect(tabs_, &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);
    connect(tabs_, &QTabBar::currentChanged, this, &MainWindow::updateCurrentTab);
    connect(outlineToggle_, &QToolButton::toggled, this, &MainWindow::setOutlineVisible);
    connect(outline_, &DocumentOutline::headingSelected, this, [this](int position) {
        if (auto *tab = currentTab())
            tab->navigateTo(position);
    });
    newTab();
}

DocumentTab *MainWindow::currentTab() const
{
    return tabs_->tabData(tabs_->currentIndex()).value<DocumentTab *>();
}

void MainWindow::addTab(DocumentTab *tab)
{
    pages_->addWidget(tab);
    connect(tab, &DocumentTab::openRequested, this, &MainWindow::chooseFile);
    connect(tab, &DocumentTab::linkActivated, this,
            [this, tab](const QUrl &link) { followLink(*tab, link); });
    {
        const QSignalBlocker blocker(tabs_);
        const int index = tabs_->addTab(tr("Новая вкладка"));
        // Store the page with the tab so dragging cannot change the association.
        tabs_->setTabData(index, QVariant::fromValue(tab));
        tabs_->setCurrentIndex(index);
    }
    updateCurrentTab();
}

void MainWindow::newTab()
{
    addTab(new DocumentTab);
}

void MainWindow::closeTab(int index)
{
    auto *tab = tabs_->tabData(index).value<DocumentTab *>();
    if (!tab)
        return;

    tabs_->removeTab(index);
    pages_->removeWidget(tab);
    delete tab;
    if (tabs_->count() == 0)
        newTab();
}

void MainWindow::switchTab(int offset)
{
    const int count = tabs_->count();
    if (count > 1)
        tabs_->setCurrentIndex((tabs_->currentIndex() + offset + count) % count);
}

void MainWindow::updateCurrentTab()
{
    auto *tab = currentTab();
    if (!tab)
        return;

    pages_->setCurrentWidget(tab);
    const auto name = QFileInfo(tab->path()).fileName();
    tabs_->setTabText(tabs_->currentIndex(), name.isEmpty() ? tr("Новая вкладка") : name);
    tabs_->setTabToolTip(tabs_->currentIndex(), QDir::toNativeSeparators(tab->path()));
    setWindowTitle(name.isEmpty() ? tr("Markdown Reader") : tr("%1 — Markdown Reader").arg(name));
    if (auto *renderer = tab->renderer())
        outline_->rebuild(*renderer->document());
    else
        outline_->clear();
}

bool MainWindow::openFile(const QString &path)
{
    return loadFile(path, currentTab()) != nullptr;
}

DocumentTab *MainWindow::loadFile(const QString &path, DocumentTab *tab)
{
    std::unique_ptr<DocumentTab> next;
    if (!tab || !tab->path().isEmpty()) {
        next = std::make_unique<DocumentTab>();
        tab = next.get();
    }

    QString error;
    if (!tab->open(path, error)) {
        QMessageBox::warning(this, tr("Ошибка открытия"), error);
        return nullptr;
    }
    if (next)
        addTab(next.release());
    else
        updateCurrentTab();
    return tab;
}

void MainWindow::chooseFile()
{
    const auto *tab = currentTab();
    const auto directory = tab && !tab->path().isEmpty() ? QFileInfo(tab->path()).absolutePath() : QString();
    const auto path = QFileDialog::getOpenFileName(this, tr("Открыть Markdown"), directory,
        tr("Markdown (*.md *.markdown);;Все файлы (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::setOutlineVisible(bool visible)
{
    if (!visible && !outline_->isHidden() && splitter_->sizes().first() >= 125)
        outlineWidth_ = splitter_->sizes().first();

    outlineToggle_->setChecked(visible);
    outlineToggle_->setArrowType(visible ? Qt::LeftArrow : Qt::RightArrow);
    const auto action = visible ? tr("Свернуть заголовки") : tr("Развернуть заголовки");
    outlineToggle_->setToolTip(action);
    outlineToggle_->setAccessibleName(action);
    outline_->setVisible(visible);
    sidebar_->setStyleSheet(visible
        ? QStringLiteral("#readerSidebar { background: #282828; }")
        : QStringLiteral("#readerSidebar { background: #dedede; }"));

    splitter_->setHandleWidth(visible ? 5 : 0);
    splitter_->handle(1)->setEnabled(visible);
    if (visible) {
        sidebar_->setMinimumWidth(125);
        sidebar_->setMaximumWidth(QWIDGETSIZE_MAX);
        const int available = splitter_->width() - splitter_->handleWidth();
        splitter_->setSizes({outlineWidth_, qMax(0, available - outlineWidth_)});
    } else {
        const int railWidth = outlineToggle_->sizeHint().width() + 16;
        sidebar_->setFixedWidth(railWidth);
        splitter_->setSizes({railWidth, qMax(0, splitter_->width() - railWidth)});
    }
}

void MainWindow::followLink(DocumentTab &tab, const QUrl &link)
{
    const auto url = tab.renderer()->resolveLink(link);
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
            auto *target = path == tab.path() ? &tab : loadFile(path, nullptr);
            if (target && url.hasFragment()) {
                const auto fragment = url.fragment(QUrl::FullyDecoded);
                if (!target->navigateToAnchor(fragment))
                    statusBar()->showMessage(tr("Якорь «%1» не найден.").arg(fragment), 5000);
                else if (auto *bar = findChild<QStatusBar *>())
                    bar->clearMessage();
            }
            return;
        }
    }

    statusBar()->showMessage(tr("Не удалось открыть ссылку."), 5000);
}
