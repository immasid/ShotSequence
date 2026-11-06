#include "scanner.h"
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QTimeZone>
#include <QDebug>
#include <QThread>
#include <algorithm>

static const QStringList kImageExts = {
    ".jpg", ".jpeg", ".heic", ".heif", ".png",
    ".tif", ".tiff", ".webp", ".gif", ".bmp",
    ".dng", ".cr2", ".cr3", ".nef", ".arw",
    ".rw2", ".orf", ".raf", ".pef"
};

static const QStringList kVideoExts = {
    ".mp4", ".mov", ".m4v", ".avi", ".mkv",
    ".mts", ".m2ts", ".3gp", ".wmv", ".webm"
};

static const QStringList kCaptureTagPriority = {
    "ExifIFD:DateTimeOriginal",
    "EXIF:DateTimeOriginal",
    "ExifIFD:SubSecDateTimeOriginal",
    "EXIF:SubSecDateTimeOriginal",
    "ExifIFD:CreateDate",
    "EXIF:CreateDate",
    "ExifIFD:DateTimeDigitized",
    "EXIF:DateTimeDigitized",
    "XMP-xmp:DateCreated",
    "XMP:DateCreated",
    "XMP-photoshop:DateCreated",
    "XMP-xmp:CreateDate",
    "XMP:CreateDate",
    "IPTC:DateCreated",
    "QuickTime:CreateDate",
    "QuickTime:MediaCreateDate",
    "QuickTime:TrackCreateDate",
    "Keys:CreationDate"
};

static const QStringList kCaptureLeafPriority = {
    "datetimeoriginal",
    "subsecdatetimeoriginal",
    "createdate",
    "datecreated",
    "mediacreatedate",
    "trackcreatedate",
    "creationdate",
    "contentcreatedate"
};

const QStringList &Scanner::imageExtensions() { return kImageExts; }
const QStringList &Scanner::videoExtensions() { return kVideoExts; }

bool Scanner::isSupportedExtension(const QString &ext) {
    QString lower = ext.toLower();
    return kImageExts.contains(lower) || kVideoExts.contains(lower);
}

static QString firstValue(const QJsonValue &val) {
    if (val.isArray()) {
        for (const auto &item : val.toArray()) {
            if (!item.isNull() && !item.toString().trimmed().isEmpty()) {
                return item.toString().trimmed();
            }
        }
        return QString();
    }
    if (val.isNull() || val.isUndefined()) return QString();
    if (val.isObject()) {
        return QString::fromUtf8(QJsonDocument(val.toObject()).toJson(QJsonDocument::Compact));
    }
    if (val.isDouble()) {
        return QString::number(val.toDouble());
    }
    if (val.isBool()) {
        return val.toBool() ? "true" : "false";
    }
    QString str = val.toString().trimmed();
    return str.isEmpty() ? QString() : str;
}

QPair<QString, QString> Scanner::findTag(const QJsonObject &metadata, const QString &leafName) {
    QString target = leafName.toLower();
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        QString key = it.key();
        QString leaf = key.split(':').last().toLower();
        if (leaf == target) {
            QString text = firstValue(it.value());
            if (!text.isEmpty()) {
                return qMakePair(key, text);
            }
        }
    }
    return qMakePair(QString(), QString());
}

