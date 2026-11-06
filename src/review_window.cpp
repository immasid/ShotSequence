#include "review_window.h"
#include "launcher_window.h"
#include "database.h"
#include "style.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateEdit>
#include <QTimeEdit>
#include <QCheckBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QStatusBar>
#include <QCloseEvent>
#include <QRegularExpression>

ReviewWindow::ReviewWindow(const QList<PhotoRecord> &originalRecords, const QStringList &warnings, LauncherWindow *launcher, QWidget *parent)
    : QMainWindow(parent), m_launcher(launcher), m_originalRecords(originalRecords), m_warnings(warnings) {
    Database::initDb();

    for (const auto &r : m_originalRecords) {
        m_recordMap[r.source_path] = r;
    }

    m_organizerState = Organizer::loadOrganizerState();
    m_manualOrderPaths = m_organizerState.manual_order_paths;

    m_previewCancelToken = std::make_shared<std::atomic<bool>>(false);
    m_previewPool = new QThreadPool(this);
    m_previewPool->setMaxThreadCount(2);

    m_previewSignals = new PreviewSignals(this);
    connect(m_previewSignals, &PreviewSignals::ready, this, &ReviewWindow::onPreviewReady);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(55);
    connect(m_previewTimer, &QTimer::timeout, this, &ReviewWindow::startPreviewRequest);

    setWindowTitle("ShotSequence — Review library");
    setMinimumSize(1120, 720);
    setStyleSheet(DARK_STYLE);

    restoreCorrections();
    recalculateOrder();
    buildUi();
    refreshTables();
    saveReport();

    if (!m_proposedRecords.isEmpty()) {
        selectPath(m_proposedRecords.first().source_path);
    }
}

ReviewWindow::~ReviewWindow() {
    if (m_previewCancelToken) {
        m_previewCancelToken->store(true);
    }
    if (m_previewTimer) m_previewTimer->stop();
    if (m_previewPool) {
        m_previewPool->clear();
        m_previewPool->waitForDone();
    }
    if (m_organizeWorker && m_organizeWorker->isRunning()) {
        m_organizeWorker->wait();
    }
}

void ReviewWindow::restoreCorrections() {
    auto dbCorrections = Database::getManualDates();
    for (auto it = dbCorrections.constBegin(); it != dbCorrections.constEnd(); ++it) {
        QString path = it.key();
        if (m_recordMap.contains(path)) {
            m_recordMap[path].manual_date = it.value().manual_date;
            m_recordMap[path].manual_time = it.value().manual_time;
            for (auto &r : m_originalRecords) {
                if (r.source_path == path) {
                    r.manual_date = it.value().manual_date;
                    r.manual_time = it.value().manual_time;
                }
            }
        }
    }
}

void ReviewWindow::recalculateOrder() {
    QList<PhotoRecord> chronological = m_originalRecords;
    std::sort(chronological.begin(), chronological.end(), [](const PhotoRecord &a, const PhotoRecord &b) {
        QString dateA = effective_date(a);
        QString dateB = effective_date(b);
        bool aNoDate = dateA.isEmpty();
        bool bNoDate = dateB.isEmpty();
        if (aNoDate != bNoDate) return !aNoDate && bNoDate;

        QString dA = aNoDate ? "9999-99-99" : dateA;
        QString dB = bNoDate ? "9999-99-99" : dateB;
        if (dA != dB) return dA < dB;

        QString tA = effective_time(a);
        QString tB = effective_time(b);
        QString timeA = tA.isEmpty() ? "99:99:99" : tA;
        QString timeB = tB.isEmpty() ? "99:99:99" : tB;
        if (timeA != timeB) return timeA < timeB;

        return naturalLess(a.relative_path, b.relative_path);
    });

    if (!m_manualOrderPaths.isEmpty()) {
        QMap<QString, PhotoRecord> byPath;
        for (const auto &r : chronological) {
            byPath[r.source_path] = r;
        }
        QList<PhotoRecord> ordered;
        for (const QString &p : m_manualOrderPaths) {
            if (byPath.contains(p)) {
                ordered.append(byPath.take(p));
            }
        }
        for (const auto &r : chronological) {
            if (byPath.contains(r.source_path)) {
                ordered.append(r);
            }
        }
        m_proposedRecords = ordered;
    } else {
        m_proposedRecords = chronological;
    }

    for (int i = 0; i < m_proposedRecords.size(); ++i) {
        m_proposedRecords[i].proposed_order = i + 1;
        if (m_recordMap.contains(m_proposedRecords[i].source_path)) {
            m_recordMap[m_proposedRecords[i].source_path].proposed_order = i + 1;
        }
    }
}

void ReviewWindow::persistOrganizerState() {
    if (m_prefixEdit) m_organizerState.prefix = m_prefixEdit->text().trimmed();
    if (m_startNumber) m_organizerState.start_number = m_startNumber->value();
    if (m_digits) m_organizerState.digits = m_digits->value();
    if (m_separatorEdit) m_organizerState.separator = m_separatorEdit->text();
    m_organizerState.manual_order_paths = m_manualOrderPaths;
    Organizer::saveOrganizerState(m_organizerState);
}

