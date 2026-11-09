#include "MarkdownRenderer.h"

#include <QDir>
#include <QImage>
#include <QPointer>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QVariant>

#ifdef Q_OS_LINUX
#include <QApplication>
#include <QEvent>
#include <cmath>
#endif

namespace {

#ifdef Q_OS_LINUX
double luminance(const QColor &color)
{
    const auto linear = [](double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF())
        + 0.0722 * linear(color.blueF());
}

double contrast(const QColor &first, const QColor &second)
{
    const auto a = luminance(first) + 0.05;
    const auto b = luminance(second) + 0.05;
    return qMax(a, b) / qMin(a, b);
}
#endif

bool isLocalImage(const QUrl &url)
{
    return url.isLocalFile() && url.host().isEmpty()
        && !QDir::fromNativeSeparators(url.toLocalFile()).startsWith(QStringLiteral("//"));
}

class LocalMarkdownDocument final : public QTextDocument
{
public:
    using QTextDocument::QTextDocument;

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        const QUrl url = baseUrl().resolved(name);
        if (type != ImageResource || !isLocalImage(url)) {
            return {};
        }

        const QImage image(url.toLocalFile());
        if (image.isNull())
            return {};

        const auto resource = QVariant::fromValue(image);
        addResource(type, name, resource);
        return resource;
    }
};

void prepareInlineContent(QTextDocument &document)
{
    struct RemoteImage {
        int position;
        int length;
        QString alt;
    };
    struct InlineFormat {
        int position;
        int length;
        QTextCharFormat format;
    };
    QTextCharFormat linkStyle;
    linkStyle.setForeground(QColor(QStringLiteral("#1b55a0")));
    linkStyle.setFontUnderline(true);

    QList<RemoteImage> remoteImages;
    QList<InlineFormat> formats;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto format = fragment.charFormat();
            if (format.isImageFormat()) {
                if (isLocalImage(document.baseUrl().resolved(QUrl(format.toImageFormat().name())))) {
                    auto image = format.toImageFormat();
                    image.setMaximumWidth(QTextLength(QTextLength::PercentageLength, 100));
                    formats.append({fragment.position(), fragment.length(), image});
                } else {
                    remoteImages.append({fragment.position(), fragment.length(),
                                         format.stringProperty(QTextFormat::ImageAltText)});
                }
            } else if (format.isAnchor()) {
                formats.append({fragment.position(), fragment.length(), linkStyle});
            }
        }
    }

    QTextCursor cursor(&document);
    for (const auto &[position, length, format] : formats) {
        cursor.setPosition(position);
        cursor.setPosition(position + length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(format);
    }

    // Replace from the end so edits cannot move the remaining image positions.
    for (auto it = remoteImages.crbegin(); it != remoteImages.crend(); ++it) {
        cursor.setPosition(it->position);
        cursor.setPosition(it->position + it->length, QTextCursor::KeepAnchor);
        cursor.insertText(it->alt, QTextCharFormat());
    }
}

}

MarkdownRenderer::MarkdownRenderer(QWidget *parent)
    : QTextBrowser(parent)
{
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral("QTextBrowser { background: #fafafa; color: #242424; }"));
#ifdef Q_OS_LINUX
    updateSelectionPalette();
#endif
}

#ifdef Q_OS_LINUX
void MarkdownRenderer::changeEvent(QEvent *event)
{
    QTextBrowser::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        updateSelectionPalette();
}

void MarkdownRenderer::updateSelectionPalette()
{
    const auto theme = QApplication::palette(this);
    const auto current = palette();
    QPalette adjusted;
    bool changed = false;
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        auto text = theme.color(group, QPalette::HighlightedText);
        const auto background = current.color(group, QPalette::Highlight);
        // Qt also uses this selection brush to draw list markers.
        if (group != QPalette::Disabled && contrast(text, background) < 4.5) {
            text = current.color(group, QPalette::Text);
            if (contrast(text, background) < 4.5)
                text = contrast(Qt::black, background) > contrast(Qt::white, background)
                    ? Qt::black : Qt::white;
        }
        adjusted.setColor(group, QPalette::HighlightedText, text);
        changed |= current.color(group, QPalette::HighlightedText) != text;
    }
    if (changed)
        setPalette(adjusted);
}
#endif

void MarkdownRenderer::render(const QString &markdown, const QString &path)
{
    // A fresh document scopes Qt's image cache to the file being displayed.
    auto *next = new LocalMarkdownDocument(this);
    auto readingFont = font();
    readingFont.setPointSize(qMax(11, readingFont.pointSize()));
    next->setDefaultFont(readingFont);
    next->setDocumentMargin(40);
    next->setUndoRedoEnabled(false);
    next->setBaseUrl(QUrl::fromLocalFile(path));
    // Qt's image layout can try to open the path directly if resource loading fails.
    next->setLayoutEnabled(false);
    next->setMarkdown(markdown, {QTextDocument::MarkdownDialectGitHub, QTextDocument::MarkdownNoHTML});
    prepareInlineContent(*next);
    next->setLayoutEnabled(true);
    const QPointer<QTextDocument> previous = document();
    const bool ownsPrevious = previous->parent() == this;
    setDocument(next);
    if (ownsPrevious && previous)
        delete previous.data();
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
}

QUrl MarkdownRenderer::resolveLink(const QUrl &link) const
{
    return document()->baseUrl().resolved(link);
}
