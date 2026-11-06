#ifndef COMMON_H
#define COMMON_H

#include <QString>
#include <QList>
#include <QJsonObject>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QCollator>
#include <cmath>

struct PhotoRecord {
    QString source_path;
    QString relative_path;
    QString filename;
    QString extension;
    QString media_kind; // "IMAGE" or "VIDEO"
    qint64 file_size = 0;
    QString filesystem_modified;
    int source_order = 0;
    int proposed_order = 0;
    QList<int> filename_numbers;
    QString capture_date;
    QString capture_time;
    QString capture_timezone;
    QString capture_raw;
    QString capture_tag;
    QString date_status;
    QString camera_make;
    QString camera_model;
    QString lens;
    QString gps_position;
    QString width;
    QString height;
    QJsonObject metadata;

    // Editable / state properties
    QString manual_date;
    QString manual_time;
    QString final_output_name;
};

inline QString effective_date(const PhotoRecord &record) {
    if (!record.manual_date.isEmpty()) {
        return record.manual_date;
    }
    return record.capture_date;
}

inline QString effective_time(const PhotoRecord &record) {
    if (!record.manual_time.isEmpty()) {
        return record.manual_time;
    }
    if (!record.capture_time.isEmpty()) {
        return record.capture_time;
    }
    return QString();
}

inline QString effective_status(const PhotoRecord &record) {
    if (!record.manual_date.isEmpty()) {
        return !effective_time(record).isEmpty() ? "MANUAL" : "DATE ONLY";
    }
    if (!record.capture_date.isEmpty() && !record.capture_time.isEmpty()) {
        return "VERIFIED";
    }
    if (!record.capture_date.isEmpty()) {
        return "DATE ONLY";
    }
    return "MISSING";
}

inline QString original_timestamp(const PhotoRecord &record) {
    if (record.capture_date.isEmpty()) {
        return "No capture date";
    }
    QString timeStr = record.capture_time.isEmpty() ? "Time unknown" : record.capture_time;
    return record.capture_date + "  " + timeStr;
}

inline QString proposed_timestamp(const PhotoRecord &record) {
    QString dateVal = effective_date(record);
    if (dateVal.isEmpty()) {
        return "Needs date review";
    }
    QString timeVal = effective_time(record);
    return dateVal + "  " + (timeVal.isEmpty() ? "Time unknown" : timeVal);
}

inline QString format_size(qint64 size) {
    double value = static_cast<double>(size);
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 4) {
        value /= 1024.0;
        unitIndex++;
    }
    if (unitIndex == 0) {
        return QString::number(size) + " B";
    }
    return QString::asprintf("%.1f %s", value, units[unitIndex]);
}

struct NaturalToken {
    bool isNumber;
    qulonglong num;
    QString text;
};

inline QList<NaturalToken> tokenizeNatural(const QString &str) {
    QList<NaturalToken> tokens;
    static const QRegularExpression re("(\\d+)");
    auto it = re.globalMatch(str);
    int lastPos = 0;
    while (it.hasNext()) {
        auto match = it.next();
        int start = match.capturedStart();
        if (start > lastPos) {
            tokens.append({false, 0, str.mid(lastPos, start - lastPos).toCaseFolded()});
        }
        tokens.append({true, match.captured(1).toULongLong(), QString()});
        lastPos = match.capturedEnd();
    }
    if (lastPos < str.length()) {
        tokens.append({false, 0, str.mid(lastPos).toCaseFolded()});
    }
    return tokens;
}

inline bool naturalLess(const QString &a, const QString &b) {
    static thread_local QCollator collator = []() {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    return collator.compare(a, b) < 0;
}

struct SequenceGroupResult {
    bool valid = false;
    QString parent;
    QString prefix;
    QString suffix;
    int number = 0;
};

inline SequenceGroupResult sequence_group(const PhotoRecord &record) {
    SequenceGroupResult res;
    QFileInfo fi(record.filename);
    QString stem = fi.completeBaseName();
    static const QRegularExpression re("^(.*?)(\\d+)(\\D*)$");
    auto match = re.match(stem);
    if (!match.hasMatch()) {
        res.valid = false;
        return res;
    }
    QString normalizedRel = QDir::fromNativeSeparators(record.relative_path);
    QFileInfo relFi(normalizedRel);
    res.valid = true;
    res.parent = relFi.dir().path();
    res.prefix = match.captured(1).toCaseFolded();
    res.number = match.captured(2).toInt();
    res.suffix = match.captured(3).toCaseFolded();
    return res;
}

#endif // COMMON_H