QMap<QString, QString> ReviewWindow::currentOutputNames() {
    OrganizerState liveState = m_organizerState;
    if (m_prefixEdit) liveState.prefix = m_prefixEdit->text().trimmed();
    if (m_startNumber) liveState.start_number = m_startNumber->value();
    if (m_digits) liveState.digits = m_digits->value();
    if (m_separatorEdit) liveState.separator = m_separatorEdit->text();

    QMap<QString, QString> result;
    for (int idx = 0; idx < m_proposedRecords.size(); ++idx) {
        const auto &rec = m_proposedRecords[idx];
        QString canonical = QFileInfo(rec.source_path).canonicalFilePath();
        if (canonical.isEmpty()) canonical = QFileInfo(rec.source_path).absoluteFilePath();
        result[canonical] = Organizer::generatedName(rec, idx + 1, liveState);
    }
    return result;
}

void ReviewWindow::buildUi() {
    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(24, 18, 24, 18);
    root->setSpacing(14);

    QHBoxLayout *header = new QHBoxLayout();
    QPushButton *back = new QPushButton("←  Folders", this);
    back->setObjectName("quiet");
    connect(back, &QPushButton::clicked, this, &ReviewWindow::close);
    header->addWidget(back);

    QVBoxLayout *titleBox = new QVBoxLayout();
    QLabel *title = new QLabel("Review library", this);
    title->setStyleSheet("font-size:24px; font-weight:700;");
    QLabel *subtitle = new QLabel("One timeline for every photo and video format", this);
    subtitle->setObjectName("subtitle");
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    header->addLayout(titleBox);
    header->addStretch();

    m_matchBadge = new QLabel(this);
    m_matchBadge->setStyleSheet(
        "background: #183324; color: #82c6a0; border: 1px solid #2b573c; "
        "border-radius: 8px; padding: 5px 12px; font-size: 12px; font-weight: 700;"
    );
    m_matchBadge->hide();
    header->addWidget(m_matchBadge);

    m_scanLocation = new QLabel(QString("%1 files").arg(m_originalRecords.size()), this);
    m_scanLocation->setObjectName("muted");
    header->addWidget(m_scanLocation);
    root->addLayout(header);

    root->addWidget(makeInspectorPanel());

    QHBoxLayout *filterRow = new QHBoxLayout();
    QLabel *filterLabel = new QLabel("FILTER", this);
    filterLabel->setObjectName("eyebrow");
    filterRow->addWidget(filterLabel);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search filename, date, format or folder");
    m_search->setClearButtonEnabled(true);
    filterRow->addWidget(m_search, 1);
    connect(m_search, &QLineEdit::textChanged, this, &ReviewWindow::refreshTables);
    root->addLayout(filterRow);

    QFrame *namingPanel = make_card(this);
    QHBoxLayout *namingRow = new QHBoxLayout(namingPanel);
    namingRow->setContentsMargins(14, 10, 14, 10);
    namingRow->setSpacing(9);

    QLabel *prefixLabel = new QLabel("PREFIX", this);
    prefixLabel->setObjectName("eyebrow");
    namingRow->addWidget(prefixLabel);

    m_prefixEdit = new QLineEdit(m_organizerState.prefix, this);
    m_prefixEdit->setPlaceholderText("e.g. SL2024");
    m_prefixEdit->setMaximumWidth(190);
    namingRow->addWidget(m_prefixEdit);

    QLabel *startLabel = new QLabel("START", this);
    startLabel->setObjectName("eyebrow");
    namingRow->addWidget(startLabel);

    m_startNumber = new QSpinBox(this);
    m_startNumber->setRange(1, 99999999);
    m_startNumber->setValue(std::max(1, m_organizerState.start_number));
    m_startNumber->setMaximumWidth(86);
    namingRow->addWidget(m_startNumber);

    QLabel *digitsLabel = new QLabel("DIGITS", this);
    digitsLabel->setObjectName("eyebrow");
    namingRow->addWidget(digitsLabel);

    m_digits = new QSpinBox(this);
    m_digits->setRange(1, 8);
    m_digits->setValue(std::max(1, std::min(8, m_organizerState.digits)));
    m_digits->setMaximumWidth(70);
    namingRow->addWidget(m_digits);

    QLabel *sepLabel = new QLabel("SEPARATOR", this);
    sepLabel->setObjectName("eyebrow");
    namingRow->addWidget(sepLabel);

    m_separatorEdit = new QLineEdit(m_organizerState.separator, this);
    m_separatorEdit->setMaxLength(4);
    m_separatorEdit->setMaximumWidth(58);
    namingRow->addWidget(m_separatorEdit);

    namingRow->addStretch();

    m_resetOrderButton = new QPushButton("Reset date order", this);
    m_resetOrderButton->setObjectName("quiet");
    namingRow->addWidget(m_resetOrderButton);

    m_organizeButton = new QPushButton("Apply & Organise  →", this);
    m_organizeButton->setObjectName("primary");
    namingRow->addWidget(m_organizeButton);
    root->addWidget(namingPanel);

    QSplitter *listsSplitter = new QSplitter(Qt::Horizontal, this);
    m_originalTable = createTable(false);
    m_proposedTable = static_cast<ReorderableTableWidget*>(createTable(true));

    listsSplitter->addWidget(makeTablePanel("Original order", "As supplied · natural filename order", m_originalTable, false));
    listsSplitter->addWidget(makeTablePanel("Chronological order", "Capture date and time · all formats together", m_proposedTable, true));
    listsSplitter->setChildrenCollapsible(false);
    listsSplitter->setSizes({1, 1});
    root->addWidget(listsSplitter, 1);

    setCentralWidget(central);
    statusBar()->showMessage("Select a file to inspect it. Use Edit date to correct missing or incorrect capture information.");

    connect(m_originalTable, &QTableWidget::currentCellChanged, this, [this](int r, int, int, int) {
        tableSelection(m_originalTable, r);
    });
    connect(m_proposedTable, &QTableWidget::currentCellChanged, this, [this](int r, int, int, int) {
        tableSelection(m_proposedTable, r);
    });
    connect(m_originalTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        openDateEditor();
    });
    connect(m_proposedTable, &QTableWidget::cellDoubleClicked, this, &ReviewWindow::proposedCellDoubleClicked);
    connect(m_proposedTable, &ReorderableTableWidget::order_changed, this, &ReviewWindow::onManualOrderChanged);

    connect(m_prefixEdit, &QLineEdit::textChanged, this, &ReviewWindow::prefixSettingChanged);
    connect(m_startNumber, &QSpinBox::valueChanged, this, &ReviewWindow::namingSettingsChanged);
    connect(m_digits, &QSpinBox::valueChanged, this, &ReviewWindow::namingSettingsChanged);
    connect(m_separatorEdit, &QLineEdit::textChanged, this, &ReviewWindow::namingSettingsChanged);
    connect(m_resetOrderButton, &QPushButton::clicked, this, &ReviewWindow::resetChronologicalOrder);
    connect(m_organizeButton, &QPushButton::clicked, this, &ReviewWindow::beginOrganisation);
}

