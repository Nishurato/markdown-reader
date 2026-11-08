#include "AnchorResolver.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>

namespace AnchorResolver {

QString normalizeFragment(const QString &fragment)
{
    return fragment.toLower().normalized(QString::NormalizationForm_C);
}

QString slugForHeading(const QString &heading)
{
    static const QRegularExpression punctuation(
        QStringLiteral(R"([^\p{L}\p{M}\p{N}_\s-])"),
        QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression whitespace(
        QStringLiteral(R"(\s)"), QRegularExpression::UseUnicodePropertiesOption);
    auto slug = normalizeFragment(heading.trimmed());
    slug.remove(punctuation);
    return slug.replace(whitespace, QStringLiteral("-"));
}

QHash<QString, int> indexHeadings(const QTextDocument &document)
{
    QHash<QString, int> anchors;
    QHash<QString, int> suffixes;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        if (level < 1 || level > 6)
            continue;

        const auto base = slugForHeading(block.text());
        if (base.isEmpty())
            continue;

        auto anchor = base;
        if (anchors.contains(anchor)) {
            auto &suffix = suffixes[base];
            do {
                anchor = base + u'-' + QString::number(++suffix);
            } while (anchors.contains(anchor));
        }
        anchors.insert(anchor, block.position());
    }
    return anchors;
}

}
