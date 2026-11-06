#include "launcher_window.h"
#include "review_window.h"
#include "scanner.h"
#include "style.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QStatusBar>

ScanWorker::ScanWorker(const QStringList &folders, QObject *parent)
    : QThread(parent), m_folders(folders) {}

void ScanWorker::run() {
    try {
        QList<PhotoRecord> original;
        QList<PhotoRecord> proposed;
        QStringList warnings;
        if (Scanner::scanPaths(m_folders, original, proposed, warnings)) {
            if (!isInterruptionRequested()) {
                emit completed(original, proposed, warnings);
            }
        } else {
            if (!isInterruptionRequested()) {
                emit failed("Failed to complete scan");
            }
        }
    } catch (const std::exception &exc) {
        if (!isInterruptionRequested()) {
            emit failed(QString::fromUtf8(exc.what()));
        }
    }
}

LauncherWindow::LauncherWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("ShotSequence");
    setMinimumSize(780, 600);
    resize(980, 700);
    setStyleSheet(DARK_STYLE);

    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(38, 32, 38, 28);
    root->setSpacing(22);

    QHBoxLayout *top = new QHBoxLayout();
    QVBoxLayout *brandBox = new QVBoxLayout();
    QLabel *brand = new QLabel("ShotSequence", this);
    brand->setObjectName("brand");
    QLabel *subtitle = new QLabel("A careful, metadata-aware photo library organiser", this);
    subtitle->setObjectName("subtitle");
    brandBox->addWidget(brand);
    brandBox->addWidget(subtitle);
    top->addLayout(brandBox);
    top->addStretch();

    QLabel *local = new QLabel("●  LOCAL PROCESSING", this);
    local->setStyleSheet("color:#82c6a0; font-size:10px; font-weight:700; letter-spacing:1px;");
    top->addWidget(local, 0, Qt::AlignTop);
    root->addLayout(top);

    QVBoxLayout *welcome = new QVBoxLayout();
    QLabel *eyebrow = new QLabel("LIBRARY SETUP", this);
    eyebrow->setObjectName("eyebrow");
    QLabel *heading = new QLabel("Choose the folders you want to organise.", this);
    heading->setStyleSheet("font-size:23px; font-weight:700; color:#f2f6fc;");
    QLabel *description = new QLabel(
        "ShotSequence reads capture dates across HEIC, JPG, MP4 and other supported formats. "
        "It compares your original filename order with one unified chronological order. "
        "Your source files are never changed during scanning.", this);
    description->setWordWrap(true);
    description->setObjectName("subtitle");
    welcome->addWidget(eyebrow);
    welcome->addWidget(heading);
    welcome->addWidget(description);
    root->addLayout(welcome);

    QFrame *sourceCard = make_card(this);
    QVBoxLayout *sourceLayout = new QVBoxLayout(sourceCard);
    sourceLayout->setContentsMargins(20, 18, 20, 18);
    sourceLayout->setSpacing(12);

    QHBoxLayout *sourceHeader = new QHBoxLayout();
    QLabel *sourceTitle = new QLabel("Source folders", this);
    sourceTitle->setObjectName("sectionTitle");
    m_folderCount = new QLabel("0 folders selected", this);
    m_folderCount->setObjectName("muted");
    sourceHeader->addWidget(sourceTitle);
    sourceHeader->addStretch();
    sourceHeader->addWidget(m_folderCount);
    sourceLayout->addLayout(sourceHeader);

    m_folderList = new QListWidget(this);
    m_folderList->setMinimumHeight(180);
    m_folderList->setSpacing(3);
    sourceLayout->addWidget(m_folderList);

    QHBoxLayout *buttons = new QHBoxLayout();
    m_addButton = new QPushButton("＋  Add folder", this);
    m_removeButton = new QPushButton("Remove selected", this);
    m_removeButton->setObjectName("quiet");
    buttons->addWidget(m_addButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch();
    sourceLayout->addLayout(buttons);
    root->addWidget(sourceCard, 1);

    QHBoxLayout *bottom = new QHBoxLayout();
    QVBoxLayout *noteBox = new QVBoxLayout();
    QLabel *note = new QLabel("READ-ONLY SCAN  ·  NO RENAMING  ·  NO MOVING", this);
    note->setObjectName("eyebrow");
    QLabel *note2 = new QLabel("Review and approve the proposed order before any organisation happens.", this);
    note2->setObjectName("muted");
    noteBox->addWidget(note);
    noteBox->addWidget(note2);
    bottom->addLayout(noteBox);
    bottom->addStretch();

    QVBoxLayout *actionBox = new QVBoxLayout();
    m_scanButton = new QPushButton("Scan folders and review  →", this);
    m_scanButton->setObjectName("primary");
    m_scanButton->setMinimumWidth(245);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->hide();
    actionBox->addWidget(m_scanButton);
    actionBox->addWidget(m_progress);
    bottom->addLayout(actionBox);
    root->addLayout(bottom);

    setCentralWidget(central);
    statusBar()->showMessage("Ready. Add a source folder to begin.");

    connect(m_addButton, &QPushButton::clicked, this, &LauncherWindow::addFolder);
    connect(m_removeButton, &QPushButton::clicked, this, &LauncherWindow::removeSelected);
    connect(m_scanButton, &QPushButton::clicked, this, &LauncherWindow::startScan);
}

