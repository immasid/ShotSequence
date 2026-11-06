#ifndef LAUNCHER_WINDOW_H
#define LAUNCHER_WINDOW_H

#include <QMainWindow>
#include <QStringList>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QThread>
#include <QPointer>
#include "common.h"

class ReviewWindow;

class ScanWorker : public QThread {
    Q_OBJECT
public:
    explicit ScanWorker(const QStringList &folders, QObject *parent = nullptr);
    void run() override;

signals:
    void completed(const QList<PhotoRecord> &original, const QList<PhotoRecord> &proposed, const QStringList &warnings);
    void failed(const QString &message);

private:
    QStringList m_folders;
};

class LauncherWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LauncherWindow(QWidget *parent = nullptr);
    ~LauncherWindow() override;

private slots:
    void addFolder();
    void removeSelected();
    void startScan();
    void scanCompleted(const QList<PhotoRecord> &original, const QList<PhotoRecord> &proposed, const QStringList &warnings);
    void scanFailed(const QString &message);

private:
    void updateFolderCount();
    void restoreControls();

    QStringList m_folders;
    QPointer<ScanWorker> m_worker;
    ReviewWindow *m_reviewWindow = nullptr;

    QLabel *m_folderCount = nullptr;
    QListWidget *m_folderList = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_scanButton = nullptr;
    QProgressBar *m_progress = nullptr;
};

#endif // LAUNCHER_WINDOW_H
