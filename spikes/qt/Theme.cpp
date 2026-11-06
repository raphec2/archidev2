// SPDX-License-Identifier: GPL-2.0-or-later
#include "Theme.h"

#include <QApplication>
#include <QPalette>

namespace Theme {

QString paneBorder(bool active) {
    return active ? QStringLiteral("border: 1px solid %1;").arg(QLatin1String(BlueActive))
                  : QStringLiteral("border: 1px solid %1;").arg(QLatin1String(Divider));
}

void apply(QApplication &app) {
    app.setStyle(QStringLiteral("Fusion"));   // predictable base for a dark palette

    QPalette p;
    p.setColor(QPalette::Window,          QColor(Window));
    p.setColor(QPalette::WindowText,      QColor(Text));
    p.setColor(QPalette::Base,            QColor(Panel));
    p.setColor(QPalette::AlternateBase,   QColor(Raised));
    p.setColor(QPalette::Text,            QColor(Text));
    p.setColor(QPalette::PlaceholderText, QColor(TextDim));
    p.setColor(QPalette::Button,          QColor(Raised));
    p.setColor(QPalette::ButtonText,      QColor(Text));
    p.setColor(QPalette::BrightText,      QColor(Red));
    p.setColor(QPalette::Highlight,       QColor(Selection));
    p.setColor(QPalette::HighlightedText, QColor(Text));
    p.setColor(QPalette::ToolTipBase,     QColor(Raised));
    p.setColor(QPalette::ToolTipText,     QColor(Text));
    p.setColor(QPalette::Link,            QColor(Blue));
    // Disabled controls must stay legible, not vanish.
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(TextDim));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(TextDim));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(TextDim));
    app.setPalette(p);

    // Named tokens rather than %1..%11: QString::arg's multi-argument overload
    // mis-handles two-digit placeholders, which silently painted the splitter
    // handles with the error colour.
    QString css = QStringLiteral(R"(
QWidget            { background: @window; color: @text; }
QSplitter::handle  { background: @divider; }
QSplitter::handle:horizontal { width: 3px; }
QSplitter::handle:vertical   { height: 3px; }

QLabel#paneHeader  { color: @dim; padding: 3px 6px; background: @panel;
                     border-bottom: 1px solid @divider; }
QLabel#paneHeader[active="true"] { color: @text; background: @raised; }
QLabel#status      { color: @dim; background: @panel; padding: 4px 8px;
                     border-top: 1px solid @divider; }
QLabel#status[level="error"] { color: @red; }
QLabel#status[level="warn"]  { color: @amber; }

QTreeView, QPlainTextEdit { background: @panel; color: @text; border: none;
                            selection-background-color: @selection; selection-color: @text; }
QTreeView::item:hover     { background: @raised; }
QHeaderView::section      { background: @raised; color: @dim; border: 0; padding: 3px; }

QPushButton        { background: @raised; color: @text; border: 1px solid @divider;
                     padding: 5px 10px; }
QPushButton:hover  { border-color: @blue; }
QPushButton:pressed{ background: @selection; }
QPushButton:disabled { color: @dim; border-color: @divider; }

QPushButton#routeButton          { background: @window; color: @text; border: 1px solid @blue;
                                   padding: 6px 12px; font-weight: bold; }
QPushButton#routeButton:hover    { background: @blue; color: #ffffff; }
QPushButton#routeButton:disabled { color: @dim; border: 1px solid @divider; background: @window; }

QComboBox          { background: @raised; color: @text; border: 1px solid @divider; padding: 3px 6px; }
QComboBox:hover    { border-color: @blue; }
QComboBox QAbstractItemView { background: @panel; color: @text; selection-background-color: @selection; }

QScrollBar:vertical, QScrollBar:horizontal { background: @panel; border: none; }
QScrollBar::handle { background: @raised; min-height: 20px; min-width: 20px; }
QScrollBar::handle:hover { background: @divider; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }

QMessageBox, QDialog { background: @panel; color: @text; }
QMessageBox QLabel   { color: @text; }
QMenu                { background: @panel; color: @text; border: 1px solid @divider; }
QMenu::item:selected { background: @selection; }
QToolTip             { background: @raised; color: @text; border: 1px solid @divider; }

/* Explorer toolbars: compact controls and an elided current-path label. */
QLabel#pathLabel   { color: @dim; background: @window; padding: 2px 5px;
                     border: 1px solid @divider; }
QPushButton#routeButton:checked { background: @blue; color: #ffffff; }
)");
    const QPair<const char *, const char *> tokens[] = {
        {"@window", Window}, {"@panel", Panel}, {"@raised", Raised}, {"@blue", Blue},
        {"@text", Text}, {"@dim", TextDim}, {"@selection", Selection},
        {"@amber", Amber}, {"@red", Red}, {"@divider", Divider}};
    for (const auto &t : tokens) css.replace(QLatin1String(t.first), QLatin1String(t.second));
    app.setStyleSheet(css);
}

} // namespace Theme
