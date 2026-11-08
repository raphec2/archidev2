// SPDX-License-Identifier: GPL-2.0-or-later
//
// The tabbed Notes + Editor area. Tab 0 is always Notes: pinned, never closed
// and never replaced by an opened file. Every other tab is one file on disk.
// All documents share the same safeguards: UTF-8 only, size limit on open,
// atomic save (QSaveFile), and a warning before overwriting a file that
// changed on disk since it was opened or last saved.
#pragma once

#include <QDateTime>
#include <QVector>
#include <QWidget>

class CodeEditor;
class QLabel;
class QLineEdit;
class QTabWidget;
class QToolButton;
class SyntaxHighlighter;

class EditorTabs : public QWidget {
    Q_OBJECT
public:
    static const qint64 kMaxOpenBytes = 8 * 1024 * 1024;     // refuse to open above this
    static const qint64 kMaxColourBytes = 1024 * 1024;       // no syntax colouring above this

    explicit EditorTabs(QWidget *parent = nullptr);

    CodeEditor *notesEditor() const;
    CodeEditor *activeEditor() const;
    bool activeIsNotes() const;
    QString activeSourceName() const;       // "NOTES" or "EDITOR main.cpp"
    QString activeSelection() const;        // U+2029 already turned into '\n'
    QString activePath() const;             // empty for a Notes scratch buffer
    QString headerText() const;
    int count() const;

    // Inserts at the Notes cursor (replacing a Notes selection), shows the
    // Notes tab and focuses it - whatever tab was active before.
    void insertIntoNotes(const QString &text);

    bool openFile(const QString &path);     // new tab, or the existing one
    bool loadNotesFile(const QString &path);
    bool saveActive();
    bool reloadActive();
    bool closeTab(int index);
    bool confirmCloseAll();                 // before the window closes
    void showFind();

signals:
    void message(const QString &text, const QString &level);
    void focused();                         // an editor got focus or moved its cursor
    void stateChanged();                    // active tab, file, dirty state or position
    void openNotesRequested();

private:
    struct Doc {
        CodeEditor *editor = nullptr;
        SyntaxHighlighter *highlighter = nullptr;
        QString path;                       // canonical; empty = Notes scratch
        QDateTime mtime;
        qint64 size = -1;
        bool endedWithNewline = true;
        bool notes = false;
    };

    Doc *docAt(int index);
    const Doc *docAt(int index) const;
    int indexOfPath(const QString &canonical) const;
    CodeEditor *newEditor(bool notes);
    bool readText(const QString &path, QString *text, QByteArray *raw);
    void fill(Doc &d, const QString &path, const QString &text, const QByteArray &raw);
    bool saveDoc(int index);
    bool confirmDiscard(int index, const QString &question);
    QString changedOnDisk(const Doc &d) const;   // empty when unchanged
    QString tabLabel(int index) const;
    void refreshTabs();
    void find(bool backward);
    void updateFindInfo();

    QTabWidget *m_tabs = nullptr;
    QVector<Doc> m_docs;                    // parallel to the tab order
    QLabel *m_position = nullptr;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_find = nullptr;
    QToolButton *m_case = nullptr;
    QLabel *m_findInfo = nullptr;
};
