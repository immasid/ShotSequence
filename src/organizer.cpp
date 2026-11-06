#include "organizer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QProcess>
#include <QDateTime>
#include <QDate>
#include <QUuid>
#include <QSet>
#include <QStandardPaths>
#include <stdexcept>
#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

QString Organizer::dataDir() {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appData.isEmpty()) {
        appData = QDir(QDir::homePath()).filePath(".photosort");
    }
    QDir().mkpath(appData);

    // Seamless migration: If legacy state exists in applicationDirPath()/data and not in appData, migrate it
    QString targetState = QDir(appData).filePath("organizer_state.json");
    if (!QFile::exists(targetState)) {
        QString legacyState = QDir(QCoreApplication::applicationDirPath()).filePath("data/organizer_state.json");
        if (QFile::exists(legacyState)) {
            QFileInfo legacyFi(legacyState);
            QFileInfo targetFi(targetState);
            if (legacyFi.absoluteFilePath() != targetFi.absoluteFilePath()) {
                QFile::copy(legacyState, targetState);
            }
        }
    }
    return appData;
}

OrganizerState Organizer::loadOrganizerState() {
    OrganizerState state;
    QString path = QDir(dataDir()).filePath("organizer_state.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return state;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return state;

    QJsonObject obj = doc.object();
    if (obj.contains("prefix")) state.prefix = obj.value("prefix").toString("Photo");
    if (obj.contains("start_number")) state.start_number = obj.value("start_number").toInt(1);
    if (obj.contains("digits")) state.digits = obj.value("digits").toInt(3);
    if (obj.contains("separator")) state.separator = obj.value("separator").toString("_");

    if (obj.contains("name_overrides") && obj.value("name_overrides").isObject()) {
        QJsonObject ov = obj.value("name_overrides").toObject();
        for (auto it = ov.constBegin(); it != ov.constEnd(); ++it) {
            state.name_overrides[it.key()] = it.value().toString();
        }
    }

    if (obj.contains("manual_order_paths") && obj.value("manual_order_paths").isArray()) {
        for (const auto &val : obj.value("manual_order_paths").toArray()) {
            state.manual_order_paths.append(val.toString());
        }
    }
    return state;
}

void Organizer::saveOrganizerState(const OrganizerState &state) {
    QJsonObject obj;
    obj["prefix"] = state.prefix;
    obj["start_number"] = state.start_number;
    obj["digits"] = state.digits;
    obj["separator"] = state.separator;

    QJsonObject ov;
    for (auto it = state.name_overrides.constBegin(); it != state.name_overrides.constEnd(); ++it) {
        ov[it.key()] = it.value();
    }
    obj["name_overrides"] = ov;

    QJsonArray arr;
    for (const auto &p : state.manual_order_paths) {
        arr.append(p);
    }
    obj["manual_order_paths"] = arr;

    QString targetPath = QDir(dataDir()).filePath("organizer_state.json");
    QString tmpPath = targetPath + ".tmp";
    QFile tmpFile(tmpPath);
    if (tmpFile.open(QIODevice::WriteOnly)) {
        tmpFile.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        tmpFile.close();
        QFile::remove(targetPath);
        QFile::rename(tmpPath, targetPath);
    }
}

QString Organizer::safePrefix(const QString &value) {
    QString val = value.trimmed();
    if (val.isEmpty()) {
        throw std::runtime_error("Enter a filename prefix first.");
    }
    if (val == "." || val == "..") {
        throw std::runtime_error("The prefix contains characters that are not valid in filenames.");
    }
    static const QString invalidChars = "/\\<>:\"|?*";
    for (const QChar &ch : invalidChars) {
        if (val.contains(ch)) {
            throw std::runtime_error("The prefix contains characters that are not valid in filenames.");
        }
    }
    while (!val.isEmpty() && (val.endsWith('.') || val.endsWith(' '))) {
        val.chop(1);
    }
    if (val.isEmpty()) {
        throw std::runtime_error("The filename prefix cannot consist only of dots or spaces.");
    }
    return val;
}

QString Organizer::safeOutputName(const QString &name, const QString &originalExtension) {
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == "." || trimmed == "..") {
        throw std::runtime_error("An output filename is empty or invalid.");
    }
    static const QString invalidChars = "/\\<>:\"|?*";
    for (const QChar &ch : invalidChars) {
        if (trimmed.contains(ch)) {
            throw std::runtime_error(QString("Invalid character in output filename: %1").arg(trimmed).toStdString());
        }
    }
    QFileInfo fi(trimmed);
    QString candExt = fi.suffix().isEmpty() ? "" : ("." + fi.suffix());
    if (!candExt.isEmpty()) {
        if (candExt.compare(originalExtension, Qt::CaseInsensitive) != 0) {
            throw std::runtime_error(QString("%1: the original extension %2 must be preserved.")
                                     .arg(trimmed, originalExtension.isEmpty() ? "(none)" : originalExtension)
                                     .toStdString());
        }
    } else if (!originalExtension.isEmpty()) {
        trimmed += originalExtension;
    }
    return trimmed;
}

