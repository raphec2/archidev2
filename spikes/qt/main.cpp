// SPDX-License-Identifier: GPL-2.0-or-later
//
// ArchiDev2 - Qt/QTermWidget spike.
// Two live AI terminals, a stock file tree, and a single-document editor.
// Selected text routes between all three surfaces and is never submitted.
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <qtermwidget.h>

enum Surface { SConsultant, SDeveloper, SEditor, SNone };
static const char *kName[] = {"CONSULTANT", "DEVELOPER", "EDITOR"};
static const qint64 kMaxOpenBytes = 8 * 1024 * 1024;   // refuse to open anything larger

// Window that can veto its own close, so unsaved work is never lost silently.
class MainWindow : public QWidget {
public:
    std::function<bool()> allowClose;
protected:
    void closeEvent(QCloseEvent *e) override {
        if (allowClose && !allowClose()) { e->ignore(); return; }
        e->accept();
    }
};

static bool resolveDir(const QString &raw, QString *out, QString *err) {
    const QFileInfo fi(QDir::current().absoluteFilePath(raw));
    if (!fi.exists())     { *err = QStringLiteral("no such directory: %1").arg(raw); return false; }
    if (!fi.isDir())      { *err = QStringLiteral("not a directory: %1").arg(raw);   return false; }
    if (!fi.isReadable()) { *err = QStringLiteral("not readable: %1").arg(raw);      return false; }
    *out = fi.canonicalFilePath();
    return true;
}

static QTermWidget *makeTerm(const QString &program, const QString &cwd) {
    auto *t = new QTermWidget(0);        // 0 = do not auto-start a shell
    t->setShellProgram(program);         // resolved via PATH by the widget itself
    t->setArgs({});
    t->setWorkingDirectory(cwd);
    t->setTerminalSizeHint(false);
    t->startShellProgram();
    return t;
}

