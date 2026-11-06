#ifndef SCANNER_H
#define SCANNER_H

#include "common.h"
#include <QStringList>
#include <QPair>

struct ParsedDateResult {
    bool valid = false;
    QString date;
    QString time;
    QString timezone;
};

class Scanner {
public:
    static const QStringList &imageExtensions();
    static const QStringList &videoExtensions();
    static bool isSupportedExtension(const QString &ext);

    static ParsedDateResult parseCaptureDate(const QString &value);
    static QPair<QString, QString> findCaptureTag(const QJsonObject &metadata);
    static QPair<QString, QString> findTag(const QJsonObject &metadata, const QString &leafName);

    static QList<QPair<QString, QString>> discoverFiles(const QStringList &inputs);
    static QPair<QMap<QString, QJsonObject>, QStringList> readExiftoolMetadata(const QStringList &paths, int chunkSize = 100);

    static PhotoRecord createRecord(const QString &filePath, const QString &sourceRoot, const QJsonObject &metadata, int sourceOrder);
    static bool scanPaths(const QStringList &inputs, QList<PhotoRecord> &originalOrder, QList<PhotoRecord> &proposedOrder, QStringList &warnings);
};

#endif // SCANNER_H
