// SPDX-License-Identifier: GPL-2.0-or-later
#include "EditorTabs.h"

#include "CodeEditor.h"
#include "Theme.h"

#include <QAction>
#include <QEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// Emits a callback when the watched editor gains focus.
class FocusWatcher : public QObject {
public:
    FocusWatcher(QObject *parent, std::function<void()> fn) : QObject(parent), m_fn(std::move(fn)) {}
protected:
    bool eventFilter(QObject *, QEvent *e) override {
        if (e->type() == QEvent::FocusIn) m_fn();
        return false;
    }
private:
    std::function<void()> m_fn;
};

QToolButton *toolButton(const QString &text, const QString &tip) {
    auto *b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setAutoRaise(false);
    return b;
}
}

EditorTabs::EditorTabs(QWidget *parent) : QWidget(parent) {
    // --- toolbar: document actions on the left, cursor position on the right
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(4);
    auto *openNotes = toolButton(QStringLiteral("Open Notes"),
        QStringLiteral("Load the notes_file configured in .archidev2.ini into the pinned Notes tab."));
    auto *save = toolButton(QStringLiteral("Save"), QStringLiteral("Save the active tab (Ctrl+S)."));
    auto *reload = toolButton(QStringLiteral("Reload"),
        QStringLiteral("Re-read the active tab's file from disk. Asks first if it has unsaved changes."));
    auto *findBtn = toolButton(QStringLiteral("Find"), QStringLiteral("Find in the active tab (Ctrl+F, F3, Shift+F3)."));
    for (QToolButton *b : {openNotes, save, reload, findBtn}) bar->addWidget(b);
    bar->addStretch(1);
    m_position = new QLabel;
    m_position->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(Theme::TextDim)));
    bar->addWidget(m_position);
    connect(openNotes, &QToolButton::clicked, this, [this] { emit openNotesRequested(); });
    connect(save, &QToolButton::clicked, this, [this] { saveActive(); });
    connect(reload, &QToolButton::clicked, this, [this] { reloadActive(); });
    connect(findBtn, &QToolButton::clicked, this, [this] { showFind(); });

    // --- tabs. Not movable: Notes stays first and m_docs stays parallel.
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->tabBar()->setElideMode(Qt::ElideMiddle);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setFocusPolicy(Qt::NoFocus);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });

    Doc notes;
    notes.notes = true;
    notes.editor = newEditor(true);
    m_docs << notes;
    m_tabs->addTab(notes.editor, QStringLiteral("Notes"));
    m_tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);   // pinned: no close button
    m_tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    m_tabs->tabBar()->setTabTextColor(0, QColor(QLatin1String(Theme::Amber)));

    // --- find bar, hidden until Ctrl+F
    m_findBar = new QWidget;
    auto *fb = new QHBoxLayout(m_findBar);
    fb->setContentsMargins(0, 0, 0, 0);
    fb->setSpacing(3);
    m_find = new QLineEdit;
    m_find->setPlaceholderText(QStringLiteral("Find in active tab"));
    m_find->setClearButtonEnabled(true);
    m_find->setMinimumWidth(60);
    auto *prev = toolButton(QStringLiteral("▲"), QStringLiteral("Previous match (Shift+F3, Shift+Enter)"));
    auto *next = toolButton(QStringLiteral("▼"), QStringLiteral("Next match (F3, Enter)"));
    m_case = toolButton(QStringLiteral("Aa"), QStringLiteral("Match case"));
    m_case->setCheckable(true);
    auto *close = toolButton(QStringLiteral("✕"), QStringLiteral("Close the find bar (Esc)"));
    m_findInfo = new QLabel;
    m_findInfo->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(Theme::TextDim)));
    fb->addWidget(m_find, 1);
    for (QWidget *w : QList<QWidget *>{prev, next, m_case, m_findInfo, close}) fb->addWidget(w);
    m_findBar->hide();
    connect(prev, &QToolButton::clicked, this, [this] { find(true); });
    connect(next, &QToolButton::clicked, this, [this] { find(false); });
    connect(close, &QToolButton::clicked, this, [this] {
        m_findBar->hide();
        if (CodeEditor *e = activeEditor()) e->setFocus();
    });
    connect(m_case, &QToolButton::toggled, this, [this] { updateFindInfo(); });
    connect(m_find, &QLineEdit::returnPressed, this, [this] {
        find(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
    });
    // Typing searches forward from where the current match starts.
    connect(m_find, &QLineEdit::textEdited, this, [this] {
        if (CodeEditor *e = activeEditor()) {
            QTextCursor c = e->textCursor();
            c.setPosition(c.selectionStart());
            e->setTextCursor(c);
        }
        find(false);
    });

    // Connected only now: adding the Notes tab above already emits currentChanged.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { refreshTabs(); updateFindInfo(); });

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    v->addLayout(bar);
    v->addWidget(m_tabs, 1);
    v->addWidget(m_findBar);

    // Scoped to this area, so terminals keep their own keys.
    auto shortcut = [this](const QKeySequence &seq, std::function<void()> fn) {
        auto *a = new QAction(this);
        a->setShortcut(seq);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
    };
    shortcut(QKeySequence::Find, [this] { showFind(); });
    shortcut(QKeySequence(Qt::Key_F3), [this] { find(false); });
    shortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), [this] { find(true); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+W")), [this] { closeTab(m_tabs->currentIndex()); });
    auto *esc = new QAction(m_find);
    esc->setShortcut(Qt::Key_Escape);
    esc->setShortcutContext(Qt::WidgetShortcut);
    connect(esc, &QAction::triggered, close, &QToolButton::click);
    m_find->addAction(esc);

    // Notice files changed by other programs (an AI, git) when the user comes back.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s == Qt::ApplicationActive) refreshTabs();
    });
    refreshTabs();
}

