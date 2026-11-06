#ifndef REORDERABLE_TABLE_H
#define REORDERABLE_TABLE_H

#include <QTableWidget>
#include <QStringList>
#include <QTimer>
#include <QDropEvent>

class ReorderableTableWidget : public QTableWidget {
    Q_OBJECT
public:
    explicit ReorderableTableWidget(int rows = 0, int columns = 0, QWidget *parent = nullptr);

signals:
    void order_changed(const QStringList &paths);

protected:
    void dropEvent(QDropEvent *event) override;

private slots:
    void emitOrder();
};

#endif // REORDERABLE_TABLE_H