ParsedDateResult Scanner::parseCaptureDate(const QString &value) {
    ParsedDateResult res;
    QString text = value.trimmed();
    if (text.isEmpty()) return res;

    // Convert EXIF separator YYYY:MM:DD to ISO YYYY-MM-DD
    static const QRegularExpression exifDateRe("^(\\d{4}):(\\d{2}):(\\d{2})");
    text.replace(exifDateRe, "\\1-\\2-\\3");

    // 10 chars: YYYY-MM-DD
    if (text.length() == 10) {
        QDate date = QDate::fromString(text, Qt::ISODate);
        if (date.isValid() && date.year() > 1900 && date.year() < 2200) {
            res.valid = true;
            res.date = date.toString("yyyy-MM-dd");
            return res;
        }
        return res;
    }

    // Full timestamp parsing: YYYY-MM-DDTHH:MM:SS or YYYY-MM-DD HH:MM:SS with possible offset / Z
    QString isoText = text;
    isoText.replace(' ', 'T');

    // Parse timezone offset if any
    QString tzStr;
    static const QRegularExpression tzRe("([+-]\\d{2}:?\\d{2}|Z)$");
    auto match = tzRe.match(isoText);
    if (match.hasMatch()) {
        QString rawTz = match.captured(1);
        if (rawTz == "Z") {
            tzStr = "+0000";
        } else {
            tzStr = rawTz;
            tzStr.remove(':');
        }
    }

    // Extract base date and time
    static const QRegularExpression dtRe("^(\\d{4}-\\d{2}-\\d{2})T(\\d{2}:\\d{2}:\\d{2})");
    auto dtMatch = dtRe.match(isoText);
    if (dtMatch.hasMatch()) {
        QString datePart = dtMatch.captured(1);
        QString timePart = dtMatch.captured(2);
        QDate qd = QDate::fromString(datePart, Qt::ISODate);
        QTime qt = QTime::fromString(timePart, "HH:mm:ss");
        if (qd.isValid() && qt.isValid() && qd.year() > 1900 && qd.year() < 2200) {
            res.valid = true;
            res.date = datePart;
            res.time = timePart;
            res.timezone = tzStr;
            return res;
        }
    }

    return res;
}

QPair<QString, QString> Scanner::findCaptureTag(const QJsonObject &metadata) {
    QList<QPair<QString, QString>> candidates;
    QSet<QString> seenKeys;

    auto addCandidate = [&](const QString &k, const QJsonValue &v) {
        QString text = firstValue(v);
        if (!text.isEmpty() && !seenKeys.contains(k)) {
            candidates.append(qMakePair(k, text));
            seenKeys.insert(k);
        }
    };

    // Priority 1: explicit priority list
    for (const QString &preferred : kCaptureTagPriority) {
        for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
            if (it.key().compare(preferred, Qt::CaseInsensitive) == 0) {
                addCandidate(it.key(), it.value());
            }
        }
    }

    // Priority 2: leaf priorities
    QStringList leafPriority;
    for (const QString &tag : kCaptureTagPriority) {
        QString leaf = tag.split(':').last().toLower();
        if (!leafPriority.contains(leaf)) leafPriority.append(leaf);
    }
    for (const QString &leaf : kCaptureLeafPriority) {
        if (!leafPriority.contains(leaf.toLower())) leafPriority.append(leaf.toLower());
    }

    for (const QString &leaf : leafPriority) {
        for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
            if (it.key().split(':').last().toLower() == leaf) {
                addCandidate(it.key(), it.value());
            }
        }
    }

    if (candidates.isEmpty()) {
        return qMakePair(QString(), QString());
    }

    // Return the first candidate that parses as a valid date
    for (const auto &cand : candidates) {
        ParsedDateResult parsed = parseCaptureDate(cand.second);
        if (parsed.valid) {
            return cand;
        }
    }

    // Otherwise preserve first candidate for diagnostic review
    return candidates.first();
}

QList<QPair<QString, QString>> Scanner::discoverFiles(const QStringList &inputs) {
    QMap<QString, QString> discovered; // canonicalFilePath -> sourceRoot

    for (const QString &raw : inputs) {
        if (QThread::currentThread()->isInterruptionRequested()) return {};
        QString path = raw;
        if (path.startsWith('~')) {
            path = QDir::homePath() + path.mid(1);
        }
        QFileInfo rootFi(path);
        if (!rootFi.exists()) continue;

        QString absRoot = rootFi.canonicalFilePath();
        if (absRoot.isEmpty()) absRoot = rootFi.absoluteFilePath();

        QString sourceRoot;
        QStringList candidates;

        if (rootFi.isFile()) {
            candidates.append(absRoot);
            sourceRoot = rootFi.dir().canonicalPath();
            if (sourceRoot.isEmpty()) sourceRoot = rootFi.dir().absolutePath();
        } else if (rootFi.isDir()) {
            sourceRoot = absRoot;
            QDirIterator it(absRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (QThread::currentThread()->isInterruptionRequested()) return {};
                candidates.append(it.next());
            }
        }

        for (const QString &candPath : candidates) {
            if (QThread::currentThread()->isInterruptionRequested()) return {};
            QFileInfo fi(candPath);
            if (fi.isSymLink() || !fi.isFile()) continue;
            QString suffix = fi.suffix().toLower();
            if (!suffix.isEmpty()) suffix = "." + suffix;
            if (!isSupportedExtension(suffix)) continue;

            QString resolved = fi.canonicalFilePath();
            if (resolved.isEmpty()) resolved = fi.absoluteFilePath();

            if (!discovered.contains(resolved) || sourceRoot.length() > discovered.value(resolved).length()) {
                discovered[resolved] = sourceRoot;
            }
        }
    }

    QList<QPair<QString, QString>> result;
    for (auto it = discovered.constBegin(); it != discovered.constEnd(); ++it) {
        result.append(qMakePair(it.key(), it.value()));
    }

    std::sort(result.begin(), result.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
        QDir dirA(a.second);
        QDir dirB(b.second);
        QString relA = dirA.relativeFilePath(a.first);
        QString relB = dirB.relativeFilePath(b.first);
        return naturalLess(relA, relB);
    });

    return result;
}

