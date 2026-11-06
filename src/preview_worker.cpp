#include "preview_worker.h"
#include <QImageReader>
#include <QProcess>
#include <QDebug>

PreviewWorker::PreviewWorker(const QString &path, const QSize &targetSize, PreviewSignals *previewSignals, std::shared_ptr<std::atomic<bool>> cancelToken)
    : m_path(path), m_targetSize(targetSize), m_previewSignals(previewSignals), m_cancelToken(cancelToken) {
    setAutoDelete(true);
}

void PreviewWorker::run() {
    try {
        if (m_cancelToken && m_cancelToken->load()) return;

        QImageReader reader(m_path);
        reader.setAutoTransform(true);
        QSize originalSize = reader.size();
        if (originalSize.isValid()) {
            QSize scaled = originalSize.scaled(m_targetSize, Qt::KeepAspectRatio);
            reader.setScaledSize(scaled);
        }
        QImage image = reader.read();

        // If direct decode succeeded
        if (!image.isNull()) {
            if (m_cancelToken && m_cancelToken->load()) return;
            if (image.width() > m_targetSize.width() || image.height() > m_targetSize.height()) {
                image = image.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            if (m_cancelToken && m_cancelToken->load()) return;
            if (m_previewSignals) {
                emit m_previewSignals->ready(m_path, image, "");
            }
            return;
        }

        if (m_cancelToken && m_cancelToken->load()) return;

        // Fallback 1: Try exiftool embedded preview / thumbnail
        QStringList previewTags = {"-b", "-PreviewImage", m_path};
        QProcess proc;
        proc.start("exiftool", previewTags);
        if (proc.waitForStarted(2000)) {
            while (!proc.waitForFinished(50)) {
                if (m_cancelToken && m_cancelToken->load()) {
                    proc.kill();
                    proc.waitForFinished();
                    return;
                }
            }
            QByteArray data = proc.readAllStandardOutput();
            if (!data.isEmpty()) {
                QImage thumb;
                if (thumb.loadFromData(data)) {
                    if (m_cancelToken && m_cancelToken->load()) return;
                    thumb = thumb.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    if (m_cancelToken && m_cancelToken->load()) return;
                    if (m_previewSignals) {
                        emit m_previewSignals->ready(m_path, thumb, "");
                    }
                    return;
                }
            }
        }

        if (m_cancelToken && m_cancelToken->load()) return;

        // Fallback 2: Try ImageMagick / convert
        QProcess magickProc;
        QStringList magickArgs = {QString("%1[0]").arg(m_path), "-thumbnail", QString("%1x%2").arg(m_targetSize.width()).arg(m_targetSize.height()), "png:-"};
        magickProc.start("magick", magickArgs);
        if (!magickProc.waitForStarted(1500)) {
            magickProc.close();
            magickProc.start("convert", magickArgs);
        }
        if (magickProc.waitForStarted(1500)) {
            while (!magickProc.waitForFinished(50)) {
                if (m_cancelToken && m_cancelToken->load()) {
                    magickProc.kill();
                    magickProc.waitForFinished();
                    return;
                }
            }
            QByteArray data = magickProc.readAllStandardOutput();
            if (!data.isEmpty()) {
                QImage thumb;
                if (thumb.loadFromData(data, "PNG")) {
                    if (m_cancelToken && m_cancelToken->load()) return;
                    if (m_previewSignals) {
                        emit m_previewSignals->ready(m_path, thumb, "");
                    }
                    return;
                }
            }
        }

        if (m_cancelToken && m_cancelToken->load()) return;
        if (m_previewSignals) {
            emit m_previewSignals->ready(m_path, QImage(), reader.errorString().isEmpty() ? "Could not decode image" : reader.errorString());
        }
    } catch (const std::exception &exc) {
        if (m_cancelToken && m_cancelToken->load()) return;
        if (m_previewSignals) {
            emit m_previewSignals->ready(m_path, QImage(), exc.what());
        }
    }
}
