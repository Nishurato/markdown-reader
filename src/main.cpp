#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Markdown Reader"));
    QApplication::setApplicationVersion(QStringLiteral(MARKDOWN_READER_VERSION));

    MainWindow window;
    window.show();
    const auto args = app.arguments();
    if (args.size() > 1)
        window.openFile(args.at(1));

    return app.exec();
}
