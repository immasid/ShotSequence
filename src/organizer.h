#ifndef ORGANIZER_H
#define ORGANIZER_H

#include "common.h"
#include <QString>
#include <QMap>
#include <QList>
#include <QJsonObject>
#include <QThread>
#include <functional>

struct OrganizerState {
    QString prefix = "Photo";
    int start_number = 1;
    int digits = 3;
    QString separator = "_";
    QMap<QString, QString> name_overrides;
    QStringList manual_order_paths;
};

struct PlanItem {
    PhotoRecord record;
    QString source;
    QString target;
    QString output_name;
    QString capture_date;
    QString capture_time;
};

class Organizer {
public:
    static QString dataDir();
    static OrganizerState loadOrganizerState();
    static void saveOrganizerState(const OrganizerState &state);

    static QString safePrefix(const QString &value);
    static QString generatedName(const PhotoRecord &record, int sequenceNumber, const OrganizerState &state);
    static QString safeOutputName(const QString &name, const QString &originalExtension);

    static QString sha256File(const QString &path);

    static QList<PlanItem> buildPlan(const QList<PhotoRecord> &records, const QString &destination, const QMap<QString, QString> &outputNames);
    static QJsonObject performOrganisation(
        const QList<PhotoRecord> &records,
        const QString &destination,
        const QMap<QString, QString> &outputNames,
        std::function<void(int current, int total, const QString &filename)> progress = nullptr
    );

    static QPair<QString, QString> writeCorrectedCaptureDate(const QString &path, const PhotoRecord &record);
};

class OrganizeWorker : public QThread {
    Q_OBJECT
public:
    OrganizeWorker(const QList<PhotoRecord> &records, const QString &destination, const QMap<QString, QString> &outputNames, QObject *parent = nullptr);
    void run() override;

signals:
    void progress_changed(int current, int total, const QString &filename);
    void completed(const QJsonObject &report);
    void failed(const QString &errorMessage);

private:
    QList<PhotoRecord> m_records;
    QString m_destination;
    QMap<QString, QString> m_outputNames;
};

#endif // ORGANIZER_H