QFrame* ReviewWindow::makeInspectorPanel() {
    QFrame *panel = make_card(this);
    panel->setFixedHeight(184);
    QHBoxLayout *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(16);

    m_preview = new QLabel("Select a file", this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFixedSize(132, 150);
    m_preview->setStyleSheet("background:#1f2022; border:1px solid #45464a; border-radius:8px; color:#a1a1aa;");
    layout->addWidget(m_preview);

    QVBoxLayout *info = new QVBoxLayout();
    info->setSpacing(5);
    m_previewName = new QLabel("No file selected", this);
    m_previewName->setStyleSheet("font-size:15px; font-weight:650; color:#f4f4f5;");
    m_previewName->setWordWrap(true);
    m_previewMeta = new QLabel("Choose a row to inspect", this);
    m_previewMeta->setObjectName("muted");
    info->addWidget(m_previewName);
    info->addWidget(m_previewMeta);

    m_metadataText = new QPlainTextEdit(this);
    m_metadataText->setReadOnly(true);
    m_metadataText->setMaximumHeight(104);
    m_metadataText->setPlaceholderText("Capture date, camera and sequence context");
    m_metadataText->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_metadataText->setStyleSheet("QPlainTextEdit { background:transparent; border:0; padding:0; }");
    info->addWidget(m_metadataText, 1);
    layout->addLayout(info, 1);

    QVBoxLayout *actions = new QVBoxLayout();
    m_editDateButton = new QPushButton("Edit date…", this);
    m_editDateButton->setObjectName("quiet");
    m_editDateButton->setEnabled(false);
    connect(m_editDateButton, &QPushButton::clicked, this, &ReviewWindow::openDateEditor);

    m_editNameButton = new QPushButton("Edit name…", this);
    m_editNameButton->setObjectName("quiet");
    m_editNameButton->setEnabled(false);
    connect(m_editNameButton, &QPushButton::clicked, this, &ReviewWindow::editSelectedOutputName);

    actions->addStretch();
    actions->addWidget(m_editDateButton);
    actions->addWidget(m_editNameButton);
    actions->addStretch();
    layout->addLayout(actions);

    return panel;
}

QFrame* ReviewWindow::makeTablePanel(const QString &titleText, const QString &captionText, QTableWidget *table, bool isChronological) {
    QFrame *panel = make_card(this);
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(7);

    QHBoxLayout *heading = new QHBoxLayout();
    QLabel *title = new QLabel(titleText, this);
    title->setObjectName("sectionTitle");
    heading->addWidget(title);

    if (isChronological) {
        m_chronologicalMatchTag = new QLabel("✓ Same as original", this);
        m_chronologicalMatchTag->setStyleSheet(
            "background: #183324; color: #82c6a0; border: 1px solid #2b573c; "
            "border-radius: 6px; padding: 2px 8px; font-size: 11px; font-weight: 700; margin-left: 6px;"
        );
        m_chronologicalMatchTag->hide();
        heading->addWidget(m_chronologicalMatchTag);
    }

    heading->addStretch();

    QLabel *count = new QLabel(QString("%1 files").arg(m_originalRecords.size()), this);
    count->setObjectName("muted");
    heading->addWidget(count);
    layout->addLayout(heading);

    QLabel *caption = new QLabel(captionText, this);
    caption->setObjectName("muted");
    layout->addWidget(caption);
    layout->addWidget(table, 1);
    return panel;
}

QTableWidget* ReviewWindow::createTable(bool proposed) {
    QTableWidget *table = proposed ? new ReorderableTableWidget(0, 6, this) : new QTableWidget(0, 5, this);
    if (proposed) {
        table->setHorizontalHeaderLabels({"#", "Original file", "Date / time taken", "Format", "Final filename", "Review"});
    } else {
        table->setHorizontalHeaderLabels({"#", "Original filename", "Date / time taken", "Format", "Review"});
    }
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(43);
    table->setWordWrap(false);

    QHeaderView *header = table->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    if (proposed) {
        header->setSectionResizeMode(4, QHeaderView::Stretch);
        header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    } else {
        header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    }
    return table;
}

QList<PhotoRecord> ReviewWindow::filtered(const QList<PhotoRecord> &records) {
    QString query = m_search ? m_search->text().trimmed().toLower() : "";
    if (query.isEmpty()) return records;

    QList<PhotoRecord> result;
    for (const auto &r : records) {
        QString haystack = QString("%1 %2 %3 %4 %5 %6 %7")
                           .arg(r.filename, r.relative_path, r.capture_date, r.capture_time, r.extension, r.media_kind, effective_status(r))
                           .toLower();
        if (haystack.contains(query)) {
            result.append(r);
        }
    }
    return result;
}

void ReviewWindow::populateTable(QTableWidget *table, const QList<PhotoRecord> &records, bool original) {
    table->setUpdatesEnabled(false);
    table->setRowCount(0);
    QMap<QString, int> rowMap;
    auto filteredRecords = filtered(records);
    auto outputNames = !original ? currentOutputNames() : QMap<QString, QString>();

    for (const auto &rec : filteredRecords) {
        int row = table->rowCount();
        table->insertRow(row);

        int order = original ? rec.source_order : rec.proposed_order;
        QString timestamp = original ? original_timestamp(rec) : proposed_timestamp(rec);
        QString status;
        QStringList values;

        if (original) {
            if (!rec.capture_date.isEmpty() && !rec.capture_time.isEmpty()) status = "HAS DATE";
            else if (!rec.capture_date.isEmpty()) status = "DATE ONLY";
            else status = "MISSING";
            values = {QString::number(order), rec.filename, timestamp, rec.extension.mid(1).toUpper(), status};
        } else {
            status = effective_status(rec);
            QString canonical = QFileInfo(rec.source_path).canonicalFilePath();
            if (canonical.isEmpty()) canonical = QFileInfo(rec.source_path).absoluteFilePath();
            QString finalName = outputNames.value(canonical, rec.filename);
            values = {QString::number(order), rec.filename, timestamp, rec.extension.mid(1).toUpper(), finalName, status};
        }

        for (int col = 0; col < values.size(); ++col) {
            QTableWidgetItem *item = new QTableWidgetItem(values[col]);
            if (col == 0) {
                item->setData(Qt::UserRole, rec.source_path);
                item->setTextAlignment(Qt::AlignCenter);
            }
            if (!original && col == 4) {
                item->setToolTip("Double-click to set a custom output filename");
            }
            int statusCol = original ? 4 : 5;
            if (col == statusCol) {
                if (status == "MISSING") item->setForeground(QColor("#ffad84"));
                else if (status == "DATE ONLY") item->setForeground(QColor("#edc77b"));
                else if (status == "MANUAL") item->setForeground(QColor("#86cba7"));
                else item->setForeground(QColor("#9fb3cc"));
            }
            if (qobject_cast<ReorderableTableWidget*>(table)) {
                item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            }
            table->setItem(row, col, item);
        }
        rowMap[rec.source_path] = row;
    }
    m_tableRowMaps[reinterpret_cast<quintptr>(table)] = rowMap;
    table->setUpdatesEnabled(true);
}

void ReviewWindow::refreshTables() {
    if (!m_originalTable) return;
    QString currentPath = m_selectedPath;
    recalculateOrder();
    m_syncing = true;

    QList<PhotoRecord> sortedOriginal = m_originalRecords;
    std::sort(sortedOriginal.begin(), sortedOriginal.end(), [](const PhotoRecord &a, const PhotoRecord &b) {
        return a.source_order < b.source_order;
    });

    populateTable(m_originalTable, sortedOriginal, true);
    populateTable(m_proposedTable, m_proposedRecords, false);
    m_syncing = false;

    if (!currentPath.isEmpty() && m_recordMap.contains(currentPath)) {
        selectPath(currentPath);
    }
    updateMatchIndicator();
}

void ReviewWindow::prefixSettingChanged(const QString &val) {
    QString newPrefix = val.trimmed();
    QString oldPrefix = m_organizerState.prefix.trimmed();
    if (newPrefix != oldPrefix) {
        m_organizerState.name_overrides.clear();
    }
    namingSettingsChanged();
    if (!newPrefix.isEmpty() && newPrefix != oldPrefix) {
        statusBar()->showMessage(QString("Batch prefix changed to '%1'. All proposed names use this prefix.").arg(newPrefix), 5000);
    }
}

void ReviewWindow::namingSettingsChanged() {
    try {
        persistOrganizerState();
        refreshOutputNames();
    } catch (const std::exception &exc) {
        statusBar()->showMessage(exc.what(), 5000);
    }
}

void ReviewWindow::refreshOutputNames() {
    if (!m_proposedTable) return;
    QMap<QString, QString> names;
    try {
        names = currentOutputNames();
    } catch (const std::exception &exc) {
        statusBar()->showMessage(exc.what(), 5000);
        return;
    }

    auto rowMap = m_tableRowMaps.value(reinterpret_cast<quintptr>(m_proposedTable));
    for (auto it = rowMap.constBegin(); it != rowMap.constEnd(); ++it) {
        QString path = it.key();
        int row = it.value();
        auto item = m_proposedTable->item(row, 4);
        if (item && m_recordMap.contains(path)) {
            QString canonical = QFileInfo(path).canonicalFilePath();
            if (canonical.isEmpty()) canonical = QFileInfo(path).absoluteFilePath();
            QString outName = names.value(canonical, m_recordMap[path].filename);
            item->setText(outName);
            m_recordMap[path].final_output_name = outName;
        }
    }
}

void ReviewWindow::editSelectedOutputName() {
    if (m_selectedPath.isEmpty() || !m_recordMap.contains(m_selectedPath)) return;
    PhotoRecord &record = m_recordMap[m_selectedPath];

    QString selectedCanonical = QFileInfo(m_selectedPath).canonicalFilePath();
    if (selectedCanonical.isEmpty()) selectedCanonical = QFileInfo(m_selectedPath).absoluteFilePath();

    QString current = record.final_output_name.isEmpty() ? record.filename : record.final_output_name;
    QString prompt = QString("New name for %1 (this file keeps its %2 extension):").arg(record.filename, record.extension.isEmpty() ? "(original)" : record.extension);
    QString title = "Custom output filename";

    bool ok = false;
    QString value = QInputDialog::getText(this, title, prompt, QLineEdit::Normal, current, &ok);
    if (!ok) return;

    value = value.trimmed();
    if (value.isEmpty()) {
        QMessageBox::warning(this, "Invalid filename", "The output filename cannot be empty.");
        return;
    }

    QString ext = record.extension;
    QFileInfo valFi(value);
    QString suppliedExt = valFi.suffix().isEmpty() ? "" : ("." + valFi.suffix());
    if (!suppliedExt.isEmpty() && suppliedExt.compare(ext, Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, "Keep the original format",
            QString("This file must keep its %1 extension. The extension is preserved automatically for every file.").arg(ext.isEmpty() ? "(no)" : ext));
        return;
    }

    if (suppliedExt.isEmpty() && !ext.isEmpty()) {
        value += ext;
    }

    if (value == current) return;
    m_organizerState.name_overrides[selectedCanonical] = value;

    persistOrganizerState();
    refreshTables();
    selectPath(m_selectedPath);
}

void ReviewWindow::proposedCellDoubleClicked(int row, int col) {
    if (col == 4) {
        auto item = m_proposedTable->item(row, 0);
        if (item) {
            QString path = item->data(Qt::UserRole).toString();
            if (!path.isEmpty()) {
                selectPath(path, m_proposedTable);
                editSelectedOutputName();
            }
        }
    } else {
        openDateEditor();
    }
}

void ReviewWindow::onManualOrderChanged(const QStringList &paths) {
    if (m_search && !m_search->text().trimmed().isEmpty()) {
        statusBar()->showMessage("Clear the search filter before manually reordering the full list.", 5000);
        refreshTables();
        return;
    }
    if (paths.size() != m_originalRecords.size()) {
        refreshTables();
        return;
    }
    m_manualOrderPaths = paths;
    persistOrganizerState();
    recalculateOrder();
    refreshTables();
    saveReport();
    statusBar()->showMessage("Manual order saved. Drag rows in the chronological list to adjust it.");
}

void ReviewWindow::resetChronologicalOrder() {
    m_manualOrderPaths.clear();
    persistOrganizerState();
    recalculateOrder();
    refreshTables();
    saveReport();
    statusBar()->showMessage("Restored chronological order from capture dates and times.");
}

void ReviewWindow::beginOrganisation() {
    try {
        persistOrganizerState();
        auto outputNames = currentOutputNames();
        QString destination = QFileDialog::getExistingDirectory(this, "Choose a new destination library", QDir::homePath());
        if (destination.isEmpty()) return;

        auto plan = Organizer::buildPlan(m_proposedRecords, destination, outputNames);

        QStringList sampleList;
        for (int i = 0; i < std::min(8, (int)plan.size()); ++i) {
            QDir destDir(destination);
            sampleList.append(QString("%1  →  %2").arg(QFileInfo(plan[i].source).fileName(), destDir.relativeFilePath(plan[i].target)));
        }
        if (plan.size() > 8) {
            sampleList.append(QString("…and %1 more files").arg(plan.size() - 8));
        }

        QString prefix = m_prefixEdit->text().trimmed();
        QString sep = m_separatorEdit->text();
        int startNum = m_startNumber->value();
        int digits = m_digits->value();
        QString namingSummary = QString("Naming pattern: %1%2%3, %4 digits\n")
                                .arg(prefix, sep, QString("%1").arg(startNum, digits, 10, QChar('0'))).arg(digits);

        QString msg = QString("ShotSequence will COPY %1 files into:\n%2\n\n%3"
                              "Files will be grouped by year/month. Original formats are retained. "
                              "Only manually corrected capture dates are written into output copies. "
                              "Source files will not be moved, renamed or modified.\n\n"
                              "Preview:\n%4\n\nContinue?")
                      .arg(plan.size()).arg(destination, namingSummary, sampleList.join('\n'));

        auto ans = QMessageBox::question(this, "Confirm organisation", msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes) return;

        m_organizeButton->setEnabled(false);
        m_organizeProgress = new QProgressDialog("Preparing library…", QString(), 0, plan.size(), this);
        m_organizeProgress->setWindowTitle("ShotSequence");
        m_organizeProgress->setWindowModality(Qt::WindowModal);
        m_organizeProgress->setMinimumDuration(0);
        m_organizeProgress->setCancelButton(nullptr);
        m_organizeProgress->show();

        m_organizeWorker = new OrganizeWorker(m_proposedRecords, destination, outputNames, this);
        connect(m_organizeWorker, &OrganizeWorker::progress_changed, this, &ReviewWindow::organisationProgressChanged);
        connect(m_organizeWorker, &OrganizeWorker::completed, this, &ReviewWindow::organisationCompleted);
        connect(m_organizeWorker, &OrganizeWorker::failed, this, &ReviewWindow::organisationFailed);
        connect(m_organizeWorker, &OrganizeWorker::finished, m_organizeWorker, &QObject::deleteLater);
        m_organizeWorker->start();
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, "Cannot organise yet", exc.what());
    }
}

