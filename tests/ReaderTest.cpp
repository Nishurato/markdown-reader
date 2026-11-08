#include "AnchorResolver.h"
#include "DocumentManager.h"
#include "DocumentOutline.h"
#include "DocumentTab.h"
#include "MainWindow.h"
#include "MarkdownRenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QTextTable>
#include <QTimer>
#include <QToolButton>
#include <QtMath>

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}

DocumentTab *activeTab(MainWindow &window)
{
    return qobject_cast<DocumentTab *>(window.findChild<QStackedWidget *>()->currentWidget());
}

}

class ReaderTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }
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
    void createsAndClosesTabs();
    void switchesIndependentDocuments();
    void keepsFixedPageWidth();
    void showsStartPage();
    void opensFromStartPage();
    void keepsStartPageAfterCancel();
    void normalizesAnchorSlugs();
    void followsAnchorsInCurrentTab();
    void followsAnchorInAnotherFile();
    void reportsMissingAnchors();

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
    auto *tab = activeTab(window);
    auto *browser = tab->renderer();
    auto *outline = window.findChild<DocumentOutline *>();
    QTest::mouseClick(window.findChild<QToolButton *>(QStringLiteral("outlineToggle")), Qt::LeftButton);
    QCOMPARE(outline->topLevelItemCount(), 2);
    const auto *first = outline->topLevelItem(0);
    const auto *second = outline->topLevelItem(1);
    QCOMPARE(first->text(0), second->text(0));
    QTRY_VERIFY(tab->verticalScrollBar()->maximum() > 0);

    QTest::mouseClick(outline->viewport(), Qt::LeftButton, {},
                      outline->visualItemRect(second).center());
    const int position = second->data(0, Qt::UserRole).toInt();
    QCOMPARE(browser->textCursor().position(), position);
    const auto block = browser->document()->findBlock(position);
    const auto bounds = browser->document()->documentLayout()->blockBoundingRect(block);
    const int scrollPosition = qFloor(bounds.top()) + browser->y();
    QCOMPARE(tab->verticalScrollBar()->value(), scrollPosition);

    tab->verticalScrollBar()->setValue(0);
    QTest::mouseClick(outline->viewport(), Qt::LeftButton, {},
                      outline->visualItemRect(second).center());
    QCOMPARE(tab->verticalScrollBar()->value(), scrollPosition);

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
    QVERIFY(activeTab(window)->renderer()->document()->isEmpty());
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
    auto *browser = activeTab(window)->renderer();
    const QUrl local(QStringLiteral("%D0%B2%D1%82%D0%BE%D1%80%D0%BE%D0%B9%20%D1%84%D0%B0%D0%B9%D0%BB.markdown"));
    emit browser->anchorClicked(local);
    QCOMPARE(browser->toPlainText(), QStringLiteral("First"));
    browser = activeTab(window)->renderer();
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
    auto *splitter = window.findChild<QSplitter *>();
    auto *toggle = window.findChild<QToolButton *>(QStringLiteral("outlineToggle"));
    QVERIFY(toggle);
    QVERIFY(outline->isHidden());
    QVERIFY(!toggle->isChecked());
    const int collapsedReadingWidth = activeTab(window)->width();
    QTest::mouseClick(toggle, Qt::LeftButton);
    QVERIFY(outline->isVisible());
    QVERIFY(toggle->isChecked());
    splitter->setSizes({300, splitter->width() - splitter->handleWidth() - 300});
    const auto before = splitter->sizes();

    QTest::mouseClick(toggle, Qt::LeftButton);
    QVERIFY(outline->isHidden());
    QVERIFY(!toggle->isChecked());
    QTRY_COMPARE(activeTab(window)->width(), collapsedReadingWidth);
    QTest::mouseClick(toggle, Qt::LeftButton);
    QVERIFY(outline->isVisible());
    QTRY_COMPARE(splitter->sizes(), before);
}

