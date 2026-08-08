// SPDX-License-Identifier: GPL-2.0-or-later
//
// ArchiDev2 - Qt/QTermWidget spike.
// Two live AI terminals, a stock file tree, and a single-document editor.
// Selected text routes between all three surfaces and is never submitted.
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QSettings>
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
#include <signal.h>
#include <qtermwidget.h>

#include "profiles.h"

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

static QTermWidget *makeTerm(const AiProfile &p, const QString &cwd) {
    auto *t = new QTermWidget(0);        // 0 = do not auto-start a shell
    t->setShellProgram(p.resolvedProgram());   // absolute path; no shell is involved
    t->setArgs(p.arguments);                   // preserved exactly, never re-parsed
    t->setWorkingDirectory(cwd);
    t->setTerminalSizeHint(false);
    t->startShellProgram();
    return t;
}

// Retire a terminal we own: hang up its own child PID, then delete the widget.
// Only ever this one PID - no process-name matching, no pkill, no shell.
static void retireTerm(QTermWidget *t) {
    if (!t) return;
    const int pid = t->getShellPID();
    if (pid > 0) ::kill(static_cast<pid_t>(pid), SIGHUP);
    t->setParent(nullptr);
    t->deleteLater();
}

// Everything one AI role owns.
struct Role {
    QTermWidget *term = nullptr;
    QVBoxLayout *slot = nullptr;     // frame layout the terminal sits in
    QComboBox *combo = nullptr;
    QLabel *header = nullptr;
    QLabel *pending = nullptr;       // "Restart to apply"
    QString runningProfile;          // profile name of the live session
    QString cwd;
};