void ReviewWindow::organisationProgressChanged(int current, int total, const QString &filename) {
    if (m_organizeProgress) {
        m_organizeProgress->setMaximum(total);
        m_organizeProgress->setValue(current - 1);
        m_organizeProgress->setLabelText(QString("Copying and verifying %1/%2\n%3").arg(current).arg(total).arg(filename));
    }
}

void ReviewWindow::organisationCompleted(const QJsonObject &report) {
    if (m_organizeProgress) {
        m_organizeProgress->setValue(m_organizeProgress->maximum());
        m_organizeProgress->close();
    }
    m_organizeButton->setEnabled(true);
    QString repPath = QDir(Organizer::dataDir()).filePath("last_organize_report.json");
    QMessageBox::information(this, "Organisation finished",
        QString("Copied: %1\nFailed: %2\nMetadata warnings: %3\n\nReport: %4")
        .arg(report.value("copied").toInt())
        .arg(report.value("failed").toInt())
        .arg(report.value("metadata_warnings").toInt())
        .arg(repPath));
    statusBar()->showMessage(QString("Organisation finished: %1 copied, %2 failed.")
                             .arg(report.value("copied").toInt())
                             .arg(report.value("failed").toInt()));
}

void ReviewWindow::organisationFailed(const QString &message) {
    if (m_organizeProgress) {
        m_organizeProgress->close();
    }
    m_organizeButton->setEnabled(true);
    QMessageBox::critical(this, "Organisation failed", message);
}