QPair<QMap<QString, QJsonObject>, QStringList> Scanner::readExiftoolMetadata(const QStringList &paths, int chunkSize) {
    QMap<QString, QJsonObject> metadataByPath;
    QStringList warnings;

    static const QRegularExpression summaryRe(R"(\d+\s+.*?files?\s+(?:read|created|updated|unchanged))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression scanSummaryRe(R"(\d+\s+directories?\s+scanned)", QRegularExpression::CaseInsensitiveOption);

    for (int start = 0; start < paths.size(); start += chunkSize) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            warnings.append("Scan interrupted by user.");
            return qMakePair(metadataByPath, warnings);
        }

        QStringList chunk;
        int end = std::min<int>(paths.size(), start + chunkSize);
        for (int i = start; i < end; ++i) {
            chunk.append(paths[i]);
        }

        QStringList args = {
            "-json", "-G1", "-a", "-s", "-api", "LargeFileSupport=1", "-api", "QuickTimeUTC=1"
        };
        args.append(chunk);

        QProcess proc;
        proc.start("exiftool", args);
        if (!proc.waitForStarted(10000)) {
            warnings.append("ExifTool is not installed or not in PATH.");
            return qMakePair(metadataByPath, warnings);
        }
        while (!proc.waitForFinished(50)) {
            if (QThread::currentThread()->isInterruptionRequested()) {
                proc.kill();
                proc.waitForFinished();
                warnings.append("Scan interrupted by user.");
                return qMakePair(metadataByPath, warnings);
            }
        }

        QString errOutput = QString::fromUtf8(proc.readAllStandardError());
        if (!errOutput.trimmed().isEmpty()) {
            for (const QString &rawLine : errOutput.split('\n')) {
                QString line = rawLine.trimmed();
                if (line.isEmpty()) continue;
                if (summaryRe.match(line).hasMatch() || scanSummaryRe.match(line).hasMatch()) {
                    continue;
                }
                warnings.append(line);
            }
        }

        QByteArray outBytes = proc.readAllStandardOutput();
        if (outBytes.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(outBytes);
        if (doc.isArray()) {
            for (const auto &item : doc.array()) {
                if (item.isObject()) {
                    QJsonObject obj = item.toObject();
                    QString source = obj.value("SourceFile").toString();
                    if (!source.isEmpty()) {
                        QString resolved = QFileInfo(source).canonicalFilePath();
                        if (resolved.isEmpty()) resolved = QFileInfo(source).absoluteFilePath();
                        metadataByPath[resolved] = obj;
                    }
                }
            }
        }
    }

    return qMakePair(metadataByPath, warnings);
}

