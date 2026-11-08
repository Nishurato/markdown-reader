#include "DocumentTab.h"

#include "AnchorResolver.h"
#include "MarkdownRenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

namespace {
constexpr int pageWidth = 800;
constexpr int pageGutter = 24;
}

DocumentTab::DocumentTab(QWidget *parent)
    : QScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setStyleSheet(QStringLiteral("DocumentTab, #documentCanvas { background: #dedede; }"));
    setWidgetResizable(true);
    setWidget(createStartPage());
    connect(&manager_, &DocumentManager::documentOpened, this, &DocumentTab::showDocument);
}

QWidget *DocumentTab::createStartPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("startPage"));
    page->setStyleSheet(QStringLiteral(
        "#startPage { background: #dedede; }"
        "QLabel { color: #242424; }"
        "#startPageHint { color: #555; }"
        "QPushButton { background: #1b55a0; color: white; padding: 8px 20px;"
        " border: 2px solid transparent; border-radius: 5px; }"
        "QPushButton:hover { background: #174782; }"
        "QPushButton:pressed { background: #113865; }"
        "QPushButton:focus { border-color: #102f57; }"));

    auto *title = new QLabel(tr("Markdown Reader"));
    auto titleFont = title->font();
    titleFont.setPointSize(26);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);

    auto *description = new QLabel(tr("Читайте локальные Markdown-документы без интернета."));
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);

    auto *open = new QPushButton(tr("Открыть файл"));
    open->setObjectName(QStringLiteral("openFileButton"));
    connect(open, &QPushButton::clicked, this, &DocumentTab::openRequested);
    const auto shortcut = QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText);
    auto *hint = new QLabel(tr("Горячая клавиша: %1").arg(shortcut));
    hint->setObjectName(QStringLiteral("startPageHint"));
    hint->setAlignment(Qt::AlignCenter);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 24, 20, 24);
    layout->setSpacing(12);
    layout->addStretch(1);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(open, 0, Qt::AlignHCenter);
    layout->addWidget(hint);
    layout->addStretch(1);
    return page;
}

void DocumentTab::showDocument(const QString &markdown, const QString &path)
{
    if (!renderer_) {
        auto *canvas = new QWidget;
        canvas->setObjectName(QStringLiteral("documentCanvas"));
        auto *layout = new QVBoxLayout(canvas);
        layout->setContentsMargins(pageGutter, pageGutter, pageGutter, pageGutter);
        layout->setSizeConstraint(QLayout::SetFixedSize);
        renderer_ = new MarkdownRenderer;
        renderer_->setFixedWidth(pageWidth);
        renderer_->setLineWrapMode(QTextEdit::FixedPixelWidth);
        renderer_->setLineWrapColumnOrWidth(pageWidth);
        renderer_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        renderer_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        renderer_->installEventFilter(this);
        connect(renderer_, &QTextBrowser::anchorClicked, this, &DocumentTab::linkActivated);
        layout->addWidget(renderer_);
        takeWidget()->deleteLater();
        setWidgetResizable(false);
        setWidget(canvas);
    }

    const auto resizePage = [this](const QSizeF &size) {
        renderer_->setFixedHeight(qCeil(size.height()));
        widget()->layout()->activate();
    };
    renderer_->render(markdown, path);
    anchors_ = AnchorResolver::indexHeadings(*renderer_->document());
    auto *documentLayout = renderer_->document()->documentLayout();
    connect(documentLayout, &QAbstractTextDocumentLayout::documentSizeChanged, this, resizePage);
    resizePage(documentLayout->documentSize());
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
}

bool DocumentTab::navigateToAnchor(const QString &fragment)
{
    const int position = anchors_.value(AnchorResolver::normalizeFragment(fragment), -1);
    if (!fragment.isEmpty() && position < 0)
        return false;

    // A newly opened tab needs its viewport and scroll range before jumping.
    QTimer::singleShot(0, this, [this, position] {
        if (position < 0)
            verticalScrollBar()->setValue(0);
        else
            navigateTo(position);
    });
    return true;
}

void DocumentTab::navigateTo(int position)
{
    if (!renderer_)
        return;

    const auto block = renderer_->document()->findBlock(position);
    if (!block.isValid())
        return;

    renderer_->setTextCursor(QTextCursor(block));
    const auto bounds = renderer_->document()->documentLayout()->blockBoundingRect(block);
    verticalScrollBar()->setValue(qFloor(bounds.top()) + renderer_->y());
}

bool DocumentTab::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == renderer_ && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        auto *bar = verticalScrollBar();
        if (key->modifiers() == Qt::NoModifier) {
            if (key->key() == Qt::Key_PageUp) {
                bar->triggerAction(QAbstractSlider::SliderPageStepSub);
                return true;
            }
            if (key->key() == Qt::Key_PageDown) {
                bar->triggerAction(QAbstractSlider::SliderPageStepAdd);
                return true;
            }
        }
        if (key->matches(QKeySequence::MoveToStartOfDocument)) {
            bar->setValue(bar->minimum());
            return true;
        }
        if (key->matches(QKeySequence::MoveToEndOfDocument)) {
            bar->setValue(bar->maximum());
            return true;
        }
    }
    return QScrollArea::eventFilter(watched, event);
}