void ReaderTest::createsAndClosesTabs()
{
    MainWindow window;
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    auto *tabs = window.findChild<QTabBar *>();
    auto *button = window.findChild<QToolButton *>(QStringLiteral("newTabButton"));
    QVERIFY(tabs->isMovable());
    QVERIFY(tabs->tabsClosable());
    QCOMPARE(tabs->count(), 1);
    const QPointer<DocumentTab> first = activeTab(window);

    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(tabs->count(), 2);
    const QPointer<DocumentTab> second = activeTab(window);
    QVERIFY(second && second != first);
    QVERIFY(second->findChild<QPushButton *>(QStringLiteral("openFileButton")));
    QVERIFY(!second->renderer());
    QTest::keyClick(second->findChild<QPushButton *>(), Qt::Key_T, Qt::ControlModifier);
    QTRY_COMPARE(tabs->count(), 3);
    const QPointer<DocumentTab> third = activeTab(window);
    QTest::keyClick(third->findChild<QPushButton *>(), Qt::Key_W, Qt::ControlModifier);
    QTRY_COMPARE(tabs->count(), 2);
    QVERIFY(third.isNull());

    emit tabs->tabCloseRequested(0);
    QVERIFY(first.isNull());
    QCOMPARE(activeTab(window), second.data());
    emit tabs->tabCloseRequested(0);
    QVERIFY(second.isNull());
    QVERIFY(window.isVisible());
    QCOMPARE(tabs->count(), 1);
    QVERIFY(activeTab(window)->path().isEmpty());
    QVERIFY(!activeTab(window)->renderer());
    QVERIFY(activeTab(window)->findChild<QPushButton *>(QStringLiteral("openFileButton")));
}

void ReaderTest::switchesIndependentDocuments()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto first = dir.filePath(QStringLiteral("first.md"));
    const auto second = dir.filePath(QStringLiteral("second.md"));
    const QByteArray filler = QByteArray("A line of text.\n\n").repeated(120);
    QVERIFY(writeFile(first, "# First\n\n" + filler));
    QVERIFY(writeFile(second, "# Second\n\n" + filler));

    MainWindow window;
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    auto *tabs = window.findChild<QTabBar *>();
    auto *outline = window.findChild<DocumentOutline *>();
    QVERIFY(window.openFile(first));
    auto *firstTab = activeTab(window);
    auto *firstDocument = firstTab->renderer()->document();
    QTRY_VERIFY(firstTab->verticalScrollBar()->maximum() > 420);
    firstTab->verticalScrollBar()->setValue(420);
    QCOMPARE(tabs->tabText(0), QStringLiteral("first.md"));
    QCOMPARE(tabs->tabToolTip(0), QDir::toNativeSeparators(first));

    QVERIFY(window.openFile(second));
    auto *secondTab = activeTab(window);
    QVERIFY(secondTab->renderer()->document() != firstDocument);
    QTRY_VERIFY(secondTab->verticalScrollBar()->maximum() > 150);
    secondTab->verticalScrollBar()->setValue(150);
    QCOMPARE(outline->topLevelItem(0)->text(0), QStringLiteral("Second"));
    QVERIFY(QFile::remove(first));

#ifdef Q_OS_MACOS
    constexpr auto tabModifier = Qt::MetaModifier;
#else
    constexpr auto tabModifier = Qt::ControlModifier;
#endif
    QTest::keyClick(secondTab->renderer(), Qt::Key_Tab, tabModifier);
    QTRY_COMPARE(activeTab(window), firstTab);
    QCOMPARE(firstTab->renderer()->document(), firstDocument);
    QVERIFY(firstTab->renderer()->toPlainText().startsWith(QStringLiteral("First")));
    QCOMPARE(firstTab->verticalScrollBar()->value(), 420);
    QCOMPARE(outline->topLevelItem(0)->text(0), QStringLiteral("First"));

    QTest::keyClick(firstTab->renderer(), Qt::Key_Tab, tabModifier | Qt::ShiftModifier);
    QTRY_COMPARE(activeTab(window), secondTab);
    QCOMPARE(secondTab->verticalScrollBar()->value(), 150);
    QCOMPARE(outline->topLevelItem(0)->text(0), QStringLiteral("Second"));

    tabs->moveTab(1, 0);
    QCOMPARE(activeTab(window), secondTab);
    QCOMPARE(tabs->tabText(0), QStringLiteral("second.md"));
    QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(1).center());
    QCOMPARE(activeTab(window), firstTab);
    QCOMPARE(outline->topLevelItem(0)->text(0), QStringLiteral("First"));
}

void ReaderTest::keepsFixedPageWidth()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("page.md"));
    QImage image(1400, 70, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath(QStringLiteral("wide.png"))));
    QVERIFY(writeFile(path, "![Wide](wide.png)\n\n" + QByteArray("A line of text.\n\n").repeated(150)));
    MainWindow window;
    window.resize(1200, 550);
    window.show();
    QVERIFY(window.openFile(path));
    auto *tab = activeTab(window);
    auto *browser = tab->renderer();
    QTRY_COMPARE(browser->width(), 800);
    QTRY_COMPARE(browser->document()->textWidth(), 800.0);
    QCOMPARE(browser->document()->documentMargin(), 40.0);
    QVERIFY(qAbs(tab->widget()->geometry().center().x() - tab->viewport()->rect().center().x()) <= 1);
    QTRY_VERIFY(tab->verticalScrollBar()->maximum() > 0);
    QCOMPARE(browser->verticalScrollBar()->maximum(), 0);
    QCOMPARE(browser->horizontalScrollBar()->maximum(), 0);

    QTest::keyClick(browser, Qt::Key_PageDown);
    QVERIFY(tab->verticalScrollBar()->value() > 0);
    window.resize(600, 400);
    QTRY_VERIFY(tab->horizontalScrollBar()->maximum() > 0);
    QCOMPARE(browser->width(), 800);
    QCOMPARE(browser->document()->textWidth(), 800.0);
}