PhotoRecord Scanner::createRecord(const QString &filePath, const QString &sourceRoot, const QJsonObject &metadata, int sourceOrder) {
    PhotoRecord rec;
    QFileInfo fi(filePath);
    rec.source_path = filePath;
    QDir rootDir(sourceRoot);
    rec.relative_path = rootDir.relativeFilePath(filePath);
    rec.filename = fi.fileName();
    rec.extension = "." + fi.suffix().toLower();
    rec.file_size = fi.size();
    rec.filesystem_modified = fi.lastModified().toString("yyyy-MM-dd HH:mm:ss");
    rec.source_order = sourceOrder;
    rec.proposed_order = 0;

    static const QRegularExpression numRe("(\\d+)");
    auto it = numRe.globalMatch(fi.completeBaseName());
    while (it.hasNext()) {
        rec.filename_numbers.append(it.next().captured(1).toInt());
    }

    auto capturePair = findCaptureTag(metadata);
    rec.capture_tag = capturePair.first;
    rec.capture_raw = capturePair.second;

    if (!rec.capture_raw.isEmpty()) {
        ParsedDateResult parsed = parseCaptureDate(rec.capture_raw);
        if (parsed.valid) {
            rec.capture_date = parsed.date;
            rec.capture_time = parsed.time;
            rec.capture_timezone = parsed.timezone;
        }
    }

    if (!rec.capture_date.isEmpty() && !rec.capture_time.isEmpty()) {
        rec.date_status = "OK";
    } else if (!rec.capture_date.isEmpty()) {
        rec.date_status = "DATE ONLY - TIME REVIEW NEEDED";
    } else {
        rec.date_status = "MISSING - MANUAL DATE/TIME NEEDED";
    }

    rec.camera_make = findTag(metadata, "Make").second;
    rec.camera_model = findTag(metadata, "Model").second;
    rec.lens = findTag(metadata, "LensModel").second;
    rec.gps_position = findTag(metadata, "GPSPosition").second;

    auto wPair = findTag(metadata, "ImageWidth");
    if (wPair.second.isEmpty()) wPair = findTag(metadata, "ExifImageWidth");
    rec.width = wPair.second;

    auto hPair = findTag(metadata, "ImageHeight");
    if (hPair.second.isEmpty()) hPair = findTag(metadata, "ExifImageHeight");
    rec.height = hPair.second;

    rec.media_kind = kVideoExts.contains(rec.extension.toLower()) ? "VIDEO" : "IMAGE";
    rec.metadata = metadata;
    return rec;
}

bool Scanner::scanPaths(const QStringList &inputs, QList<PhotoRecord> &originalOrder, QList<PhotoRecord> &proposedOrder, QStringList &warnings) {
    if (QThread::currentThread()->isInterruptionRequested()) return false;
    auto discovered = discoverFiles(inputs);
    if (discovered.isEmpty()) return true;
    if (QThread::currentThread()->isInterruptionRequested()) return false;

    QStringList paths;
    for (const auto &pair : discovered) {
        paths.append(pair.first);
    }

    auto metaPair = readExiftoolMetadata(paths);
    const auto &metadataByPath = metaPair.first;
    warnings.append(metaPair.second);
    if (QThread::currentThread()->isInterruptionRequested()) return false;

    int idx = 1;
    for (const auto &pair : discovered) {
        QString path = pair.first;
        QString root = pair.second;
        QJsonObject meta = metadataByPath.value(path);
        PhotoRecord record = createRecord(path, root, meta, idx++);
        originalOrder.append(record);
    }

    proposedOrder = originalOrder;
    std::sort(proposedOrder.begin(), proposedOrder.end(), [](const PhotoRecord &a, const PhotoRecord &b) {
        bool aNoDate = a.capture_date.isEmpty();
        bool bNoDate = b.capture_date.isEmpty();
        if (aNoDate != bNoDate) return !aNoDate && bNoDate;

        QString dateA = aNoDate ? "9999-99-99" : a.capture_date;
        QString dateB = bNoDate ? "9999-99-99" : b.capture_date;
        if (dateA != dateB) return dateA < dateB;

        QString timeA = a.capture_time.isEmpty() ? "00:00:00" : a.capture_time;
        QString timeB = b.capture_time.isEmpty() ? "00:00:00" : b.capture_time;
        if (timeA != timeB) return timeA < timeB;

        return naturalLess(a.relative_path, b.relative_path);
    });

    for (int i = 0; i < proposedOrder.size(); ++i) {
        proposedOrder[i].proposed_order = i + 1;
    }

    return true;
}