QString Organizer::generatedName(const PhotoRecord &record, int sequenceNumber, const OrganizerState &state) {
    QString canonical = QFileInfo(record.source_path).canonicalFilePath();
    if (canonical.isEmpty()) canonical = QFileInfo(record.source_path).absoluteFilePath();

    QString extension = QFileInfo(record.filename).suffix().isEmpty() ? "" : ("." + QFileInfo(record.filename).suffix());

    if (state.name_overrides.contains(canonical)) {
        QString custom = state.name_overrides.value(canonical).trimmed();
        QFileInfo fi(custom);
        QString custExt = fi.suffix().isEmpty() ? "" : ("." + fi.suffix());
        if (!custExt.isEmpty()) {
            if (custExt.compare(extension, Qt::CaseInsensitive) != 0) {
                throw std::runtime_error(QString("The extension for %1 must remain %2")
                                         .arg(record.filename, extension.isEmpty() ? "(none)" : extension)
                                         .toStdString());
            }
            return custom;
        }
        return custom + extension;
    }

    QString prefix = safePrefix(state.prefix.isEmpty() ? "Photo" : state.prefix);
    QString separator = state.separator;
    int digits = std::max(1, std::min(8, state.digits <= 0 ? 3 : state.digits));
    int number = state.start_number + sequenceNumber - 1;

    QString numStr = QString("%1").arg(number, digits, 10, QChar('0'));
    return QString("%1%2%3%4").arg(prefix, separator, numStr, extension);
}

QString Organizer::sha256File(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QString("Cannot open file for hashing: %1").arg(path).toStdString());
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    char buf[1024 * 1024];
    while (!file.atEnd()) {
        qint64 readBytes = file.read(buf, sizeof(buf));
        if (readBytes > 0) {
            hash.addData(QByteArrayView(buf, readBytes));
        }
    }
    return hash.result().toHex();
}

static QSet<QString> sourceRoots(const QList<PhotoRecord> &records) {
    QSet<QString> roots;
    for (const auto &rec : records) {
        QFileInfo fi(rec.source_path);
        QString source = fi.canonicalFilePath();
        if (source.isEmpty()) source = fi.absoluteFilePath();
        QString rel = rec.relative_path.isEmpty() ? fi.fileName() : rec.relative_path;

        QDir dir(fi.dir());
        QString normalizedRel = QDir::fromNativeSeparators(rel);
        QStringList parts = normalizedRel.split('/', Qt::SkipEmptyParts);
        // Step up parents matching parts count
        for (int i = 0; i < parts.size() - 1; ++i) {
            dir.cdUp();
        }
        QString canonicalRoot = dir.canonicalPath();
        if (canonicalRoot.isEmpty()) canonicalRoot = dir.absolutePath();
        roots.insert(QDir::cleanPath(canonicalRoot));
    }
    return roots;
}

