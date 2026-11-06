#ifndef PREVIEW_WORKER_H
#define PREVIEW_WORKER_H

#include <QRunnable>
#include <QObject>
#include <QImage>
#include <QSize>
#include <QPointer>
#include <memory>
#include <atomic>

class PreviewSignals : public QObject {
    Q_OBJECT
public:
    explicit PreviewSignals(QObject *parent = nullptr) : QObject(parent) {}
signals:
    void ready(const QString &path, const QImage &image, const QString &error);
};

class PreviewWorker : public QRunnable {
public:
    PreviewWorker(const QString &path, const QSize &targetSize, PreviewSignals *previewSignals, std::shared_ptr<std::atomic<bool>> cancelToken = nullptr);
    void run() override;

private:
    QString m_path;
    QSize m_targetSize;
    QPointer<PreviewSignals> m_previewSignals;
    std::shared_ptr<std::atomic<bool>> m_cancelToken;
};

#endif // PREVIEW_WORKER_H