CodeEditor *EditorTabs::newEditor(bool notes) {
    auto *e = new CodeEditor;
    e->setLineNumbersVisible(!notes);
    if (notes) {
        e->setPlaceholderText(QStringLiteral("NOTES - type, or route text in from any pane. "
                                             "Files you open get their own tabs; Notes stays here."));
        e->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    } else {
        e->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        e->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
    e->setTabStopDistance(4 * e->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    connect(e->document(), &QTextDocument::modificationChanged, this, [this] { refreshTabs(); });
    connect(e, &QPlainTextEdit::cursorPositionChanged, this, [this, e] {
        if (e == activeEditor()) { refreshTabs(); emit focused(); }
    });
    e->installEventFilter(new FocusWatcher(e, [this] { emit focused(); }));
    return e;
}

EditorTabs::Doc *EditorTabs::docAt(int i) { return (i >= 0 && i < m_docs.size()) ? &m_docs[i] : nullptr; }
const EditorTabs::Doc *EditorTabs::docAt(int i) const {
    return (i >= 0 && i < m_docs.size()) ? &m_docs.at(i) : nullptr;
}
int EditorTabs::count() const { return m_docs.size(); }
CodeEditor *EditorTabs::notesEditor() const { return m_docs.first().editor; }
CodeEditor *EditorTabs::activeEditor() const {
    const Doc *d = docAt(m_tabs->currentIndex());
    return d ? d->editor : nullptr;
}
bool EditorTabs::activeIsNotes() const { return m_tabs->currentIndex() == 0; }
QString EditorTabs::activePath() const {
    const Doc *d = docAt(m_tabs->currentIndex());
    return d ? d->path : QString();
}

QString EditorTabs::activeSourceName() const {
    if (activeIsNotes()) return QStringLiteral("NOTES");
    return QStringLiteral("EDITOR %1").arg(QFileInfo(activePath()).fileName());
}

QString EditorTabs::activeSelection() const {
    const CodeEditor *e = activeEditor();
    if (!e) return QString();
    QString t = e->textCursor().selectedText();
    return t.replace(QChar(0x2029), QLatin1Char('\n'));   // QTextCursor uses U+2029
}

int EditorTabs::indexOfPath(const QString &canonical) const {
    for (int i = 0; i < m_docs.size(); ++i)
        if (!m_docs.at(i).path.isEmpty() && m_docs.at(i).path == canonical) return i;
    return -1;
}

void EditorTabs::insertIntoNotes(const QString &text) {
    m_tabs->setCurrentIndex(0);
    notesEditor()->insertPlainText(text);    // replaces a Notes selection, else inserts at cursor
    notesEditor()->setFocus(Qt::OtherFocusReason);
}

// Size, readability, NUL and strict UTF-8 round-trip checks, shared by every
// way a document is loaded.
bool EditorTabs::readText(const QString &path, QString *text, QByteArray *raw) {
    const QFileInfo fi(path);
    if (fi.isDir())       { emit message(QStringLiteral("%1 is a folder").arg(path), QStringLiteral("warn")); return false; }
    if (!fi.isReadable()) { emit message(QStringLiteral("Cannot read %1").arg(path), QStringLiteral("error")); return false; }
    if (fi.size() > kMaxOpenBytes) {
        emit message(QStringLiteral("Refusing to open %1 (%2 bytes, limit %3)")
                         .arg(path).arg(fi.size()).arg(kMaxOpenBytes), QStringLiteral("warn"));
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit message(QStringLiteral("Cannot open %1 - %2").arg(path, f.errorString()), QStringLiteral("error"));
        return false;
    }
    *raw = f.readAll();
    *text = QString::fromUtf8(*raw);
    if (raw->contains('\0') || text->toUtf8() != *raw) {
        emit message(QStringLiteral("Not a UTF-8 text file: %1 - not opened").arg(path), QStringLiteral("warn"));
        return false;
    }
    return true;
}

void EditorTabs::fill(Doc &d, const QString &path, const QString &text, const QByteArray &raw) {
    const QFileInfo fi(path);
    delete d.highlighter;
    d.highlighter = nullptr;
    d.editor->setPlainText(text);
    d.editor->document()->setModified(false);
    d.path = fi.canonicalFilePath();
    d.mtime = fi.lastModified();
    d.size = fi.size();
    d.endedWithNewline = raw.isEmpty() || raw.endsWith('\n');
    if (!d.notes && raw.size() <= kMaxColourBytes) {
        const auto lang = SyntaxHighlighter::langForFile(path, text.section(QLatin1Char('\n'), 0, 0));
        if (lang != SyntaxHighlighter::Lang::None)
            d.highlighter = new SyntaxHighlighter(d.editor->document(), lang);
    }
}

bool EditorTabs::openFile(const QString &path) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) { emit message(QStringLiteral("No such file: %1").arg(path), QStringLiteral("error")); return false; }
    if (QFileInfo(canonical).isDir()) return false;
    const int existing = indexOfPath(canonical);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        m_docs.at(existing).editor->setFocus();
        emit message(existing == 0 ? QStringLiteral("%1 is the Notes file - showing Notes").arg(canonical)
                                   : QStringLiteral("%1 is already open - switched to its tab").arg(canonical),
                     QString());
        return true;
    }
    QString text;
    QByteArray raw;
    if (!readText(canonical, &text, &raw)) return false;
    Doc d;
    d.editor = newEditor(false);
    fill(d, canonical, text, raw);
    m_docs << d;
    const int i = m_tabs->addTab(d.editor, QFileInfo(canonical).fileName());
    m_tabs->setCurrentIndex(i);
    d.editor->setFocus();
    refreshTabs();
    emit message(QStringLiteral("Opened %1 (%2 bytes, %3) in a new tab%4")
                     .arg(canonical).arg(raw.size())
                     .arg(SyntaxHighlighter::langName(d.highlighter ? d.highlighter->lang()
                                                                    : SyntaxHighlighter::Lang::None))
                     .arg(raw.size() > kMaxColourBytes ? QStringLiteral(" - too large for colouring") : QString()),
                 QString());
    return true;
}