QList<PlanItem> Organizer::buildPlan(const QList<PhotoRecord> &records, const QString &destination, const QMap<QString, QString> &outputNames) {
    QDir destDir(destination);
    QString canonicalDest = destDir.canonicalPath();
    if (canonicalDest.isEmpty()) canonicalDest = destDir.absolutePath();
    canonicalDest = QDir::cleanPath(canonicalDest);

    if (records.isEmpty()) {
        throw std::runtime_error("There are no files to organise.");
    }

    auto roots = sourceRoots(records);
    for (const auto &root : roots) {
        QString cleanRoot = QDir::cleanPath(root);
        if (canonicalDest == cleanRoot || canonicalDest.startsWith(cleanRoot + "/") || cleanRoot.startsWith(canonicalDest + "/")) {
            throw std::runtime_error(QString("The destination %1 overlaps a source folder (%2). Choose a separate folder.")
                                     .arg(canonicalDest, cleanRoot).toStdString());
        }
    }

    QList<PlanItem> plan;
    QMap<QString, QString> targets;
    QStringList missingDates;

    for (const auto &rec : records) {
        QFileInfo srcFi(rec.source_path);
        QString canonicalSrc = srcFi.canonicalFilePath();
        if (canonicalSrc.isEmpty()) canonicalSrc = srcFi.absoluteFilePath();

        if (!srcFi.isFile()) {
            throw std::runtime_error(QString("Source file no longer exists: %1").arg(canonicalSrc).toStdString());
        }

        QString captureDate = effective_date(rec);
        if (captureDate.isEmpty()) {
            missingDates.append(rec.filename.isEmpty() ? srcFi.fileName() : rec.filename);
            continue;
        }

        QDate parsed = QDate::fromString(captureDate, Qt::ISODate);
        if (!parsed.isValid()) {
            throw std::runtime_error(QString("Invalid capture date for %1: %2").arg(srcFi.fileName(), captureDate).toStdString());
        }

        QString outputName = safeOutputName(outputNames.value(canonicalSrc, rec.filename), rec.extension);
        QString monthName = parsed.toString("yyyy-MM-MMMM");
        QString yearStr = parsed.toString("yyyy");

        QString targetDirPath = destDir.filePath(yearStr + "/" + monthName);
        QString targetPath = QDir(targetDirPath).filePath(outputName);

        QString collisionKey = targetPath.toLower();
        if (targets.contains(collisionKey)) {
            throw std::runtime_error(QString("Two files would have the same output name:\n%1\n%2")
                                     .arg(targets[collisionKey], canonicalSrc).toStdString());
        }
        targets[collisionKey] = canonicalSrc;

        if (QFileInfo::exists(targetPath)) {
            throw std::runtime_error(QString("An output file already exists; nothing was changed:\n%1").arg(targetPath).toStdString());
        }

        PlanItem item;
        item.record = rec;
        item.source = canonicalSrc;
        item.target = targetPath;
        item.output_name = outputName;
        item.capture_date = parsed.toString(Qt::ISODate);
        item.capture_time = effective_time(rec);
        plan.append(item);
    }

    if (!missingDates.isEmpty()) {
        QStringList previewList;
        for (int i = 0; i < std::min(15, (int)missingDates.size()); ++i) {
            previewList.append("• " + missingDates[i]);
        }
        if (missingDates.size() > 15) {
            previewList.append(QString("…and %1 more").arg(missingDates.size() - 15));
        }
        throw std::runtime_error(QString("%1 file(s) still have no capture date. Correct them before organising:\n\n%2")
                                 .arg(missingDates.size())
                                 .arg(previewList.join('\n')).toStdString());
    }

    return plan;
}

