#include "DocumentManager.h"
#include "DocumentOutline.h"
#include "MainWindow.h"
#include "MarkdownRenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QFile>
#include <QImage>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QTextTable>
#include <QToolButton>
#include <QtMath>

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}

}

class ReaderTest final : public QObject
{
    Q_OBJECT

private slots:
    void opensMarkdown();
    void missingFileKeepsActiveDocument();
    void readsUtf8AndRejectsInvalidBytes();
    void findsAllHeadingLevels();
    void nestsHeadings();
    void navigatesDuplicateHeadings();
    void rendersWithoutHeadings();
    void rendersEmptyFile();
    void rendersMarkdownFeatures();
    void loadsLocalImagesOnly();
    void opensRelativeAndExternalLinks();
    void togglesAndRestoresOutline();

    void captureExternalUrl(const QUrl &url) { externalUrl_ = url; }

private:
    QUrl externalUrl_;
};

void ReaderTest::opensMarkdown()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("readme.markdown"));
    QVERIFY(writeFile(path, "# Hello"));

    DocumentManager manager;
    QSignalSpy opened(&manager, &DocumentManager::documentOpened);
    QString error;
    QVERIFY2(manager.open(path, error), qPrintable(error));
    QCOMPARE(manager.activePath(), path);
    QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.first().at(0).toString(), QStringLiteral("# Hello"));
}

void ReaderTest::missingFileKeepsActiveDocument()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("first.md"));
    QVERIFY(writeFile(path, "# First"));

    DocumentManager manager;
    QString error;
    QVERIFY(manager.open(path, error));
    QSignalSpy opened(&manager, &DocumentManager::documentOpened);
    QVERIFY(!manager.open(dir.filePath(QStringLiteral("missing.md")), error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(manager.activePath(), path);
    QVERIFY(opened.isEmpty());
}

void ReaderTest::readsUtf8AndRejectsInvalidBytes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("Пример.MD"));
    const auto markdown = QStringLiteral("# Привет, мир!");
    QVERIFY(writeFile(path, QByteArray::fromHex("efbbbf") + markdown.toUtf8()));

    DocumentManager manager;
    QSignalSpy opened(&manager, &DocumentManager::documentOpened);
    QString error;
    QVERIFY2(manager.open(path, error), qPrintable(error));
    QCOMPARE(opened.takeFirst().at(0).toString(), markdown);
    QVERIFY(writeFile(path, QByteArray::fromHex("f09f")));
    QVERIFY(!manager.open(path, error));
    QVERIFY(error.contains(QStringLiteral("UTF-8")));
    QVERIFY(opened.isEmpty());
}

void ReaderTest::findsAllHeadingLevels()
{
    MarkdownRenderer browser;
    browser.render(QStringLiteral("# A\n## B\n### C\n#### D\n##### E\n###### F\n"), {});
    DocumentOutline outline;
    outline.rebuild(*browser.document());
    QCOMPARE(outline.topLevelItemCount(), 1);
    auto *item = outline.topLevelItem(0);
    for (int level = 1; level <= 6; ++level) {
        QVERIFY(item);
        const auto block = browser.document()->findBlock(item->data(0, Qt::UserRole).toInt());
        QCOMPARE(block.blockFormat().headingLevel(), level);
        QCOMPARE(item->childCount(), level < 6 ? 1 : 0);
        item = item->child(0);
    }
}

void ReaderTest::nestsHeadings()
{
    MarkdownRenderer browser;
    browser.render(QStringLiteral("### A\n##### B\n#### C\n## D\n### E\n# F"), {});
    DocumentOutline outline;
    outline.rebuild(*browser.document());
    QCOMPARE(outline.topLevelItemCount(), 3);
    const auto *first = outline.topLevelItem(0);
    QCOMPARE(first->text(0), QStringLiteral("A"));
    QCOMPARE(first->childCount(), 2);
    QCOMPARE(first->child(0)->text(0), QStringLiteral("B"));
    QCOMPARE(first->child(1)->text(0), QStringLiteral("C"));
    QCOMPARE(outline.topLevelItem(1)->childCount(), 1);
    QCOMPARE(outline.topLevelItem(1)->child(0)->text(0), QStringLiteral("E"));
    QCOMPARE(outline.topLevelItem(2)->text(0), QStringLiteral("F"));
}

