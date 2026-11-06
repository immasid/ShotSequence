#include "reorderable_table.h"

ReorderableTableWidget::ReorderableTableWidget(int rows, int columns, QWidget *parent)
    : QTableWidget(rows, columns, parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDragDropOverwriteMode(false);
}

void ReorderableTableWidget::dropEvent(QDropEvent *event) {
    QTableWidget::dropEvent(event);
    QTimer::singleShot(0, this, &ReorderableTableWidget::emitOrder);
}

void ReorderableTableWidget::emitOrder() {
    QStringList paths;
    for (int row = 0; row < rowCount(); ++row) {
        auto it = item(row, 0);
        if (it) {
            QString path = it->data(Qt::UserRole).toString();
            if (!path.isEmpty()) {
                paths.append(path);
            }
        }
    }
    if (!paths.isEmpty()) {
        emit order_changed(paths);
    }
}