bool EditorTabs::loadNotesFile(const QString &path) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    Doc &notes = m_docs.first();
    if (!canonical.isEmpty() && canonical == notes.path && !notes.editor->document()->isModified()) {
        m_tabs->setCurrentIndex(0);
        emit message(QStringLiteral("Notes already shows %1").arg(canonical), QString());
        return true;
    }
    const int other = indexOfPath(canonical);
    if (other > 0) {           // the same file must never be edited in two tabs
        if (m_docs.at(other).editor->document()->isModified()) {
            m_tabs->setCurrentIndex(other);
            emit message(QStringLiteral("%1 is open with unsaved changes in another tab - save or close it first")
                             .arg(canonical), QStringLiteral("warn"));
            return false;
        }
        closeTab(other);
    }
    m_tabs->setCurrentIndex(0);
    if (!confirmDiscard(0, QStringLiteral("Save Notes before loading %1?").arg(QFileInfo(path).fileName())))
        return false;
    QString text;
    QByteArray raw;
    if (!readText(path, &text, &raw)) return false;
    fill(m_docs.first(), path, text, raw);
    refreshTabs();
    emit message(QStringLiteral("Notes: loaded %1 (%2 bytes)").arg(canonical).arg(raw.size()), QString());
    return true;
}

QString EditorTabs::changedOnDisk(const Doc &d) const {
    if (d.path.isEmpty()) return QString();
    const QFileInfo now(d.path);
    if (!now.exists()) return QStringLiteral("deleted on disk");
    if (now.lastModified() != d.mtime || now.size() != d.size) return QStringLiteral("changed on disk");
    return QString();
}

