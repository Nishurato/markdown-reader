#include "DocumentManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>

bool DocumentManager::isMarkdownPath(const QString &path)
{
    const auto suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown");
}

bool DocumentManager::open(const QString &path, QString &error)
{
    error.clear();
    const QFileInfo info(path);
    const auto absolutePath = info.absoluteFilePath();
    if (!isMarkdownPath(path)) {
        error = tr("Выберите файл с расширением .md или .markdown.");
        return false;
    }

    QFile file(absolutePath);
    if (!info.isFile() || !file.open(QIODevice::ReadOnly)) {
        error = tr("Не удалось открыть файл «%1».").arg(QDir::toNativeSeparators(absolutePath));
        if (file.error() != QFileDevice::NoError)
            error += QStringLiteral("\n") + file.errorString();
        return false;
    }

    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = tr("Не удалось прочитать файл «%1».\n%2").arg(info.fileName(), file.errorString());
        return false;
    }

    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString markdown = decoder(bytes);
    if (decoder.hasError()) {
        error = tr("Файл «%1» должен быть сохранён в UTF-8.").arg(info.fileName());
        return false;
    }

    activePath_ = absolutePath;
    emit documentOpened(markdown, activePath_);
    return true;
}
