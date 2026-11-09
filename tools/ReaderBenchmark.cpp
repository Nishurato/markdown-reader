#include "MainWindow.h"
#include "MarkdownRenderer.h"

#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <cstdio>

#if defined(Q_OS_WIN)
#include <QtCore/qt_windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace {

class FirstPaint final : public QObject
{
public:
    bool wait(QWidget *widget)
    {
        if (!widget || !widget->isVisible())
            return false;

        observed_ = false;
        widget->installEventFilter(this);
        widget->update();
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &loop_, &QEventLoop::quit);
        timeout.start(15000);
        loop_.exec();
        widget->removeEventFilter(this);
        return observed_;
    }

private:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint && !observed_) {
            observed_ = true;
            // The widget handles this paint before control returns to our timer.
            QTimer::singleShot(0, &loop_, &QEventLoop::quit);
        }
        return false;
    }

    QEventLoop loop_;
    bool observed_ = false;
};

qint64 residentBytes()
{
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<qint64>(counters.WorkingSetSize);
#elif defined(Q_OS_MACOS)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<qint64>(info.resident_size);
#elif defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/self/statm"));
    if (file.open(QIODevice::ReadOnly)) {
        const auto fields = file.readLine().simplified().split(' ');
        bool ok = false;
        const auto pages = fields.value(1).toLongLong(&ok);
        const long pageSize = sysconf(_SC_PAGESIZE);
        if (ok && pageSize > 0)
            return pages * pageSize;
    }
#endif
    return -1;
}

qint64 settledResidentBytes()
{
    QEventLoop settle;
    QTimer::singleShot(250, &settle, &QEventLoop::quit);
    settle.exec();
    return residentBytes();
}

double milliseconds(const QElapsedTimer &timer)
{
    return timer.nsecsElapsed() / 1000000.0;
}

int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

}

int main(int argc, char *argv[])
{
    QElapsedTimer timer;
    timer.start();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Markdown Reader"));
    const auto arguments = app.arguments();
    if (arguments.size() != 2 || !QFileInfo(arguments.at(1)).isFile())
        return fail("Usage: markdown_reader_benchmark <existing Markdown file>");

    MainWindow window;
    FirstPaint paint;
    window.show();
    if (!paint.wait(window.findChild<QPushButton *>(QStringLiteral("openFileButton"))))
        return fail("Start page did not paint");

    QJsonObject metrics{{QStringLiteral("startup_ms"), milliseconds(timer)},
                        {QStringLiteral("first_paint_epoch_ms"), QDateTime::currentMSecsSinceEpoch()}};
    metrics.insert(QStringLiteral("empty_rss_bytes"), settledResidentBytes());
    auto *pages = window.findChild<QStackedWidget *>();
    auto *tabs = window.findChild<QTabBar *>();
    const auto openAndPaint = [&] {
        if (!window.openFile(arguments.at(1)))
            return false;
        auto *reader = pages->currentWidget()->findChild<MarkdownRenderer *>();
        return reader && paint.wait(reader->viewport());
    };

    timer.restart();
    if (!openAndPaint())
        return fail("Document did not open and paint");
    metrics.insert(QStringLiteral("open_to_paint_ms"), milliseconds(timer));
    metrics.insert(QStringLiteral("one_tab_rss_bytes"), settledResidentBytes());

    for (int i = 1; i < 5; ++i) {
        if (!openAndPaint())
            return fail("Additional document did not paint");
    }
    if (tabs->count() != 5)
        return fail("Expected five document tabs");
    metrics.insert(QStringLiteral("five_tabs_rss_bytes"), settledResidentBytes());

    for (int i = 0; i < 5; ++i)
        emit tabs->tabCloseRequested(tabs->currentIndex());
    if (tabs->count() != 1
        || !paint.wait(pages->currentWidget()->findChild<QPushButton *>(QStringLiteral("openFileButton"))))
        return fail("Start page did not return after closing the documents");
    metrics.insert(QStringLiteral("closed_tabs_rss_bytes"), settledResidentBytes());

    const auto output = QJsonDocument(metrics).toJson(QJsonDocument::Compact);
    std::puts(output.constData());
}