QPair<QString, QString> Organizer::writeCorrectedCaptureDate(const QString &path, const PhotoRecord &record) {
    if (record.manual_date.isEmpty()) {
        return qMakePair("not-needed", "No manual date correction");
    }

    QString dateValue = record.manual_date;
    QString timeValue = effective_time(record);
    QString dateWithColons = dateValue;
    dateWithColons.replace('-', ':');

    QString effectiveTimeVal = timeValue.isEmpty() ? "00:00:00" : timeValue;
    QString timestamp = QString("%1 %2").arg(dateWithColons, effectiveTimeVal);
    QString xmpTimestamp;
    if (!timeValue.isEmpty()) {
        xmpTimestamp = QString("%1T%2").arg(dateValue, timeValue);
    } else {
        xmpTimestamp = dateValue;
    }

    // Timezone offset for QuickTime Keys:CreationDate
    QString tzStr;
    if (!record.capture_timezone.isEmpty()) {
        QString rawTz = record.capture_timezone.trimmed();
        if (rawTz == "Z" || rawTz == "+0000" || rawTz == "+00:00") {
            tzStr = "+00:00";
        } else {
            rawTz.remove(':');
            if (rawTz.length() == 5 && (rawTz[0] == '+' || rawTz[0] == '-')) {
                tzStr = QString("%1:%2").arg(rawTz.left(3), rawTz.mid(3, 2));
            }
        }
    }
    if (tzStr.isEmpty()) {
        QDateTime dt = QDateTime::fromString(dateValue + " " + effectiveTimeVal, "yyyy-MM-dd HH:mm:ss");
        int offsetSecs = dt.isValid() ? dt.offsetFromUtc() : QDateTime::currentDateTime().offsetFromUtc();
        int offsetHours = offsetSecs / 3600;
        int offsetMins = std::abs((offsetSecs % 3600) / 60);
        tzStr = QString("%1%2:%3")
                .arg(offsetHours >= 0 ? "+" : "-")
                .arg(std::abs(offsetHours), 2, 10, QChar('0'))
                .arg(offsetMins, 2, 10, QChar('0'));
    }
    QString keysTimestamp = QString("%1 %2%3").arg(dateWithColons, effectiveTimeVal, tzStr);

    QFileInfo fi(path);
    QString suffix = "." + fi.suffix().toLower();
    static const QStringList videoExts = {".mp4", ".mov", ".m4v", ".3gp", ".mts", ".m2ts"};

    QStringList args = {"-overwrite_original"};
    if (videoExts.contains(suffix)) {
        args << "-api" << "QuickTimeUTC=1"
             << QString("-QuickTime:CreateDate=%1").arg(timestamp)
             << QString("-QuickTime:ModifyDate=%1").arg(timestamp)
             << QString("-QuickTime:TrackCreateDate=%1").arg(timestamp)
             << QString("-QuickTime:TrackModifyDate=%1").arg(timestamp)
             << QString("-QuickTime:MediaCreateDate=%1").arg(timestamp)
             << QString("-QuickTime:MediaModifyDate=%1").arg(timestamp)
             << QString("-UserData:DateTimeOriginal=%1").arg(timestamp)
             << QString("-Keys:CreationDate=%1").arg(keysTimestamp)
             << QString("-XMP:CreateDate=%1").arg(xmpTimestamp)
             << QString("-XMP:DateCreated=%1").arg(xmpTimestamp)
             << QString("-FileModifyDate=%1").arg(timestamp);
    } else {
        args << QString("-EXIF:DateTimeOriginal=%1").arg(timestamp)
             << QString("-EXIF:CreateDate=%1").arg(timestamp)
             << QString("-EXIF:ModifyDate=%1").arg(timestamp)
             << QString("-XMP:CreateDate=%1").arg(xmpTimestamp)
             << QString("-XMP:DateCreated=%1").arg(xmpTimestamp)
             << QString("-FileModifyDate=%1").arg(timestamp);
    }
    args << path;

    QProcess proc;
    proc.start("exiftool", args);
    if (!proc.waitForStarted(10000)) {
        return qMakePair("warning", "ExifTool is unavailable; manual date remains in the ShotSequence report only");
    }
    if (!proc.waitForFinished(180000)) {
        proc.kill();
        return qMakePair("warning", "ExifTool timed out while writing the manual date");
    }

    if (proc.exitCode() != 0) {
        QString msg = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (msg.isEmpty()) msg = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (msg.isEmpty()) msg = "ExifTool returned an error";
        return qMakePair("warning", msg.right(1200));
    }

    // Read back verification
    QStringList verifyArgs = {
        "-json", "-G1", "-a", "-s", "-api", "QuickTimeUTC=1",
        "-EXIF:DateTimeOriginal", "-EXIF:CreateDate", "-QuickTime:CreateDate",
        "-QuickTime:MediaCreateDate", "-Keys:CreationDate",
        "-XMP:CreateDate", "-XMP:DateCreated", path
    };
    QProcess verifyProc;
    verifyProc.start("exiftool", verifyArgs);
    if (!verifyProc.waitForStarted(10000) || !verifyProc.waitForFinished(90000) || verifyProc.exitCode() != 0) {
        return qMakePair("warning", "Date write ran, but metadata read-back verification failed");
    }

    QByteArray verifyOut = verifyProc.readAllStandardOutput();
    QJsonDocument verifyDoc = QJsonDocument::fromJson(verifyOut);
    bool verified = false;
    QString colonDate = dateValue;
    colonDate.replace('-', ':');

    if (verifyDoc.isArray() && !verifyDoc.array().isEmpty()) {
        static const QSet<QString> kVerifiedDateTags = {
            "datetimeoriginal", "createdate", "modifydate",
            "mediacreatedate", "trackcreatedate", "creationdate", "datecreated"
        };
        QJsonObject obj = verifyDoc.array().first().toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            QString leafKey = it.key().split(':').last().toLower();
            if (!kVerifiedDateTags.contains(leafKey)) {
                continue; // Only check actual capture date tags, ignoring SourceFile, FileName, Directory, etc.
            }
            QString val = it.value().isString() ? it.value().toString() : "";
            if (val.contains(dateValue) || val.contains(colonDate)) {
                verified = true;
                break;
            }
        }
    }

    if (!verified) {
        return qMakePair("warning", "ExifTool returned success, but the corrected date was not found during read-back verification");
    }

    return qMakePair("written", "Manual capture date written and verified in supported metadata fields");
}

