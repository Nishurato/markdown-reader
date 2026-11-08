#include "DocumentOutline.h"

#include <QTextBlock>
#include <QTextDocument>

DocumentOutline::DocumentOutline(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setFrameShape(QFrame::NoFrame);
    setAccessibleName(tr("Заголовки документа"));
    setUniformRowHeights(true);
    const auto navigateToItem = [this](QTreeWidgetItem *item) {
        emit headingSelected(item->data(0, Qt::UserRole).toInt());
    };
    connect(this, &QTreeWidget::itemClicked, this, navigateToItem);
    connect(this, &QTreeWidget::itemActivated, this, navigateToItem);
}

void DocumentOutline::rebuild(const QTextDocument &document)
{
    clear();
    QList<QPair<int, QTreeWidgetItem *>> parents;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        if (level < 1 || level > 6)
            continue;

        while (!parents.isEmpty() && parents.last().first >= level)
            parents.removeLast();

        auto *parent = parents.isEmpty() ? invisibleRootItem() : parents.last().second;
        auto *item = new QTreeWidgetItem(parent, {block.text()});
        item->setData(0, Qt::UserRole, block.position());
        parents.append({level, item});
    }

    expandAll();
}