void ReviewWindow::tableSelection(QTableWidget *table, int row) {
    if (m_syncing || row < 0) return;
    auto it = table->item(row, 0);
    if (!it) return;
    QString path = it->data(Qt::UserRole).toString();
    if (!path.isEmpty()) {
        selectPath(path, table);
    }
}

void ReviewWindow::selectPath(const QString &path, QTableWidget *sourceTable) {
    m_selectedPath = path;
    m_syncing = true;
    for (QTableWidget *t : {m_originalTable, static_cast<QTableWidget*>(m_proposedTable)}) {
        if (!t) continue;
        auto rowMap = m_tableRowMaps.value(reinterpret_cast<quintptr>(t));
        if (rowMap.contains(path)) {
            int row = rowMap[path];
            if (row < t->rowCount()) {
                t->setCurrentCell(row, 0);
            }
        }
    }
    m_syncing = false;
    showRecord(path);
}

QString ReviewWindow::sequenceNeighbours(const PhotoRecord &record) {
    QString path = record.source_path;
    if (m_sequenceContextCache.contains(path)) {
        return m_sequenceContextCache.value(path);
    }

    struct Entry {
        int num;
        PhotoRecord rec;
    };
    QMap<QString, QList<Entry>> groups;
    for (const auto &item : m_originalRecords) {
        auto grp = sequence_group(item);
        if (grp.valid) {
            QString key = QString("%1||%2||%3").arg(grp.parent, grp.prefix, grp.suffix);
            groups[key].append({grp.number, item});
        }
    }

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        auto &entries = it.value();
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            return a.num < b.num;
        });

        for (int i = 0; i < entries.size(); ++i) {
            QString prevLabel = "Not found";
            QString nextLabel = "Not found";
            if (i > 0) prevLabel = QString("%1 — %2").arg(entries[i - 1].rec.filename, original_timestamp(entries[i - 1].rec));
            if (i + 1 < entries.size()) nextLabel = QString("%1 — %2").arg(entries[i + 1].rec.filename, original_timestamp(entries[i + 1].rec));

            QString text = QString("Previous number: %1\nNext number: %2\n\nNeighbours are clues only; they do not prove capture date or time.")
                           .arg(prevLabel, nextLabel);
            m_sequenceContextCache[entries[i].rec.source_path] = text;
        }
    }

    if (!m_sequenceContextCache.contains(path)) {
        m_sequenceContextCache[path] = "No numbered filename sequence could be identified.";
    }
    return m_sequenceContextCache.value(path);
}