static QWidget *labelled(const QString &title, const QString &sub, QWidget *inner, QLabel **hdrOut,
                         QVBoxLayout **slotOut = nullptr) {
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
    if (inner) fv->addWidget(inner);   // terminal slots start empty and are filled by launchRole
    v->addWidget(frame, 1);
    *hdrOut = hdr;
    if (slotOut) *slotOut = fv;      // where the terminal lives, for restart
    return box;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTextStream err(stderr);
    QCoreApplication::setOrganizationName(QStringLiteral("ArchiDev2"));
    QCoreApplication::setApplicationName(QStringLiteral("ArchiDev2"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ArchiDev2 - consultant/developer AI workspace"));
    parser.addHelpOption();
    QCommandLineOption optCProg(QStringLiteral("consultant-program"),
        QStringLiteral("Development override: run this executable as Consultant."), QStringLiteral("program"));
    QCommandLineOption optDProg(QStringLiteral("developer-program"),
        QStringLiteral("Development override: run this executable as Developer."), QStringLiteral("program"));
    parser.addOption(optCProg);
    parser.addOption(optDProg);
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project directory (Developer cwd)."));
    parser.addPositionalArgument(QStringLiteral("consultant"),
        QStringLiteral("Consultant directory (optional; remembered between runs)."));
    parser.process(app);

    const QStringList pos = parser.positionalArguments();
    if (pos.isEmpty()) {
        err << "usage: archidev2-qt [--consultant-program P] [--developer-program P] <project-dir> [consultant-dir]\n";
        return 2;
    }
    QString projectDir, consultantDir, msg;
    if (!resolveDir(pos.at(0), &projectDir, &msg)) { err << "project dir: " << msg << "\n"; return 2; }

    // Consultant directory: explicit argument wins and is remembered; else the
    // remembered one; else ask once. Cancelling the first-run chooser exits
    // without starting any AI session.
    QSettings settings;
    if (pos.size() >= 2) {
        if (!resolveDir(pos.at(1), &consultantDir, &msg)) { err << "consultant dir: " << msg << "\n"; return 2; }
        settings.setValue(QStringLiteral("consultantDir"), consultantDir);
    } else {
        const QString remembered = settings.value(QStringLiteral("consultantDir")).toString();
        if (remembered.isEmpty() || !resolveDir(remembered, &consultantDir, &msg)) {
            const QString picked = QFileDialog::getExistingDirectory(nullptr,
                QStringLiteral("Choose the consultant working directory"), QDir::homePath());
            if (picked.isEmpty()) { err << "no consultant directory chosen - nothing was launched\n"; return 3; }
            if (!resolveDir(picked, &consultantDir, &msg)) { err << "consultant dir: " << msg << "\n"; return 3; }
            settings.setValue(QStringLiteral("consultantDir"), consultantDir);
        }
    }

    // Profiles: user file first, built-ins appended only for names it does not use.
    QStringList profileProblems;
    QVector<AiProfile> profiles = mergeWithBuiltins(loadAiProfiles(aiProfilesPath(), &profileProblems));
    const QString ovC = parser.value(optCProg), ovD = parser.value(optDProg);
    for (const QString &ov : {ovC, ovD}) {              // development overrides appear as profiles
        if (ov.isEmpty()) continue;
        const QString nm = QStringLiteral("Override: %1").arg(QFileInfo(ov).fileName());
        bool have = false;
        for (const auto &pr : profiles) if (pr.name == nm) have = true;
        if (!have) profiles.prepend(AiProfile{nm, ov, {}});
    }

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

    Role rc, rd;
    rc.cwd = consultantDir;
    rd.cwd = projectDir;
    QLabel *hE = nullptr, *hF = nullptr;
    auto *terms = new QSplitter(Qt::Horizontal);
    terms->addWidget(labelled(QStringLiteral("CONSULTANT"), consultantDir, nullptr, &rc.header, &rc.slot));
    terms->addWidget(labelled(QStringLiteral("DEVELOPER"),  projectDir,    nullptr, &rd.header, &rd.slot));
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
    auto liveText = [&](Surface s) -> QString {          // by reference: survives a restart
        if (s == SConsultant) return rc.term ? rc.term->selectedText(true) : QString();
        if (s == SDeveloper)  return rd.term ? rd.term->selectedText(true) : QString();
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
            QTermWidget *t = (dest == SConsultant) ? rc.term : rd.term;
            if (!t) { say(QStringLiteral("%1 has no running session").arg(kName[dest])); return; }
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
        QLabel *hs[] = {rc.header, rd.header, hE};
        for (int i = 0; i < 3; ++i)
            hs[i]->setStyleSheet(i == s ? QStringLiteral("font-weight:bold; background:#cde8ff;") : QString());
    };
    QObject::connect(editor, &QPlainTextEdit::cursorPositionChanged, [&] { markFocus(SEditor); });
    QObject::connect(editor, &QPlainTextEdit::selectionChanged, [&] {
        if (editor->textCursor().hasSelection()) lastSel = SEditor;
    });

    // --- role sessions ---------------------------------------------------
    // Restarting replaces the QTermWidget rather than trying to revive a
    // finished session: QTermWidget has no restart entry point, only
    // construct -> startShellProgram -> finished(). The widget is swapped
    // inside its own frame layout, so splitter sizes and layout position are
    // untouched and the other role is never disturbed.
    auto profileByName = [&](const QString &n) -> AiProfile {
        for (const auto &pr : profiles) if (pr.name == n) return pr;
        return AiProfile{};
    };
    auto refreshPending = [&](Role &r) {
        const bool differs = r.combo && r.combo->currentText() != r.runningProfile;
        const bool noSession = (r.term == nullptr);
        r.pending->setText(differs || noSession ? QStringLiteral("Restart to apply") : QString());
        r.pending->setStyleSheet(QStringLiteral("color:#b04000; font-weight:bold;"));
    };
    auto launchRole = [&](Role &r, Surface which, const AiProfile &p) -> bool {
        if (p.name.isEmpty())       { say(QStringLiteral("%1: no profile selected").arg(kName[which])); return false; }
        if (!p.available()) {        // validate BEFORE touching a working session
            say(QStringLiteral("%1: executable not found for profile \"%2\" (%3) - session unchanged")
                    .arg(kName[which], p.name, p.program));
            return false;
        }
        retireTerm(r.term);                       // hangs up only this role's own child
        r.term = makeTerm(p, r.cwd);
        r.slot->addWidget(r.term);
        r.runningProfile = p.name;
        r.header->setText(QStringLiteral("%1   %2 @ %3").arg(kName[which], p.name, r.cwd));
        QObject::connect(r.term, &QTermWidget::termGetFocus, [&, which] { markFocus(which); });
        QObject::connect(r.term, &QTermWidget::copyAvailable, [&, which](bool on) {
            if (on) { lastSel = which;
                      say(QStringLiteral("%1 selection: %2 chars").arg(kName[which]).arg(liveText(which).size())); }
        });
        refreshPending(r);
        return true;
    };
    auto restartRole = [&](Role &r, Surface which) {
        const AiProfile p = profileByName(r.combo->currentText());
        if (!p.available()) {
            say(QStringLiteral("%1: cannot start \"%2\" - %3 not found. Existing session left running.")
                    .arg(kName[which], p.name, p.program));
            return;
        }
        if (r.term && QMessageBox::question(win, QStringLiteral("Restart %1").arg(kName[which]),
                QStringLiteral("This ends the current %1 session and starts \"%2\" in\n%3\n\nContinue?")
                    .arg(kName[which], p.name, r.cwd),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
            say(QStringLiteral("%1 restart cancelled - session untouched").arg(kName[which]));
            return;
        }
        if (launchRole(r, which, p)) {
            settings.setValue(which == SConsultant ? QStringLiteral("consultantProfile")
                                                   : QStringLiteral("developerProfile"), p.name);
            say(QStringLiteral("%1 restarted with \"%2\" in %3").arg(kName[which], p.name, r.cwd));
        }
    };

    // Row of role controls: profile choice + explicit restart, per role.
    auto *roleBar = new QHBoxLayout;
    auto addRole = [&](Role &r, Surface which, const QString &label) {
        r.combo = new QComboBox;
        r.combo->setFocusPolicy(Qt::NoFocus);
        for (const auto &pr : profiles)
            r.combo->addItem(pr.available() ? pr.name : QStringLiteral("%1  (not installed)").arg(pr.name), pr.name);
        r.pending = new QLabel;
        auto *btn = new QPushButton(QStringLiteral("Restart %1").arg(label));
        btn->setFocusPolicy(Qt::NoFocus);
        QObject::connect(btn, &QPushButton::clicked, [&, which] { restartRole(r, which); });
        QObject::connect(r.combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                         [&] { refreshPending(r); });     // never restarts on its own
        roleBar->addWidget(new QLabel(QStringLiteral("%1 AI:").arg(label)));
        roleBar->addWidget(r.combo);
        roleBar->addWidget(btn);
        roleBar->addWidget(r.pending);
        roleBar->addSpacing(18);
    };
    addRole(rc, SConsultant, QStringLiteral("Consultant"));
    addRole(rd, SDeveloper,  QStringLiteral("Developer"));

    auto *bDir = new QPushButton(QStringLiteral("Change Consultant Directory..."));
    bDir->setFocusPolicy(Qt::NoFocus);
    QObject::connect(bDir, &QPushButton::clicked, [&] {
        const QString picked = QFileDialog::getExistingDirectory(win,
            QStringLiteral("Choose the consultant working directory"), rc.cwd);
        if (picked.isEmpty()) { say(QStringLiteral("Consultant directory unchanged")); return; }
        QString resolved, why;
        if (!resolveDir(picked, &resolved, &why)) { say(QStringLiteral("Not usable: %1").arg(why)); return; }
        rc.cwd = resolved;
        settings.setValue(QStringLiteral("consultantDir"), resolved);
        rc.header->setText(QStringLiteral("CONSULTANT   %1 @ %2").arg(rc.runningProfile, rc.cwd));
        rc.pending->setText(QStringLiteral("Restart to apply"));
        say(QStringLiteral("Consultant directory is now %1 - restart Consultant to use it").arg(resolved));
    });
    roleBar->addWidget(bDir);
    roleBar->addStretch(1);

    // Restore the remembered profile per role, else a sensible available default.
    auto initialFor = [&](const QString &key, const QString &prefer, const QString &override) -> AiProfile {
        if (!override.isEmpty())
            for (const auto &pr : profiles) if (pr.program == override) return pr;
        const QString saved = settings.value(key).toString();
        for (const auto &pr : profiles) if (pr.name == saved && pr.available()) return pr;
        for (const auto &pr : profiles) if (pr.name == prefer && pr.available()) return pr;
        for (const auto &pr : profiles) if (pr.available()) return pr;
        return AiProfile{};
    };
    const AiProfile startC = initialFor(QStringLiteral("consultantProfile"), QStringLiteral("Codex"), ovC);
    const AiProfile startD = initialFor(QStringLiteral("developerProfile"),  QStringLiteral("Claude"), ovD);
    if (!startC.name.isEmpty()) rc.combo->setCurrentIndex(rc.combo->findData(startC.name));
    if (!startD.name.isEmpty()) rd.combo->setCurrentIndex(rd.combo->findData(startD.name));

    win->allowClose = [&] { return confirmDiscard(QStringLiteral("Save before closing?")); };
    auto *root = new QVBoxLayout(win);
    root->addLayout(roleBar);
    root->addLayout(bar);
    root->addWidget(outer, 1);
    root->addWidget(status);
    win->resize(1500, 900);
    win->show();
    refreshDoc();
    launchRole(rc, SConsultant, startC);
    launchRole(rd, SDeveloper,  startD);
    for (const QString &pb : profileProblems) say(QStringLiteral("Profile problem - %1").arg(pb));
    if (profiles.isEmpty())
        say(QStringLiteral("No usable AI profiles. Create %1 - see ai-profiles.example.json")
                .arg(aiProfilesPath()));
    else if (profileProblems.isEmpty())
        say(QStringLiteral("Select text in any surface, then click a destination. Double-click a file to edit it."));

    // Spike-only, non-interactive check (ARCHIDEV2_SPIKE_SELFTEST=<file>):
    // exercises the real routing and save paths so they can be proven headlessly.
    // Spike-only: restart a role without the confirmation dialog, then prove
    // routing still reaches it (i.e. the swapped widget was re-wired).
    const QString restartWho = qEnvironmentVariable("ARCHIDEV2_SPIKE_RESTART");
    if (!restartWho.isEmpty()) {
        QTimer::singleShot(1500, [&, restartWho] {
            Role &r  = (restartWho == QLatin1String("consultant")) ? rc : rd;
            Surface w = (restartWho == QLatin1String("consultant")) ? SConsultant : SDeveloper;
            const int oldPid = r.term ? r.term->getShellPID() : -1;
            launchRole(r, w, profileByName(r.combo->currentText()));
            const int newPid = r.term ? r.term->getShellPID() : -1;
            err << "[restart] role=" << restartWho << " oldPid=" << oldPid << " newPid=" << newPid
                << " otherAlive=" << ((&r == &rc ? rd.term : rc.term) != nullptr) << "\n";
            err.flush();
            QTimer::singleShot(1500, [&, w] {          // routing must still work afterwards
                editor->setPlainText(QStringLiteral("post-restart routing probe"));
                editor->selectAll();
                sendTo(w);
            });
        });
    }

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