void ReaderTest::showsStartPage()
{
    MainWindow window;
    window.show();
    auto *tab = activeTab(window);
    QVERIFY(tab);
    QCOMPARE(window.findChild<QTabBar *>()->count(), 1);
    QVERIFY(!window.findChild<QMenuBar *>());
    QVERIFY(!tab->renderer());
    QCOMPARE(tab->widget()->objectName(), QStringLiteral("startPage"));
    QVERIFY(tab->findChild<QPushButton *>(QStringLiteral("openFileButton"))->isVisible());
    const auto shortcut = QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText);
    QVERIFY(tab->findChild<QLabel *>(QStringLiteral("startPageHint"))->text().contains(shortcut));
}

void ReaderTest::opensFromStartPage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("opened.md"));
    QVERIFY(writeFile(path, "# Opened"));

    MainWindow window;
    window.show();
    auto *tab = activeTab(window);
    const QPointer<QWidget> startPage = tab->widget();
    auto *button = tab->findChild<QPushButton *>(QStringLiteral("openFileButton"));
    bool sawDialog = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            QApplication::closeAllWindows();
            return;
        }
        sawDialog = true;
        dialog->selectFile(path);
        QMetaObject::invokeMethod(dialog, "accept");
        if (dialog->isVisible())
            dialog->reject();
    });
    QTest::mouseClick(button, Qt::LeftButton);

    QVERIFY(sawDialog);
    QCOMPARE(window.findChild<QTabBar *>()->count(), 1);
    QCOMPARE(activeTab(window), tab);
    QCOMPARE(tab->path(), path);
    QCOMPARE(tab->renderer()->toPlainText(), QStringLiteral("Opened"));
    QTRY_VERIFY(startPage.isNull());
}

void ReaderTest::keepsStartPageAfterCancel()
{
    MainWindow window;
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    auto *tab = activeTab(window);
    auto *page = tab->widget();
    auto *button = tab->findChild<QPushButton *>(QStringLiteral("openFileButton"));
    bool sawDialog = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            QApplication::closeAllWindows();
            return;
        }
        sawDialog = true;
        dialog->reject();
    });
    QTest::keyClick(button, Qt::Key_O, Qt::ControlModifier);

    QVERIFY(sawDialog);
    QCOMPARE(activeTab(window), tab);
    QCOMPARE(tab->widget(), page);
    QVERIFY(!tab->renderer());
    QVERIFY(button->isVisible());
}

void ReaderTest::normalizesAnchorSlugs()
{
    QCOMPARE(AnchorResolver::slugForHeading(QStringLiteral("  Café  Setup!  ")),
             QStringLiteral("café--setup"));
    QCOMPARE(AnchorResolver::slugForHeading(QStringLiteral("УСТАНОВКА — шаг_1")),
             QStringLiteral("установка--шаг_1"));
    QCOMPARE(AnchorResolver::normalizeFragment(QStringLiteral("CAFE\u0301")),
             QStringLiteral("café"));

    MarkdownRenderer browser;
    browser.render(QStringLiteral("# Section\n## Section-1\n### Section\n#### Section\n"
                                  "##### Fifth\n###### Sixth\n\nPlain text"), {});
    const auto index = AnchorResolver::indexHeadings(*browser.document());
    const QStringList slugs{QStringLiteral("section"), QStringLiteral("section-1"),
                            QStringLiteral("section-2"), QStringLiteral("section-3"),
                            QStringLiteral("fifth"), QStringLiteral("sixth")};
    QCOMPARE(index.size(), slugs.size());
    int next = 0;
    for (auto block = browser.document()->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() > 0)
            QCOMPARE(index.value(slugs.at(next++), -1), block.position());
    }
    QCOMPARE(next, slugs.size());
    QVERIFY(!index.contains(QStringLiteral("plain-text")));
}