bool EditorTabs::saveActive() { return saveDoc(m_tabs->currentIndex()); }

bool EditorTabs::saveDoc(int index) {
    Doc *d = docAt(index);
    if (!d) return false;
    if (d->path.isEmpty()) {
        emit message(QStringLiteral("Notes is a scratch buffer, not a file - set notes_file= under "
                                    "[workspace] in .archidev2.ini and use Open Notes to keep it"),
                     QStringLiteral("warn"));
        return false;
    }
    if (!d->editor->document()->isModified()) { emit message(QStringLiteral("No changes to save in %1").arg(d->path), QString()); return true; }
    const QFileInfo now(d->path);
    if (now.exists() && (now.lastModified() != d->mtime || now.size() != d->size)) {
        m_tabs->setCurrentIndex(index);
        if (QMessageBox::warning(this, QStringLiteral("Changed on disk"),
                QStringLiteral("%1 changed on disk since it was opened or last saved.\n\nOverwrite it with "
                               "this tab's version?").arg(d->path),
                QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save) {
            emit message(QStringLiteral("Save cancelled - %1 changed on disk").arg(d->path), QStringLiteral("warn"));
            return false;
        }
    }
    // QPlainTextEdit has no concept of a trailing newline, so editing the last
    // line would otherwise strip the file's final "\n" and produce a spurious diff.
    QString out = d->editor->toPlainText();
    if (d->endedWithNewline && !out.isEmpty() && !out.endsWith(QLatin1Char('\n')))
        out.append(QLatin1Char('\n'));
    QSaveFile f(d->path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(out.toUtf8()) < 0 || !f.commit()) {
        emit message(QStringLiteral("SAVE FAILED: %1 - %2 (file left unchanged, still unsaved)")
                         .arg(d->path, f.errorString()), QStringLiteral("error"));
        return false;                                    // stays dirty on purpose
    }
    const QFileInfo after(d->path);
    d->mtime = after.lastModified();
    d->size = after.size();
    d->editor->document()->setModified(false);
    refreshTabs();
    emit message(QStringLiteral("Saved %1 (%2 bytes)").arg(d->path).arg(d->size), QString());
    return true;
}

bool EditorTabs::reloadActive() {
    const int i = m_tabs->currentIndex();
    Doc *d = docAt(i);
    if (!d || d->path.isEmpty()) { emit message(QStringLiteral("Notes is a scratch buffer - nothing to reload"), QStringLiteral("warn")); return false; }
    if (d->editor->document()->isModified()) {
        QMessageBox box(QMessageBox::Question, QStringLiteral("Reload from disk"),
                        QStringLiteral("Discard the unsaved changes in %1 and reload it from disk?").arg(d->path),
                        QMessageBox::NoButton, this);
        QAbstractButton *reload = box.addButton(QStringLiteral("Discard and Reload"), QMessageBox::DestructiveRole);
        QPushButton *cancel = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(cancel);
        box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() != reload) { emit message(QStringLiteral("Reload cancelled"), QString()); return false; }
    }
    QString text;
    QByteArray raw;
    if (!readText(d->path, &text, &raw)) return false;
    const int line = d->editor->textCursor().blockNumber();
    fill(*d, d->path, text, raw);
    QTextCursor c(d->editor->document()->findBlockByNumber(qMin(line, d->editor->blockCount() - 1)));
    d->editor->setTextCursor(c);
    refreshTabs();
    emit message(QStringLiteral("Reloaded %1 (%2 bytes)").arg(d->path).arg(raw.size()), QString());
    return true;
}