void ReaderTest::navigatesDuplicateHeadings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("duplicates.md"));
    const QByteArray filler = QByteArray("A line of text.\n\n").repeated(40);
    QVERIFY(writeFile(path, "# Repeat\n\n" + filler + "# Repeat\n\n" + filler));

    MainWindow window;
    window.resize(650, 420);
    window.show();
    QVERIFY(window.openFile(path));
    QVERIFY(window.windowTitle().contains(QStringLiteral("duplicates.md")));
    auto *browser = window.findChild<MarkdownRenderer *>();
    auto *outline = window.findChild<DocumentOutline *>();
    QCOMPARE(outline->topLevelItemCount(), 2);
    const auto *first = outline->topLevelItem(0);
    const auto *second = outline->topLevelItem(1);
    QCOMPARE(first->text(0), second->text(0));

    QTest::mouseClick(outline->viewport(), Qt::LeftButton, {},
                      outline->visualItemRect(second).center());
    const int position = second->data(0, Qt::UserRole).toInt();
    QCOMPARE(browser->textCursor().position(), position);
    const auto block = browser->document()->findBlock(position);
    const auto bounds = browser->document()->documentLayout()->blockBoundingRect(block);
    QCOMPARE(browser->verticalScrollBar()->value(), qFloor(bounds.top()));

    browser->verticalScrollBar()->setValue(0);
    QTest::mouseClick(outline->viewport(), Qt::LeftButton, {},
                      outline->visualItemRect(second).center());
    QCOMPARE(browser->verticalScrollBar()->value(), qFloor(bounds.top()));

    QTest::mouseClick(outline->viewport(), Qt::LeftButton, {},
                      outline->visualItemRect(first).center());
    QCOMPARE(browser->textCursor().position(), first->data(0, Qt::UserRole).toInt());
}

void ReaderTest::rendersWithoutHeadings()
{
    MarkdownRenderer browser;
    browser.render(QStringLiteral("Paragraph.\n\n```md\n# Inside code\n```"), {});
    DocumentOutline outline;
    outline.rebuild(*browser.document());
    QCOMPARE(outline.topLevelItemCount(), 0);
    QVERIFY(browser.toPlainText().contains(QStringLiteral("# Inside code")));
}

void ReaderTest::rendersEmptyFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto first = dir.filePath(QStringLiteral("first.md"));
    const auto empty = dir.filePath(QStringLiteral("empty.md"));
    QVERIFY(writeFile(first, "# Before"));
    QVERIFY(writeFile(empty, {}));

    MainWindow window;
    QVERIFY(window.openFile(first));
    QVERIFY(window.openFile(empty));
    QVERIFY(window.findChild<MarkdownRenderer *>()->document()->isEmpty());
    QCOMPARE(window.findChild<DocumentOutline *>()->topLevelItemCount(), 0);
    QVERIFY(window.windowTitle().contains(QStringLiteral("empty.md")));
}

void ReaderTest::rendersMarkdownFeatures()
{
    MarkdownRenderer browser;
    browser.render(QStringLiteral(
        "**Bold** and *italic* and [link](https://example.org)\n\n"
        "- Bullet\n\n1. Numbered\n\n> Quote\n\n"
        "```cpp\nint n = 1;\n```\n\n---\n\n"
        "| A | B |\n| --- | --- |\n| C | Final cell |\n"), {});

    auto *document = browser.document();
    QVERIFY(document->find(QStringLiteral("Bold")).charFormat().fontWeight() >= QFont::Bold);
    QVERIFY(document->find(QStringLiteral("italic")).charFormat().fontItalic());
    QCOMPARE(document->find(QStringLiteral("link")).charFormat().anchorHref(),
             QStringLiteral("https://example.org"));
    const auto bullet = document->find(QStringLiteral("Bullet"));
    const auto numbered = document->find(QStringLiteral("Numbered"));
    QVERIFY(bullet.currentList());
    QVERIFY(numbered.currentList());
    QCOMPARE(bullet.currentList()->format().style(), QTextListFormat::ListDisc);
    QCOMPARE(numbered.currentList()->format().style(), QTextListFormat::ListDecimal);
    QVERIFY(document->find(QStringLiteral("Quote")).blockFormat().hasProperty(QTextFormat::BlockQuoteLevel));
    QVERIFY(document->find(QStringLiteral("int n = 1;")).blockFormat().hasProperty(QTextFormat::BlockCodeLanguage));
    QVERIFY(browser.toHtml().contains(QStringLiteral("<hr")));
    const auto *table = document->find(QStringLiteral("Final cell")).currentTable();
    QVERIFY(table);
    QCOMPARE(table->rows(), 2);
    QCOMPARE(table->columns(), 2);
}