void ReviewWindow::showRecord(const QString &path) {
    if (!m_recordMap.contains(path)) return;
    const PhotoRecord &rec = m_recordMap[path];

    m_previewName->setText(rec.filename);
    m_previewMeta->setText(QString("%1 · %2 · %3 · Original #%4 · Proposed #%5")
                           .arg(rec.media_kind, rec.extension.mid(1).toUpper(),
                                format_size(rec.file_size),
                                QString::number(rec.source_order),
                                rec.proposed_order > 0 ? QString::number(rec.proposed_order) : "—"));
    showPreview(rec);

    QString manualStr;
    if (!rec.manual_date.isEmpty()) {
        manualStr = QString(" · Manual: %1 %2").arg(rec.manual_date, !rec.manual_time.isEmpty() ? rec.manual_time : "(time unknown)");
    }

    QStringList lines;
    lines << QString("Capture: %1  %2%3")
             .arg(!rec.capture_date.isEmpty() ? rec.capture_date : "No date",
                  !rec.capture_time.isEmpty() ? rec.capture_time : "No time",
                  manualStr);
    lines << QString("Camera: %1 %2  ·  GPS: %3")
             .arg(!rec.camera_make.isEmpty() ? rec.camera_make : "—",
                  rec.camera_model,
                  !rec.gps_position.isEmpty() ? rec.gps_position : "—");
    lines << QString("Sequence: %1").arg(sequenceNeighbours(rec).replace('\n', "  "));
    lines << QString("Metadata: %1  ·  Raw: %2")
             .arg(!rec.capture_tag.isEmpty() ? rec.capture_tag : "No capture-date tag",
                  !rec.capture_raw.isEmpty() ? rec.capture_raw : "None");

    m_metadataText->setPlainText(lines.join('\n'));
    m_editDateButton->setEnabled(true);
    m_editNameButton->setEnabled(true);
}