bool EditorTabs::confirmDiscard(int index, const QString &question) {
    Doc *d = docAt(index);
    if (!d || !d->editor->document()->isModified()) return true;
    m_tabs->setCurrentIndex(index);
    const QString what = d->path.isEmpty() ? QStringLiteral("The Notes scratch buffer") : d->path;
    const QString text = d->path.isEmpty()
        ? QStringLiteral("%1 has unsaved changes. It is not a file, so they cannot be saved.\n\n"
                         "Discard them and continue?").arg(what)
        : QStringLiteral("%1 has unsaved changes.\n\n%2").arg(what, question);
    // Explicit labels: the platform's own Discard text ("Close without Saving")
    // reads wrongly when nothing is being closed.
    QMessageBox box(QMessageBox::Question, QStringLiteral("Unsaved changes"), text, QMessageBox::NoButton, this);
    QAbstractButton *save = d->path.isEmpty() ? nullptr : box.addButton(QMessageBox::Save);
    QAbstractButton *discard = box.addButton(QStringLiteral("Discard Changes"), QMessageBox::DestructiveRole);
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    const auto r = box.clickedButton() == discard ? QMessageBox::Discard
                 : (save && box.clickedButton() == save) ? QMessageBox::Save : QMessageBox::Cancel;
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Discard) return true;
    return saveDoc(index);
}

bool EditorTabs::closeTab(int index) {
    if (index <= 0 || index >= m_docs.size()) {
        if (index == 0) emit message(QStringLiteral("Notes is pinned and cannot be closed"), QString());
        return false;
    }
    if (!confirmDiscard(index, QStringLiteral("Save before closing the tab?"))) {
        emit message(QStringLiteral("Close cancelled"), QString());
        return false;
    }
    const QString path = m_docs.at(index).path;
    QWidget *w = m_tabs->widget(index);
    m_docs.remove(index);
    m_tabs->removeTab(index);
    w->deleteLater();
    refreshTabs();
    emit message(QStringLiteral("Closed %1").arg(path), QString());
    return true;
}

bool EditorTabs::confirmCloseAll() {
    for (int i = 0; i < m_docs.size(); ++i)
        if (!confirmDiscard(i, QStringLiteral("Save before closing ArchiDev2?"))) return false;
    return true;
}

QString EditorTabs::tabLabel(int index) const {
    const Doc &d = m_docs.at(index);
    QString label;
    if (d.notes) {
        label = QStringLiteral("Notes");
    } else {
        const QFileInfo fi(d.path);
        label = fi.fileName();
        // Two tabs with the same name get their folder added.
        for (int j = 1; j < m_docs.size(); ++j)
            if (j != index && QFileInfo(m_docs.at(j).path).fileName() == fi.fileName()) {
                label = QStringLiteral("%1 — %2").arg(fi.fileName(), fi.dir().dirName());
                break;
            }
    }
    if (d.editor->document()->isModified()) label += QStringLiteral(" ●");
    return label;
}

// Name and state first, full path last: a narrow pane clips the end of the
// header, and the unsaved/changed-on-disk markers must stay visible.
QString EditorTabs::headerText() const {
    const Doc *d = docAt(m_tabs->currentIndex());
    if (!d) return QStringLiteral("NOTES / EDITOR");
    QStringList parts;
    if (d->notes)
        parts << QStringLiteral("NOTES")
              << (d->path.isEmpty() ? QStringLiteral("scratch buffer, not a file") : QFileInfo(d->path).fileName());
    else
        parts << QStringLiteral("EDITOR") << QFileInfo(d->path).fileName();
    if (d->editor->document()->isModified())
        parts << (d->path.isEmpty() ? QStringLiteral("● edited") : QStringLiteral("● unsaved changes"));
    const QString disk = changedOnDisk(*d);
    if (!disk.isEmpty()) parts << QStringLiteral("⚠ %1 - Save will ask before overwriting").arg(disk);
    int dirtyOthers = 0;
    for (int i = 0; i < m_docs.size(); ++i)
        if (i != m_tabs->currentIndex() && m_docs.at(i).editor->document()->isModified()) ++dirtyOthers;
    if (dirtyOthers) parts << QStringLiteral("(%1 other tab(s) unsaved)").arg(dirtyOthers);
    if (!d->path.isEmpty()) parts << QStringLiteral("— %1").arg(d->path);
    return parts.join(QStringLiteral("   "));
}