void ReaderTest::followsAnchorsInCurrentTab()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("anchors.md"));
    const QByteArray filler = QByteArray("Filler paragraph.\n\n").repeated(80);
    QVERIFY(writeFile(path, "# Home\n\n" + filler + "# Install Guide\n\n" + filler
                           + QStringLiteral("## Установка\n\n").toUtf8() + filler
                           + "## Repeat\n\n" + filler + "## Repeat\n\n" + filler));

    MainWindow window;
    window.show();
    QVERIFY(window.openFile(path));
    auto *tab = activeTab(window);
    auto *browser = tab->renderer();
    auto *document = browser->document();
    QVERIFY(QFile::remove(path));

    emit browser->anchorClicked(QUrl(QStringLiteral("#INSTALL-GUIDE")));
    QTRY_COMPARE(browser->textCursor().block().text(), QStringLiteral("Install Guide"));
    QVERIFY(tab->verticalScrollBar()->value() > 0);

    emit browser->anchorClicked(QUrl(QStringLiteral("#%D0%A3%D0%A1%D0%A2%D0%90%D0%9D%D0%9E%D0%92%D0%9A%D0%90")));
    QTRY_COMPARE(browser->textCursor().block().text(), QStringLiteral("Установка"));

    const auto first = document->find(QStringLiteral("Repeat"));
    const auto second = document->find(QStringLiteral("Repeat"), first);
    QVERIFY(!second.isNull());
    emit browser->anchorClicked(QUrl(QStringLiteral("#repeat-1")));
    QTRY_COMPARE(browser->textCursor().position(), second.block().position());

    emit browser->anchorClicked(QUrl(QStringLiteral("#")));
    QTRY_COMPARE(tab->verticalScrollBar()->value(), 0);
    QCOMPARE(window.findChild<QTabBar *>()->count(), 1);
    QCOMPARE(activeTab(window), tab);
    QCOMPARE(browser->document(), document);
}

void ReaderTest::followsAnchorInAnotherFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto source = dir.filePath(QStringLiteral("source.md"));
    const auto target = dir.filePath(QStringLiteral("guide file.markdown"));
    const QByteArray filler = QByteArray("Filler paragraph.\n\n").repeated(80);
    QVERIFY(writeFile(source, "# Source\n\n" + filler));
    QVERIFY(writeFile(target, "# Guide\n\n" + filler + "## Installation\n\n" + filler));

    MainWindow window;
    window.show();
    QVERIFY(window.openFile(source));
    auto *sourceTab = activeTab(window);
    QTRY_VERIFY(sourceTab->verticalScrollBar()->maximum() > 300);
    sourceTab->verticalScrollBar()->setValue(300);
    emit sourceTab->renderer()->anchorClicked(QUrl(QStringLiteral("guide%20file.markdown#installation")));

    auto *tabs = window.findChild<QTabBar *>();
    QCOMPARE(tabs->count(), 2);
    auto *targetTab = activeTab(window);
    QVERIFY(targetTab != sourceTab);
    QCOMPARE(targetTab->path(), target);
    QTRY_COMPARE(targetTab->renderer()->textCursor().block().text(), QStringLiteral("Installation"));
    QVERIFY(targetTab->verticalScrollBar()->value() > 0);

    const int targetScroll = targetTab->verticalScrollBar()->value();
    tabs->setCurrentIndex(0);
    QCOMPARE(activeTab(window), sourceTab);
    QCOMPARE(sourceTab->verticalScrollBar()->value(), 300);
    tabs->setCurrentIndex(1);
    QCOMPARE(targetTab->verticalScrollBar()->value(), targetScroll);
}

void ReaderTest::reportsMissingAnchors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto source = dir.filePath(QStringLiteral("source.md"));
    const auto target = dir.filePath(QStringLiteral("target.md"));
    const QByteArray filler = QByteArray("Missing anchor.\n\n").repeated(80);
    QVERIFY(writeFile(source, "# Source\n\n" + filler));
    QVERIFY(writeFile(target, "# Target"));

    MainWindow window;
    window.show();
    QVERIFY(window.openFile(source));
    auto *tab = activeTab(window);
    auto *tabs = window.findChild<QTabBar *>();
    QTRY_VERIFY(tab->verticalScrollBar()->maximum() > 300);
    tab->verticalScrollBar()->setValue(300);
    emit tab->renderer()->anchorClicked(QUrl(QStringLiteral("#missing-anchor")));
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(activeTab(window), tab);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("missing-anchor")));
    QCOMPARE(tab->verticalScrollBar()->value(), 300);

    emit tab->renderer()->anchorClicked(QUrl(QStringLiteral("target.md#other-missing")));
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(activeTab(window)->path(), target);
    QCOMPARE(activeTab(window)->verticalScrollBar()->value(), 0);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("other-missing")));
    QCOMPARE(tab->verticalScrollBar()->value(), 300);
}

QTEST_MAIN(ReaderTest)
#include "ReaderTest.moc"
