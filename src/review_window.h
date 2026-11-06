#ifndef REVIEW_WINDOW_H
#define REVIEW_WINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QThreadPool>
#include <QTimer>
#include <QMap>
#include <QSet>
#include <memory>
#include <atomic>
#include "common.h"
#include "organizer.h"
#include "reorderable_table.h"
#include "preview_worker.h"

class LauncherWindow;

class ReviewWindow : public QMainWindow {
    Q_OBJECT
public:
    ReviewWindow(const QList<PhotoRecord> &originalRecords, const QStringList &warnings, LauncherWindow *launcher, QWidget *parent = nullptr);
    ~ReviewWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshTables();
    void prefixSettingChanged(const QString &val);
    void namingSettingsChanged();
    void editSelectedOutputName();
    void proposedCellDoubleClicked(int row, int col);
    void onManualOrderChanged(const QStringList &paths);
    void resetChronologicalOrder();
    void beginOrganisation();
    void organisationProgressChanged(int current, int total, const QString &filename);
    void organisationCompleted(const QJsonObject &report);
    void organisationFailed(const QString &message);
    void tableSelection(QTableWidget *table, int row);
    void startPreviewRequest();
    void onPreviewReady(const QString &path, const QImage &image, const QString &error);
    void openDateEditor();
    void clearCorrection(const QString &path = QString());

private:
    void restoreCorrections();
    void recalculateOrder();
    void persistOrganizerState();
    QMap<QString, QString> currentOutputNames();
    void buildUi();
    QFrame *makeInspectorPanel();
    QFrame *makeTablePanel(const QString &titleText, const QString &captionText, QTableWidget *table, bool isChronological = false);
    QTableWidget *createTable(bool proposed);
    QList<PhotoRecord> filtered(const QList<PhotoRecord> &records);
    void populateTable(QTableWidget *table, const QList<PhotoRecord> &records, bool original);
    void refreshOutputNames();
    void selectPath(const QString &path, QTableWidget *sourceTable = nullptr);
    QString sequenceNeighbours(const PhotoRecord &record);
    void showRecord(const QString &path);
    void showPreview(const PhotoRecord &record);
    void setPreviewImage(const QImage &image);
    void saveReport();
    bool isOrderIdentical() const;
    void updateMatchIndicator();

    LauncherWindow *m_launcher = nullptr;
    QList<PhotoRecord> m_originalRecords;
    QList<PhotoRecord> m_proposedRecords;
    QStringList m_warnings;
    QMap<QString, PhotoRecord> m_recordMap;
    QString m_selectedPath;
    bool m_syncing = false;
    OrganizerState m_organizerState;
    QStringList m_manualOrderPaths;

    OrganizeWorker *m_organizeWorker = nullptr;
    QProgressDialog *m_organizeProgress = nullptr;

    // Thumbnail cache
    QMap<QString, QImage> m_previewCache;
    QStringList m_previewCacheOrder;
    const int m_previewCacheLimit = 128;
    QSet<QString> m_previewInFlight;
    QString m_previewRequestedPath;
    QSize m_previewSize = QSize(240, 240);
    QThreadPool *m_previewPool = nullptr;
    PreviewSignals *m_previewSignals = nullptr;
    QTimer *m_previewTimer = nullptr;
    std::shared_ptr<std::atomic<bool>> m_previewCancelToken;

    QMap<quintptr, QMap<QString, int>> m_tableRowMaps;
    QMap<QString, QString> m_sequenceContextCache;

    QLabel *m_scanLocation = nullptr;
    QLabel *m_matchBadge = nullptr;
    QLabel *m_chronologicalMatchTag = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_previewName = nullptr;
    QLabel *m_previewMeta = nullptr;
    QPlainTextEdit *m_metadataText = nullptr;
    QPushButton *m_editDateButton = nullptr;
    QPushButton *m_editNameButton = nullptr;
    QLineEdit *m_search = nullptr;
    QLineEdit *m_prefixEdit = nullptr;
    QSpinBox *m_startNumber = nullptr;
    QSpinBox *m_digits = nullptr;
    QLineEdit *m_separatorEdit = nullptr;
    QPushButton *m_resetOrderButton = nullptr;
    QPushButton *m_organizeButton = nullptr;
    QTableWidget *m_originalTable = nullptr;
    ReorderableTableWidget *m_proposedTable = nullptr;
};

#endif // REVIEW_WINDOW_H
