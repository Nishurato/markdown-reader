#pragma once

#include <QHash>
#include <QString>

class QTextDocument;

namespace AnchorResolver {

QString slugForHeading(const QString &heading);
QString normalizeFragment(const QString &fragment);
QHash<QString, int> indexHeadings(const QTextDocument &document);

}