void ReviewWindow::showPreview(const PhotoRecord &record) {
    m_previewTimer->stop();
    m_previewRequestedPath = "";

    if (record.media_kind == "VIDEO") {
        m_preview->setPixmap(QPixmap());
        m_preview->setText("VIDEO\n\nPreview not generated");
        return;
    }

    QString path = record.source_path;
    if (m_previewCache.contains(path)) {
        m_previewCacheOrder.removeAll(path);
        m_previewCacheOrder.append(path);
        setPreviewImage(m_previewCache[path]);
        return;
    }

    m_preview->setPixmap(QPixmap());
    m_preview->setText("Loading preview…");
    m_previewRequestedPath = path;
    m_previewTimer->start();
}

void ReviewWindow::startPreviewRequest() {
    QString path = m_previewRequestedPath;
    if (path.isEmpty() || m_previewInFlight.contains(path)) return;
    m_previewRequestedPath = "";
    m_previewInFlight.insert(path);

    PreviewWorker *worker = new PreviewWorker(path, m_previewSize, m_previewSignals, m_previewCancelToken);
    m_previewPool->start(worker);
}

void ReviewWindow::onPreviewReady(const QString &path, const QImage &image, const QString &error) {
    m_previewInFlight.remove(path);

    if (!image.isNull()) {
        m_previewCache[path] = image;
        m_previewCacheOrder.removeAll(path);
        m_previewCacheOrder.append(path);
        while (m_previewCacheOrder.size() > m_previewCacheLimit) {
            QString oldest = m_previewCacheOrder.takeFirst();
            m_previewCache.remove(oldest);
        }
    }

    if (m_selectedPath != path) return;

    if (!image.isNull()) {
        setPreviewImage(image);
    } else {
        m_preview->setPixmap(QPixmap());
        m_preview->setText("Preview unavailable");
        if (!error.isEmpty()) {
            statusBar()->showMessage(QString("Preview failed: %1").arg(error), 5000);
        }
    }
}