QJsonObject Organizer::performOrganisation(
    const QList<PhotoRecord> &records,
    const QString &destination,
    const QMap<QString, QString> &outputNames,
    std::function<void(int current, int total, const QString &filename)> progress
) {
    QList<PlanItem> plan = buildPlan(records, destination, outputNames);
    QJsonArray resultsArr;
    int total = plan.size();
    int successCount = 0;
    int warningCount = 0;

    for (int index = 0; index < total; ++index) {
        const auto &entry = plan[index];
        QString source = entry.source;
        QString target = entry.target;

        QFileInfo targetFi(target);
        QString tempName = QString(".%1.photosort-%2%3")
                           .arg(targetFi.completeBaseName(),
                                QUuid::createUuid().toString(QUuid::WithoutBraces),
                                targetFi.suffix().isEmpty() ? "" : ("." + targetFi.suffix()));
        QString tempPath = targetFi.dir().filePath(tempName);

        QJsonObject row;
        row["source"] = source;
        row["destination"] = target;
        row["output_name"] = entry.output_name;
        row["capture_date"] = entry.capture_date;
        row["capture_time"] = entry.capture_time;
        row["integrity_verified"] = false;
        row["metadata_status"] = "not-needed";
        row["status"] = "failed";
        row["message"] = "";

        if (progress) {
            progress(index + 1, total, QFileInfo(source).fileName());
        }

        try {
            targetFi.dir().mkpath(".");
            if (QFileInfo::exists(target)) {
                throw std::runtime_error(QString("Output appeared during processing: %1").arg(target).toStdString());
            }

            // Copy source to temp
            if (!QFile::copy(source, tempPath)) {
                throw std::runtime_error("Failed to copy source file to temporary destination");
            }

            QString srcHash = sha256File(source);
            QString copyHash = sha256File(tempPath);
            if (srcHash != copyHash) {
                throw std::runtime_error("SHA-256 verification failed after copying");
            }
            row["integrity_verified"] = true;
            row["sha256_before_metadata"] = copyHash;

            auto metaRes = writeCorrectedCaptureDate(tempPath, entry.record);
            row["metadata_status"] = metaRes.first;
            row["message"] = metaRes.second;

            if (metaRes.first == "written") {
                row["sha256_final"] = sha256File(tempPath);
            } else {
                row["sha256_final"] = copyHash;
            }

            // Safe move with strict overwrite prevention
            if (QFile::exists(target)) {
                throw std::runtime_error(QString("Output file already exists, skipping to prevent overwrite: %1").arg(target).toStdString());
            }

#if defined(Q_OS_UNIX)
            if (::link(QFile::encodeName(tempPath).constData(), QFile::encodeName(target).constData()) == 0) {
                QFile::remove(tempPath);
            } else {
                if (QFile::exists(target)) {
                    throw std::runtime_error(QString("Output file already exists: %1").arg(target).toStdString());
                }
                if (!QFile::rename(tempPath, target)) {
                    throw std::runtime_error(QString("Failed to create final organized target file: %1").arg(target).toStdString());
                }
            }
#else
            if (!QFile::rename(tempPath, target)) {
                if (QFile::exists(target)) {
                    throw std::runtime_error(QString("Output file already exists: %1").arg(target).toStdString());
                }
                if (QFile::copy(tempPath, target)) {
                    QFile::remove(tempPath);
                } else {
                    throw std::runtime_error("Failed to create final organized target file");
                }
            }
#endif

            row["status"] = "copied";
            successCount++;
            if (metaRes.first == "warning") warningCount++;
        } catch (const std::exception &exc) {
            QFile::remove(tempPath);
            row["message"] = exc.what();
        }

        resultsArr.append(row);
    }

    QJsonObject report;
    report["created_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    report["destination_root"] = QDir(destination).canonicalPath();
    report["total"] = total;
    report["copied"] = successCount;
    report["failed"] = total - successCount;
    report["metadata_warnings"] = warningCount;
    report["results"] = resultsArr;

    QString reportPath = QDir(dataDir()).filePath("last_organize_report.json");
    QFile repFile(reportPath);
    if (repFile.open(QIODevice::WriteOnly)) {
        repFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    }

    return report;
}

OrganizeWorker::OrganizeWorker(const QList<PhotoRecord> &records, const QString &destination, const QMap<QString, QString> &outputNames, QObject *parent)
    : QThread(parent), m_records(records), m_destination(destination), m_outputNames(outputNames) {}

void OrganizeWorker::run() {
    try {
        QJsonObject report = Organizer::performOrganisation(
            m_records, m_destination, m_outputNames,
            [this](int current, int total, const QString &fn) {
                emit progress_changed(current, total, fn);
            }
        );
        emit completed(report);
    } catch (const std::exception &exc) {
        emit failed(QString::fromUtf8(exc.what()));
    }
}
