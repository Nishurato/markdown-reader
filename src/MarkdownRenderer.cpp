#include "MarkdownRenderer.h"

#include <QDir>
#include <QImage>
#include <QPointer>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QVariant>

namespace {

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
    QList<RemoteImage> remoteImages;
    QList<QPair<int, int>> links;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto format = fragment.charFormat();
            if (format.isImageFormat()
                && !isLocalImage(document.baseUrl().resolved(QUrl(format.toImageFormat().name())))) {
                remoteImages.append({fragment.position(), fragment.length(),
                                     format.stringProperty(QTextFormat::ImageAltText)});
            } else if (format.isAnchor()) {
                links.append({fragment.position(), fragment.length()});
            }
        }
    }

    QTextCursor cursor(&document);
    QTextCharFormat linkStyle;
    linkStyle.setForeground(QColor(QStringLiteral("#1b55a0")));
    linkStyle.setFontUnderline(true);
    for (const auto &[position, length] : links) {
        cursor.setPosition(position);
        cursor.setPosition(position + length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(linkStyle);
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
}

void MarkdownRenderer::render(const QString &markdown, const QString &path)
{
    // A fresh document scopes Qt's image cache to the file being displayed.
    auto *next = new LocalMarkdownDocument(this);
    auto readingFont = font();
    readingFont.setPointSize(qMax(11, readingFont.pointSize()));
    next->setDefaultFont(readingFont);
    next->setDocumentMargin(28);
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
