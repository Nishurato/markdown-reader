#include "DocumentTab.h"

#include "MarkdownRenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QScrollBar>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QtMath>

namespace {
constexpr int pageWidth = 800;
constexpr int pageGutter = 24;
}

DocumentTab::DocumentTab(QWidget *parent)
    : QScrollArea(parent), renderer_(new MarkdownRenderer)
{
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setStyleSheet(QStringLiteral("DocumentTab, #documentCanvas { background: #dedede; }"));

    auto *canvas = new QWidget;
    canvas->setObjectName(QStringLiteral("documentCanvas"));
    auto *layout = new QVBoxLayout(canvas);
    layout->setContentsMargins(pageGutter, pageGutter, pageGutter, pageGutter);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    renderer_->setFixedWidth(pageWidth);
    renderer_->setLineWrapMode(QTextEdit::FixedPixelWidth);
    renderer_->setLineWrapColumnOrWidth(pageWidth);
    renderer_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    renderer_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    renderer_->installEventFilter(this);
    layout->addWidget(renderer_);
    setWidget(canvas);

    const auto resizePage = [this, layout](const QSizeF &size) {
        renderer_->setFixedHeight(qCeil(size.height()));
        layout->activate();
    };
    connect(&manager_, &DocumentManager::documentOpened, this,
            [this, resizePage](const QString &markdown, const QString &path) {
        renderer_->render(markdown, path);
        auto *documentLayout = renderer_->document()->documentLayout();
        connect(documentLayout, &QAbstractTextDocumentLayout::documentSizeChanged, this, resizePage);
        resizePage(documentLayout->documentSize());
        verticalScrollBar()->setValue(0);
        horizontalScrollBar()->setValue(0);
    });

    renderer_->document()->setDocumentMargin(40);
    renderer_->setPlainText(tr("Откройте Markdown-документ через меню «Файл»."));
    resizePage(renderer_->document()->size());
}

void DocumentTab::navigateTo(int position)
{
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