static QWidget *labelled(const QString &title, const QString &sub, QWidget *inner, QLabel **hdrOut) {
    auto *box = new QWidget;
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto *hdr = new QLabel(QStringLiteral("%1   %2").arg(title, sub));
    hdr->setContentsMargins(6, 2, 6, 2);
    v->addWidget(hdr);
    auto *frame = new QFrame;
    frame->setFrameShape(QFrame::StyledPanel);
    auto *fv = new QVBoxLayout(frame);
    fv->setContentsMargins(1, 1, 1, 1);
    fv->addWidget(inner);
    v->addWidget(frame, 1);
    *hdrOut = hdr;
    return box;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTextStream err(stderr);
    const QStringList a = QApplication::arguments();
    if (a.size() != 5) {
        err << "usage: archidev2-qt <project-dir> <consultant-dir> <consultant-prog> <developer-prog>\n";
        return 2;
    }
    QString projectDir, consultantDir, msg;
    if (!resolveDir(a.at(1), &projectDir, &msg))    { err << "project dir: "    << msg << "\n"; return 2; }
    if (!resolveDir(a.at(2), &consultantDir, &msg)) { err << "consultant dir: " << msg << "\n"; return 2; }
    const QString consultantProg = a.at(3), developerProg = a.at(4);
    for (const QString &p : {consultantProg, developerProg})
        if (QStandardPaths::findExecutable(p).isEmpty()) { err << "not on PATH: " << p << "\n"; return 2; }

    QTermWidget *consultant = makeTerm(consultantProg, consultantDir);
    QTermWidget *developer  = makeTerm(developerProg, projectDir);
    auto *editor = new QPlainTextEdit;
    editor->setPlaceholderText(QStringLiteral("EDITOR - double-click a file, type, or route text in"));

    // Stock filesystem model + tree. No custom explorer, no recursive crawler:
    // QFileSystemModel populates lazily on its own thread and keeps itself up
    // to date through QFileSystemWatcher.
    auto *fsModel = new QFileSystemModel;
    fsModel->setRootPath(projectDir);
    fsModel->setReadOnly(true);
    auto *tree = new QTreeView;
    tree->setModel(fsModel);
    tree->setRootIndex(fsModel->index(projectDir));
    for (int c = 1; c < fsModel->columnCount(); ++c) tree->hideColumn(c);   // name only
    tree->setHeaderHidden(true);
    tree->setAnimated(false);

    QLabel *hC = nullptr, *hD = nullptr, *hE = nullptr, *hF = nullptr;
    auto *terms = new QSplitter(Qt::Horizontal);
    terms->addWidget(labelled(QStringLiteral("CONSULTANT"), consultantProg + " @ " + consultantDir, consultant, &hC));
    terms->addWidget(labelled(QStringLiteral("DEVELOPER"),  developerProg  + " @ " + projectDir,    developer,  &hD));
    auto *rightSplit = new QSplitter(Qt::Vertical);
    rightSplit->addWidget(terms);
    rightSplit->addWidget(labelled(QStringLiteral("EDITOR"), QStringLiteral("(no file)"), editor, &hE));
    rightSplit->setStretchFactor(0, 3);
    rightSplit->setStretchFactor(1, 2);
    auto *outer = new QSplitter(Qt::Horizontal);
    outer->addWidget(labelled(QStringLiteral("FILES"), projectDir, tree, &hF));
    outer->addWidget(rightSplit);
    outer->setStretchFactor(0, 1);
    outer->setStretchFactor(1, 4);

    auto *status = new QLabel;
    status->setContentsMargins(6, 2, 6, 2);
    auto say = [status, &err](const QString &m) {
        status->setText(m);
        err << "[status] " << m << "\n";   // spike aid: live log mirrors the label
        err.flush();
    };

    auto *win = new MainWindow;
    win->setWindowTitle(QStringLiteral("ArchiDev2 Qt spike - %1").arg(projectDir));

    // --- document state --------------------------------------------------
    QString docPath;                       // empty => scratch buffer, cannot be saved
    QDateTime docMtime;                    // as of last successful load/save
    qint64 docSize = -1;                   // ditto, for on-disk change detection
    bool docEndedWithNewline = true;       // preserve the file's own convention
    auto dirty = [editor] { return editor->document()->isModified(); };
    auto refreshDoc = [&] {
        const QString name = docPath.isEmpty() ? QStringLiteral("(no file)") : docPath;
        hE->setText(QStringLiteral("EDITOR   %1%2").arg(name, dirty() ? QStringLiteral(" *") : QString()));
        win->setWindowTitle(QStringLiteral("ArchiDev2 Qt spike - %1%2")
                                .arg(name, dirty() ? QStringLiteral(" *") : QString()));
    };
    QObject::connect(editor->document(), &QTextDocument::modificationChanged, [&](bool) { refreshDoc(); });

    // Save: atomic via QSaveFile (temp file + rename, original untouched on
    // failure). Refuses when there is nothing to save to, and refuses to
    // clobber a file that changed underneath us without confirmation.
    std::function<bool()> saveDoc = [&]() -> bool {
        if (docPath.isEmpty()) { say(QStringLiteral("No file open - nothing to save (this spike has no Save As)")); return false; }
        if (!dirty())          { say(QStringLiteral("No changes to save")); return true; }
        const QFileInfo now(docPath);
        if (now.exists() && (now.lastModified() != docMtime || now.size() != docSize)) {
            if (QMessageBox::warning(win, QStringLiteral("Changed on disk"),
                    QStringLiteral("%1 changed on disk since it was opened.\n\nOverwrite it?").arg(docPath),
                    QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save) {
                say(QStringLiteral("Save cancelled - %1 changed on disk").arg(docPath));
                return false;
            }
        }
        // QPlainTextEdit has no concept of a trailing newline, so editing the
        // last line would otherwise strip the file's final "\n" and produce a
        // spurious diff. Restore whatever convention the file arrived with.
        QString out = editor->toPlainText();
        if (docEndedWithNewline && !out.isEmpty() && !out.endsWith(QLatin1Char('\n')))
            out.append(QLatin1Char('\n'));
        QSaveFile f(docPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)
            || f.write(out.toUtf8()) < 0 || !f.commit()) {
            say(QStringLiteral("SAVE FAILED: %1 - %2 (file left unchanged, still unsaved)")
                    .arg(docPath, f.errorString()));
            return false;                                   // stays dirty on purpose
        }
        const QFileInfo after(docPath);
        docMtime = after.lastModified();
        docSize  = after.size();
        editor->document()->setModified(false);
        refreshDoc();
        say(QStringLiteral("Saved %1 (%2 bytes)").arg(docPath).arg(docSize));
        return true;
    };

    // Returns false if the caller should abort (user cancelled, or save failed).
    auto confirmDiscard = [&](const QString &what) -> bool {
        if (!dirty()) return true;
        const auto r = QMessageBox::question(win, QStringLiteral("Unsaved changes"),
            QStringLiteral("%1 has unsaved changes.\n\n%2")
                .arg(docPath.isEmpty() ? QStringLiteral("The editor buffer") : docPath, what),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r == QMessageBox::Cancel)  return false;
        if (r == QMessageBox::Discard) return true;
        return saveDoc();
    };

    auto openFile = [&](const QString &path) {
        const QFileInfo fi(path);
        if (fi.isDir()) return;                              // the tree expands these itself
        if (!fi.isReadable())            { say(QStringLiteral("Cannot read %1").arg(path)); return; }
        if (fi.size() > kMaxOpenBytes)   { say(QStringLiteral("Refusing to open %1 (%2 bytes, limit %3)")
                                                  .arg(path).arg(fi.size()).arg(kMaxOpenBytes)); return; }
        if (!confirmDiscard(QStringLiteral("Save before opening %1?").arg(fi.fileName()))) {
            say(QStringLiteral("Open cancelled")); return;
        }
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { say(QStringLiteral("Cannot open %1 - %2").arg(path, f.errorString())); return; }
        const QByteArray raw = f.readAll();
        const QString text = QString::fromUtf8(raw);
        if (raw.contains('\0') || text.toUtf8() != raw) {    // binary or not valid UTF-8
            say(QStringLiteral("Not a UTF-8 text file: %1 (left current document open)").arg(path));
            return;
        }
        editor->setPlainText(text);
        editor->document()->setModified(false);
        docPath  = path;
        docMtime = fi.lastModified();
        docSize  = fi.size();
        docEndedWithNewline = raw.isEmpty() || raw.endsWith('\n');
        refreshDoc();
        say(QStringLiteral("Opened %1 (%2 bytes)").arg(path).arg(fi.size()));
    };
    QObject::connect(tree, &QTreeView::doubleClicked,
                     [&](const QModelIndex &i) { openFile(fsModel->filePath(i)); });

    // --- selection -------------------------------------------------------
    // Read live at send time, never cached, so a selection that has since
    // vanished can never be routed as stale text.
    auto liveText = [consultant, developer, editor](Surface s) -> QString {
        if (s == SConsultant) return consultant->selectedText(true);
        if (s == SDeveloper)  return developer->selectedText(true);
        if (s == SEditor) {
            QString t = editor->textCursor().selectedText();
            return t.replace(QChar(0x2029), QLatin1Char('\n'));   // QTextCursor uses U+2029
        }
        return QString();
    };
    Surface lastSel = SNone, focused = SNone;
    auto pickSource = [&](Surface dest) -> Surface {
        for (Surface s : {focused, lastSel})                      // focused wins, then most recent
            if (s != SNone && s != dest && !liveText(s).isEmpty()) return s;
        for (Surface s : {SConsultant, SDeveloper, SEditor})
            if (s != dest && !liveText(s).isEmpty()) return s;
        return SNone;
    };

    auto sendTo = [&](Surface dest) {
        const Surface src = pickSource(dest);
        if (src == SNone) { say(QStringLiteral("Nothing selected")); return; }
        QString text = liveText(src);
        if (dest == SConsultant || dest == SDeveloper) {
            while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r')))
                text.chop(1);                                     // never submit
            if (text.isEmpty()) { say(QStringLiteral("Selection is only line breaks")); return; }
            QTermWidget *t = (dest == SConsultant) ? consultant : developer;
            const int visible = text.size();      // count before bracketing adds marker bytes
            t->bracketText(text);   // wraps in \e[200~..\e[201~ only if the app enabled it
            t->sendText(text);      // exactly these bytes; no Enter is ever appended
            t->setFocus(Qt::OtherFocusReason);
            say(QStringLiteral("Sent %1 chars from %2 to %3 - focus moved, not submitted")
                    .arg(visible).arg(kName[src], kName[dest]));
            return;
        }
        editor->insertPlainText(text);            // replaces selection, else inserts at cursor
        editor->setFocus(Qt::OtherFocusReason);
        say(QStringLiteral("Sent %1 chars from %2 to EDITOR - focus moved").arg(text.size()).arg(kName[src]));
    };

    auto *bar = new QHBoxLayout;
    const struct { const char *label; Surface dest; } kBtn[] = {
        {"Send to Consultant", SConsultant}, {"Send to Developer", SDeveloper}, {"Send to Editor", SEditor}};
    for (const auto &b : kBtn) {
        auto *btn = new QPushButton(QString::fromLatin1(b.label));
        btn->setFocusPolicy(Qt::NoFocus);         // clicking must not move focus off the source
        QObject::connect(btn, &QPushButton::clicked, [&sendTo, d = b.dest] { sendTo(d); });
        bar->addWidget(btn);
    }
    bar->addSpacing(24);
    auto *bSave = new QPushButton(QStringLiteral("Save  (Ctrl+S)"));
    bSave->setFocusPolicy(Qt::NoFocus);
    bSave->setShortcut(QKeySequence::Save);
    QObject::connect(bSave, &QPushButton::clicked, [&saveDoc] { saveDoc(); });
    bar->addWidget(bSave);
    bar->addStretch(1);

    // --- focus indication + selection tracking ---------------------------
    auto markFocus = [&](Surface s) {
        focused = s;
        QLabel *hs[] = {hC, hD, hE};
        for (int i = 0; i < 3; ++i)
            hs[i]->setStyleSheet(i == s ? QStringLiteral("font-weight:bold; background:#cde8ff;") : QString());
    };
    QObject::connect(consultant, &QTermWidget::termGetFocus, [&] { markFocus(SConsultant); });
    QObject::connect(developer,  &QTermWidget::termGetFocus, [&] { markFocus(SDeveloper); });
    QObject::connect(editor, &QPlainTextEdit::cursorPositionChanged, [&] { markFocus(SEditor); });
    QObject::connect(consultant, &QTermWidget::copyAvailable, [&](bool on) {
        if (on) { lastSel = SConsultant;
                  say(QStringLiteral("CONSULTANT selection: %1 chars").arg(liveText(SConsultant).size())); }
    });
    QObject::connect(developer, &QTermWidget::copyAvailable, [&](bool on) {
        if (on) { lastSel = SDeveloper;
                  say(QStringLiteral("DEVELOPER selection: %1 chars").arg(liveText(SDeveloper).size())); }
    });
    QObject::connect(editor, &QPlainTextEdit::selectionChanged, [&] {
        if (editor->textCursor().hasSelection()) lastSel = SEditor;
    });

    win->allowClose = [&] { return confirmDiscard(QStringLiteral("Save before closing?")); };
    auto *root = new QVBoxLayout(win);
    root->addLayout(bar);
    root->addWidget(outer, 1);
    root->addWidget(status);
    win->resize(1500, 900);
    win->show();
    refreshDoc();
    say(QStringLiteral("Select text in any surface, then click a destination. Double-click a file to edit it."));

    // Spike-only, non-interactive check (ARCHIDEV2_SPIKE_SELFTEST=<file>):
    // exercises the real routing and save paths so they can be proven headlessly.
    const QString selftest = qEnvironmentVariable("ARCHIDEV2_SPIKE_SELFTEST");
    if (!selftest.isEmpty()) {
        QTimer::singleShot(2000, [&, selftest] {
            editor->setPlainText(QStringLiteral("touch /tmp/ad2-selftest-EXECUTED\nsecond line\n"));
            editor->selectAll();
            sendTo(SDeveloper);                   // must insert, never execute
            openFile(selftest);                   // stock open path
            editor->appendPlainText(QStringLiteral("appended-by-selftest"));
            saveDoc();                            // atomic save path
        });
    }
    return app.exec();
}
