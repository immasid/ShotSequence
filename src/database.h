#ifndef DATABASE_H
#define DATABASE_H

#include <QString>
#include <QMap>

struct ManualDateEntry {
    QString manual_date;
    QString manual_time;
    QString updated_at;
};

class Database {
public:
    static QString getDbPath();
    static bool initDb();
    static QMap<QString, ManualDateEntry> getManualDates();
    static bool saveManualDate(const QString &source_path, const QString &manual_date, const QString &manual_time);
    static bool deleteManualDate(const QString &source_path);
};

#endif // DATABASE_H