void ReaderTest::loadsLocalImagesOnly()
{
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    QImage image(16, 12, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(first.filePath(QStringLiteral("local image.png"))));
    image.fill(Qt::blue);
    QVERIFY(image.save(second.filePath(QStringLiteral("local image.png"))));

    MarkdownRenderer browser;
    const auto markdown = QStringLiteral(
        "![Local](local%20image.png) ![Remote](https://example.org/image.png) "
        "![Network share](//example.org/image.png)");
    const auto local = QUrl(QStringLiteral("local%20image.png"));
    browser.render(markdown, first.filePath(QStringLiteral("test.md")));
    QCOMPARE(browser.toHtml().count(QStringLiteral("<img")), 1);
    QVERIFY(browser.toPlainText().contains(QStringLiteral("Remote")));
    QVERIFY(browser.toPlainText().contains(QStringLiteral("Network share")));
    auto *document = browser.document();
    QCOMPARE(document->resource(QTextDocument::ImageResource, local).value<QImage>().pixelColor(0, 0),
             QColor(Qt::red));
    QVERIFY(!document->resource(QTextDocument::ImageResource,
                                QUrl(QStringLiteral("https://example.org/image.png"))).isValid());
    QVERIFY(!document->resource(QTextDocument::ImageResource,
                                QUrl(QStringLiteral("//example.org/image.png"))).isValid());

    browser.render(markdown, second.filePath(QStringLiteral("test.md")));
    QCOMPARE(browser.document()->resource(QTextDocument::ImageResource, local).value<QImage>().pixelColor(0, 0),
             QColor(Qt::blue));
    QCOMPARE(browser.findChildren<QTextDocument *>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
}

void ReaderTest::opensRelativeAndExternalLinks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto first = dir.filePath(QStringLiteral("first.md"));
    const auto next = dir.filePath(QStringLiteral("второй файл.markdown"));
    QVERIFY(writeFile(first, "# First"));
    QVERIFY(writeFile(next, "# Next"));

    MainWindow window;
    QVERIFY(window.openFile(first));
    auto *browser = window.findChild<MarkdownRenderer *>();
    const QUrl local(QStringLiteral("%D0%B2%D1%82%D0%BE%D1%80%D0%BE%D0%B9%20%D1%84%D0%B0%D0%B9%D0%BB.markdown"));
    emit browser->anchorClicked(local);
    QCOMPARE(browser->toPlainText(), QStringLiteral("Next"));

    const QUrl external(QStringLiteral("https://example.org/test"));
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "captureExternalUrl");
    emit browser->anchorClicked(external);
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    QCOMPARE(externalUrl_, external);
    emit browser->anchorClicked(QUrl(QStringLiteral("unknown:invalid")));
    QCOMPARE(browser->toPlainText(), QStringLiteral("Next"));
}

void ReaderTest::togglesAndRestoresOutline()
{
    MainWindow window;
    window.show();
    auto *outline = window.findChild<DocumentOutline *>();
    auto *browser = window.findChild<MarkdownRenderer *>();
    auto *splitter = window.findChild<QSplitter *>();
    auto *toggle = window.findChild<QToolButton *>(QStringLiteral("outlineToggle"));
    QVERIFY(toggle);
    splitter->setSizes({300, splitter->width() - splitter->handleWidth() - 300});
    const auto before = splitter->sizes();
    const auto readingWidth = browser->width();

    QTest::mouseClick(toggle, Qt::LeftButton);
    QVERIFY(outline->isHidden());
    QTRY_VERIFY(browser->width() > readingWidth);
    QTest::mouseClick(toggle, Qt::LeftButton);
    QVERIFY(outline->isVisible());
    QTRY_COMPARE(splitter->sizes(), before);

    window.resize(window.width() + 150, window.height() + 80);
    QTRY_VERIFY(browser->width() > readingWidth);
}

QTEST_MAIN(ReaderTest)
#include "ReaderTest.moc"
