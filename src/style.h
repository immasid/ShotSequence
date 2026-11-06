#ifndef STYLE_H
#define STYLE_H

#include <QString>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

inline const QString DARK_STYLE = R"(
QMainWindow, QWidget { background:#202123; color:#ececec; font-family:Inter,'Noto Sans','DejaVu Sans',sans-serif; font-size:13px; }
QLabel { background:transparent; }
QLabel#brand { font-size:29px; font-weight:750; }
QLabel#subtitle,QLabel#muted { color:#a1a1aa; font-size:12px; }
QLabel#eyebrow { color:#a1a1aa; font-size:10px; font-weight:700; letter-spacing:1px; }
QLabel#sectionTitle { color:#f4f4f5; font-size:15px; font-weight:650; }
QLabel#metricValue { color:#f4f4f5; font-size:25px; font-weight:750; }
QLabel#metricLabel { color:#a1a1aa; font-size:11px; }
QFrame#surface,QGroupBox#surface { background:#28292b; border:1px solid #414246; border-radius:12px; }
QGroupBox#surface { margin-top:10px; padding:12px; font-weight:650; }
QPushButton { color:#ededed; background:#343538; border:1px solid #4b4c50; border-radius:8px; padding:9px 15px; min-height:20px; font-weight:600; }
QPushButton:hover { background:#414246; border-color:#62636a; }
QPushButton:disabled { color:#77787d; background:#2b2c2e; border-color:#3b3c40; }
QPushButton#primary { background:#e5e5e5; border:1px solid #f0f0f0; color:#202123; padding:10px 17px; }
QPushButton#primary:hover { background:#fff; }
QPushButton#quiet { background:transparent; border-color:#4b4c50; }
QLineEdit,QDateEdit,QTimeEdit,QListWidget,QPlainTextEdit { color:#ececec; background:#222326; border:1px solid #414246; border-radius:8px; padding:8px 10px; selection-background-color:#55565b; }
QLineEdit:focus,QDateEdit:focus,QTimeEdit:focus,QPlainTextEdit:focus { border:1px solid #8b8c91; }
QListWidget::item { padding:9px; border-radius:6px; }
QListWidget::item:selected { background:#45464b; }
QTableWidget { background:#242528; alternate-background-color:#292a2d; color:#ededed; border:1px solid #414246; border-radius:9px; gridline-color:transparent; selection-background-color:#505156; selection-color:white; }
QTableWidget::item { padding:9px 8px; border-bottom:1px solid #37383c; }
QTableWidget::item:selected { background:#505156; }
QHeaderView::section { background:#303135; color:#c4c4c8; border:none; border-bottom:1px solid #48494e; padding:11px 8px; font-size:11px; font-weight:650; }
QCheckBox { spacing:8px; color:#dedee1; }
QProgressBar { background:#38393d; border:none; border-radius:4px; height:6px; text-align:center; }
QProgressBar::chunk { background:#b7b7bc; border-radius:4px; }
QScrollBar:vertical { background:#242528; width:10px; margin:2px; }
QScrollBar::handle:vertical { background:#55565b; min-height:24px; border-radius:5px; }
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
QStatusBar { color:#a1a1aa; background:#202123; border-top:1px solid #3a3b3f; }
)";

inline QFrame* make_card(QWidget *parent = nullptr) {
    QFrame *frame = new QFrame(parent);
    frame->setObjectName("surface");
    return frame;
}

#endif // STYLE_H