void ReviewWindow::setPreviewImage(const QImage &image) {
    QPixmap pixmap = QPixmap::fromImage(image);
    m_preview->setText("");
    m_preview->setPixmap(pixmap.scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ReviewWindow::openDateEditor() {
    if (m_selectedPath.isEmpty() || !m_recordMap.contains(m_selectedPath)) return;
    PhotoRecord &record = m_recordMap[m_selectedPath];

    QDialog dialog(this);
    dialog.setWindowTitle("Edit capture date");
    dialog.setMinimumWidth(400);
    dialog.setStyleSheet(DARK_STYLE);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(14);

    QLabel *filename = new QLabel(record.filename, &dialog);
    filename->setStyleSheet("font-size:16px; font-weight:650;");
    filename->setWordWrap(true);
    layout->addWidget(filename);

    QLabel *helpText = new QLabel("Set the date and, if known, the exact capture time. This changes the proposed order only; the source file remains untouched.", &dialog);
    helpText->setObjectName("muted");
    helpText->setWordWrap(true);
    layout->addWidget(helpText);

    QFormLayout *form = new QFormLayout();
    form->setVerticalSpacing(12);

    QDateEdit *dateEdit = new QDateEdit(&dialog);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("dd MMM yyyy");
    dateEdit->setMinimumDate(QDate(1900, 1, 1));
    dateEdit->setMaximumDate(QDate(2100, 12, 31));

    QString curDateStr = effective_date(record);
    QDate curDate = QDate::fromString(curDateStr, Qt::ISODate);
    dateEdit->setDate(curDate.isValid() ? curDate : QDate(2024, 1, 1));

    QCheckBox *timeKnown = new QCheckBox("I know the exact time", &dialog);
    QString curTimeStr = effective_time(record);
    timeKnown->setChecked(!curTimeStr.isEmpty());

    QTimeEdit *timeEdit = new QTimeEdit(&dialog);
    timeEdit->setDisplayFormat("HH:mm:ss");
    QTime curTime = QTime::fromString(curTimeStr, "HH:mm:ss");
    timeEdit->setTime(curTime.isValid() ? curTime : QTime(12, 0, 0));
    timeEdit->setEnabled(timeKnown->isChecked());
    connect(timeKnown, &QCheckBox::toggled, timeEdit, &QTimeEdit::setEnabled);

    form->addRow("Capture date", dateEdit);
    form->addRow("Capture time", timeKnown);
    form->addRow("", timeEdit);
    layout->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText("Save correction");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (!record.manual_date.isEmpty()) {
        QPushButton *removeButton = new QPushButton("Remove correction", &dialog);
        removeButton->setObjectName("quiet");
        layout->insertWidget(layout->count() - 1, removeButton);
        connect(removeButton, &QPushButton::clicked, [&]() {
            dialog.reject();
            clearCorrection(m_selectedPath);
        });
    }

    if (dialog.exec() != QDialog::Accepted) return;

    if (dateEdit->date() == dateEdit->minimumDate()) {
        QMessageBox::warning(this, "Choose a date", "Select a capture date before saving.");
        return;
    }

    QString manualDate = dateEdit->date().toString(Qt::ISODate);
    QString manualTime = timeKnown->isChecked() ? timeEdit->time().toString("HH:mm:ss") : "";

    Database::saveManualDate(m_selectedPath, manualDate, manualTime);
    record.manual_date = manualDate;
    record.manual_time = manualTime;
    for (auto &r : m_originalRecords) {
        if (r.source_path == m_selectedPath) {
            r.manual_date = manualDate;
            r.manual_time = manualTime;
        }
    }

    recalculateOrder();
    saveReport();
    refreshTables();
    statusBar()->showMessage(QString("Date correction saved for %1.").arg(record.filename));
}

void ReviewWindow::clearCorrection(const QString &path) {
    QString target = path.isEmpty() ? m_selectedPath : path;
    if (target.isEmpty() || !m_recordMap.contains(target)) return;

    PhotoRecord &record = m_recordMap[target];
    Database::deleteManualDate(target);
    record.manual_date = "";
    record.manual_time = "";
    for (auto &r : m_originalRecords) {
        if (r.source_path == target) {
            r.manual_date = "";
            r.manual_time = "";
        }
    }

    recalculateOrder();
    saveReport();
    refreshTables();
    statusBar()->showMessage(QString("Manual correction removed for %1.").arg(record.filename));
}

void ReviewWindow::saveReport() {
    QJsonObject payload;
    payload["scan_version"] = 2;
    payload["sorting_policy"] = "One unified chronology across all image and video formats";
    payload["total_files"] = m_originalRecords.size();

    auto recordToJson = [](const PhotoRecord &r) {
        QJsonObject obj;
        obj["source_path"] = r.source_path;
        obj["relative_path"] = r.relative_path;
        obj["filename"] = r.filename;
        obj["extension"] = r.extension;
        obj["media_kind"] = r.media_kind;
        obj["file_size"] = r.file_size;
        obj["filesystem_modified"] = r.filesystem_modified;
        obj["source_order"] = r.source_order;
        obj["proposed_order"] = r.proposed_order;
        obj["capture_date"] = r.capture_date;
        obj["capture_time"] = r.capture_time;
        obj["capture_timezone"] = r.capture_timezone;
        obj["capture_raw"] = r.capture_raw;
        obj["capture_tag"] = r.capture_tag;
        obj["date_status"] = r.date_status;
        obj["camera_make"] = r.camera_make;
        obj["camera_model"] = r.camera_model;
        obj["lens"] = r.lens;
        obj["gps_position"] = r.gps_position;
        obj["width"] = r.width;
        obj["height"] = r.height;
        if (!r.manual_date.isEmpty()) obj["manual_date"] = r.manual_date;
        if (!r.manual_time.isEmpty()) obj["manual_time"] = r.manual_time;
        obj["metadata"] = r.metadata;
        return obj;
    };

    QJsonArray origArr;
    for (const auto &r : m_originalRecords) origArr.append(recordToJson(r));
    payload["original_order"] = origArr;

    QJsonArray propArr;
    for (const auto &r : m_proposedRecords) propArr.append(recordToJson(r));
    payload["proposed_order"] = propArr;

    QJsonArray warnArr;
    for (const auto &w : m_warnings) warnArr.append(w);
    payload["warnings"] = warnArr;

    QString reportPath = QDir(Organizer::dataDir()).filePath("last_scan.json");
    QFile repFile(reportPath);
    if (repFile.open(QIODevice::WriteOnly)) {
        repFile.write(QJsonDocument(payload).toJson(QJsonDocument::Indented));
    }
}

void ReviewWindow::closeEvent(QCloseEvent *event) {
    if (m_organizeWorker && m_organizeWorker->isRunning()) {
        QMessageBox::warning(this, "Organisation in progress",
            "Please wait for file organisation to finish before closing.");
        event->ignore();
        return;
    }
    if (m_previewCancelToken) {
        m_previewCancelToken->store(true);
    }
    if (m_previewTimer) m_previewTimer->stop();
    if (m_previewPool) {
        m_previewPool->clear();
        m_previewPool->waitForDone();
    }
    if (m_launcher) {
        m_launcher->show();
        m_launcher->raise();
        m_launcher->activateWindow();
    }
    event->accept();
}

bool ReviewWindow::isOrderIdentical() const {
    if (m_originalRecords.size() != m_proposedRecords.size()) return false;
    if (m_originalRecords.isEmpty()) return false;

    QList<PhotoRecord> sortedOriginal = m_originalRecords;
    std::sort(sortedOriginal.begin(), sortedOriginal.end(), [](const PhotoRecord &a, const PhotoRecord &b) {
        return a.source_order < b.source_order;
    });

    for (int i = 0; i < sortedOriginal.size(); ++i) {
        if (sortedOriginal[i].source_path != m_proposedRecords[i].source_path) {
            return false;
        }
    }
    return true;
}

void ReviewWindow::updateMatchIndicator() {
    bool same = isOrderIdentical();
    if (m_matchBadge) {
        if (same) {
            m_matchBadge->setText("✓  Orders Match (No reordering needed)");
            m_matchBadge->setToolTip("The natural filename order and chronological capture-date order are already identical.");
            m_matchBadge->show();
        } else {
            m_matchBadge->hide();
        }
    }
    if (m_chronologicalMatchTag) {
        if (same) {
            m_chronologicalMatchTag->show();
        } else {
            m_chronologicalMatchTag->hide();
        }
    }
}