void EditorTabs::refreshTabs() {
    for (int i = 0; i < m_docs.size(); ++i) {
        m_tabs->setTabText(i, tabLabel(i));
        const Doc &d = m_docs.at(i);
        m_tabs->setTabToolTip(i, d.notes
            ? (d.path.isEmpty() ? QStringLiteral("Notes (pinned) - scratch buffer, not saved to a file")
                                : QStringLiteral("Notes (pinned) - %1").arg(d.path))
            : d.path);
        if (!d.notes) {
            const bool stale = !changedOnDisk(d).isEmpty();
            m_tabs->tabBar()->setTabTextColor(i, stale ? QColor(QLatin1String(Theme::Red)) : QColor());
        }
    }
    if (const CodeEditor *e = activeEditor()) {
        const QTextCursor c = e->textCursor();
        const QString lang = [&] {
            const Doc *d = docAt(m_tabs->currentIndex());
            if (!d || d->notes) return QString();
            return SyntaxHighlighter::langName(d->highlighter ? d->highlighter->lang()
                                                              : SyntaxHighlighter::Lang::None) + QStringLiteral("  ");
        }();
        m_position->setText(QStringLiteral("%1Ln %2, Col %3").arg(lang)
                                .arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    }
    emit stateChanged();
}

void EditorTabs::showFind() {
    m_findBar->show();
    if (CodeEditor *e = activeEditor()) {
        const QString sel = e->textCursor().selectedText();
        if (!sel.isEmpty() && !sel.contains(QChar(0x2029))) m_find->setText(sel);
    }
    m_find->selectAll();
    m_find->setFocus();
    updateFindInfo();
}

void EditorTabs::find(bool backward) {
    CodeEditor *e = activeEditor();
    const QString needle = m_find->text();
    if (!e || needle.isEmpty()) { if (!m_findBar->isVisible()) showFind(); return; }
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    if (m_case->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    QTextCursor c = e->document()->find(needle, e->textCursor(), flags);
    bool wrapped = false;
    if (c.isNull()) {                                   // wrap around once
        QTextCursor from(e->document());
        if (backward) from.movePosition(QTextCursor::End);
        c = e->document()->find(needle, from, flags);
        wrapped = !c.isNull();
    }
    if (c.isNull()) {
        m_findInfo->setText(QStringLiteral("no match"));
        m_findInfo->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(Theme::Red)));
        return;
    }
    e->setTextCursor(c);
    e->centerCursor();
    updateFindInfo();
    if (wrapped) m_findInfo->setText(m_findInfo->text() + QStringLiteral(" (wrapped)"));
}

void EditorTabs::updateFindInfo() {
    m_findInfo->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(Theme::TextDim)));
    CodeEditor *e = activeEditor();
    const QString needle = m_find->text();
    if (!e || needle.isEmpty() || !m_findBar->isVisible()) { m_findInfo->clear(); return; }
    QTextDocument::FindFlags flags;
    if (m_case->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    const int here = e->textCursor().selectionStart();
    int total = 0, current = 0;
    for (QTextCursor c = e->document()->find(needle, 0, flags); !c.isNull() && total < 9999;
         c = e->document()->find(needle, c, flags)) {
        ++total;
        if (c.selectionStart() == here) current = total;
    }
    if (total == 0) {
        m_findInfo->setText(QStringLiteral("no match"));
        m_findInfo->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(Theme::Red)));
    } else {
        m_findInfo->setText(current ? QStringLiteral("%1 of %2").arg(current).arg(total)
                                    : QStringLiteral("%1 matches").arg(total));
    }
}
