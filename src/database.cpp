#include "database.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QThread>

static QMutex dbMutex;

QString Database::getDbPath() {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appData.isEmpty()) {
        appData = QDir(QDir::homePath()).filePath(".photosort");
    }
    QDir().mkpath(appData);
    QString targetDb = QDir(appData).filePath("photosort.db");

    // Seamless migration: If legacy database exists in applicationDirPath()/data and not yet in AppDataLocation, migrate it
    if (!QFile::exists(targetDb)) {
        QString legacyDb = QDir(QCoreApplication::applicationDirPath()).filePath("data/photosort.db");
        if (QFile::exists(legacyDb)) {
            QFileInfo legacyFi(legacyDb);
            QFileInfo targetFi(targetDb);
            if (legacyFi.absoluteFilePath() != targetFi.absoluteFilePath()) {
                QFile::copy(legacyDb, targetDb);
            }
        }
    }
    return targetDb;
}

static QSqlDatabase getDatabaseConnection() {
    const QString connName = QString("photosort_conn_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(connName)) {
        auto db = QSqlDatabase::database(connName, false);
        if (db.isOpen()) return db;
        if (db.open()) return db;
    }
    auto db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(Database::getDbPath());
    db.open();
    return db;
}

bool Database::initDb() {
    QMutexLocker locker(&dbMutex);
    auto db = getDatabaseConnection();
    if (!db.isOpen()) {
        qWarning() << "Failed to open database:" << db.lastError().text();
        return false;
    }
    QSqlQuery query(db);
    return query.exec(R"(
        CREATE TABLE IF NOT EXISTS manual_dates (
            source_path TEXT PRIMARY KEY,
            manual_date TEXT NOT NULL,
            manual_time TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )");
}

QMap<QString, ManualDateEntry> Database::getManualDates() {
    QMutexLocker locker(&dbMutex);
    QMap<QString, ManualDateEntry> result;
    auto db = getDatabaseConnection();
    if (!db.isOpen()) return result;

    QSqlQuery query("SELECT source_path, manual_date, manual_time, updated_at FROM manual_dates", db);
    while (query.next()) {
        QString path = query.value(0).toString();
        ManualDateEntry entry;
        entry.manual_date = query.value(1).toString();
        entry.manual_time = query.value(2).toString();
        entry.updated_at = query.value(3).toString();
        result[path] = entry;
    }
    return result;
}

bool Database::saveManualDate(const QString &source_path, const QString &manual_date, const QString &manual_time) {
    QMutexLocker locker(&dbMutex);
    auto db = getDatabaseConnection();
    if (!db.isOpen()) return false;

    QString canonical = QFileInfo(source_path).canonicalFilePath();
    if (canonical.isEmpty()) canonical = QFileInfo(source_path).absoluteFilePath();

    QString nowUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlQuery query(db);
    query.prepare(R"(
        INSERT INTO manual_dates (source_path, manual_date, manual_time, updated_at)
        VALUES (?, ?, ?, ?)
        ON CONFLICT(source_path) DO UPDATE SET
            manual_date = excluded.manual_date,
            manual_time = excluded.manual_time,
            updated_at = excluded.updated_at
    )");
    query.addBindValue(canonical);
    query.addBindValue(manual_date);
    query.addBindValue(manual_time);
    query.addBindValue(nowUtc);
    return query.exec();
}

bool Database::deleteManualDate(const QString &source_path) {
    QMutexLocker locker(&dbMutex);
    auto db = getDatabaseConnection();
    if (!db.isOpen()) return false;

    QString canonical = QFileInfo(source_path).canonicalFilePath();
    if (canonical.isEmpty()) canonical = QFileInfo(source_path).absoluteFilePath();

    QSqlQuery query(db);
    query.prepare("DELETE FROM manual_dates WHERE source_path = ?");
    query.addBindValue(canonical);
    return query.exec();
}