void LauncherWindow::addFolder() {
    QString folder = QFileDialog::getExistingDirectory(this, "Choose a source folder", QDir::homePath());
    if (folder.isEmpty()) return;

    QString resolved = QFileInfo(folder).canonicalFilePath();
    if (resolved.isEmpty()) resolved = QFileInfo(folder).absoluteFilePath();

    if (m_folders.contains(resolved)) return;

    m_folders.append(resolved);
    QListWidgetItem *item = new QListWidgetItem("▰   " + resolved);
    item->setData(Qt::UserRole, resolved);
    m_folderList->addItem(item);
    updateFolderCount();
}

void LauncherWindow::removeSelected() {
    int row = m_folderList->currentRow();
    if (row < 0) return;

    m_folders.removeAt(row);
    delete m_folderList->takeItem(row);
    updateFolderCount();
}

void LauncherWindow::updateFolderCount() {
    int count = m_folders.size();
    m_folderCount->setText(QString("%1 folder%2 selected").arg(count).arg(count != 1 ? "s" : ""));
}

LauncherWindow::~LauncherWindow() {
    if (m_worker) {
        if (m_worker->isRunning()) {
            m_worker->requestInterruption();
            m_worker->wait();
        }
    }
    if (m_reviewWindow) {
        delete m_reviewWindow;
        m_reviewWindow = nullptr;
    }
}

void LauncherWindow::startScan() {
    if (m_folders.isEmpty()) {
        QMessageBox::information(this, "Choose folders", "Add at least one folder first.");
        return;
    }
    m_scanButton->setEnabled(false);
    m_addButton->setEnabled(false);
    m_removeButton->setEnabled(false);
    m_progress->show();
    statusBar()->showMessage("Scanning files and reading metadata…");

    m_worker = new ScanWorker(m_folders, this);
    connect(m_worker, &ScanWorker::completed, this, &LauncherWindow::scanCompleted);
    connect(m_worker, &ScanWorker::failed, this, &LauncherWindow::scanFailed);
    connect(m_worker, &ScanWorker::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}

void LauncherWindow::restoreControls() {
    m_scanButton->setEnabled(true);
    m_addButton->setEnabled(true);
    m_removeButton->setEnabled(true);
    m_progress->hide();
}

void LauncherWindow::scanFailed(const QString &message) {
    restoreControls();
    statusBar()->showMessage("Scan failed");
    QMessageBox::critical(this, "Scan failed", message);
}

void LauncherWindow::scanCompleted(const QList<PhotoRecord> &original, const QList<PhotoRecord> &proposed, const QStringList &warnings) {
    restoreControls();
    if (original.isEmpty()) {
        QMessageBox::information(this, "No supported files", "No supported image or video files were found in the selected folders.");
        return;
    }

    try {
        if (m_reviewWindow) {
            delete m_reviewWindow;
            m_reviewWindow = nullptr;
        }
        m_reviewWindow = new ReviewWindow(original, warnings, this);
        m_reviewWindow->showMaximized();
        hide();
    } catch (const std::exception &exc) {
        QMessageBox::critical(this, "Could not open review", exc.what());
        show();
    }
}
