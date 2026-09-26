// SPDX-License-Identifier: GPL-2.0-or-later
#include "MainWindow.h"

#include "ExplorerPane.h"
#include "ProjectConfig.h"
#include "Theme.h"

#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <signal.h>
#include <qtermwidget.h>

static const char *kName[]  = {"CONSULTANT", "DEVELOPER", "PROJECT SHELL", "NOTES"};
static const char *kShort[] = {"Consultant", "Developer", "Shell", "Notes"};
static const qint64 kMaxOpenBytes     = 8 * 1024 * 1024;  // editor open limit
static const qint64 kMaxSendBytes     = 256 * 1024;       // hard limit for file -> terminal
static const qint64 kConfirmSendBytes = 16 * 1024;        // ask above this

// QTermWidget forwards ordinary left drags to a child program when it enables
// mouse reporting. Its own selection path is selected by Shift. Apply that
// modifier only to left-button events on the terminal display; wheel events
// still reach the child program so Codex can scroll its conversation.
class TerminalSelectionFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *target, QEvent *event) override {
        if (m_forwarding) return false;
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
            break;
        default:
            return false;
        }
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton && !(mouse->buttons() & Qt::LeftButton))
            return false;
        QMouseEvent selectionEvent(mouse->type(), mouse->localPos(), mouse->windowPos(),
                                   mouse->screenPos(), mouse->button(), mouse->buttons(),
                                   mouse->modifiers() | Qt::ShiftModifier, mouse->source());
        QScopedValueRollback<bool> forwarding(m_forwarding, true);
        QCoreApplication::sendEvent(target, &selectionEvent);
        return true;
    }

private:
    bool m_forwarding = false;
};

// Retire a terminal we own: hang up its own child PID, then delete the widget.
// Only ever this one PID - no process-name matching, no pkill, no shell.
static void retireTerm(QTermWidget *t) {
    if (!t) return;
    const int pid = t->getShellPID();
    if (pid > 0) ::kill(static_cast<pid_t>(pid), SIGHUP);
    t->setParent(nullptr);
    t->deleteLater();
}

MainWindow::MainWindow(Launch launch, ProjectConfig &cfg, QWidget *parent)
    : QWidget(parent), m_l(std::move(launch)), m_cfg(cfg) {
    setWindowTitle(QStringLiteral("ArchiDev2 - %1").arg(m_l.projectDir));
    m_consultant.cwd = m_l.consultantDir; m_consultant.title = QStringLiteral("CONSULTANT");
    m_developer.cwd  = m_l.projectDir;    m_developer.title  = QStringLiteral("DEVELOPER");
    m_shell.cwd      = m_l.projectDir;    m_shell.title      = QStringLiteral("PROJECT SHELL");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 0);
    root->setSpacing(5);

    auto *projLabel = new QLabel(QStringLiteral("Project: %1").arg(m_l.projectDir));
    projLabel->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(Theme::TextDim)));
    root->addWidget(projLabel);

    m_topSplit = new QSplitter(Qt::Horizontal);
    m_topSplit->addWidget(buildConsultantArea());
    m_topSplit->addWidget(buildDeveloperArea());

    auto *vsplit = new QSplitter(Qt::Vertical);
    vsplit->addWidget(m_topSplit);
    vsplit->addWidget(buildBottomRow());
    vsplit->setStretchFactor(0, 3);
    vsplit->setStretchFactor(1, 3);
    root->addWidget(vsplit, 1);

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("status"));
    root->addWidget(m_status);

    wireEditor();
    restoreLayout();
    resize(1700, 1000);

    launchTerm(m_consultant, SConsultant, profileByName(m_l.consultantProfile));
    launchTerm(m_developer,  SDeveloper,  profileByName(m_l.developerProfile));
    launchTerm(m_shell,      SShell,      shellProfile());

    refreshDoc();

    // Spike-only: prove there is no global selection fallback. NOTES holds a
    // selection; the terminals hold none. Every terminal's own buttons must
    // refuse rather than borrow the NOTES text.
    if (qEnvironmentVariableIsSet("ARCHIDEV2_SELFTEST_SIMUL")) {
        QTimer::singleShot(2500, this, [this] {
            QTextStream o(stderr);
            m_editor->setPlainText(QStringLiteral("NOTESTEXT-BBB"));
            m_editor->selectAll();
            int enabled = 0, total = 0;
            for (QPushButton *b : findChildren<QPushButton *>())
                if (b->objectName() == QLatin1String("routeButton")) { ++total; if (b->isEnabled()) ++enabled; }
            o << "[simul] routing buttons enabled " << enabled << "/" << total << "\n";
            o << "[simul] notes-sel='" << liveText(SNotes) << "'"
              << " consultant-sel='" << liveText(SConsultant) << "'"
              << " developer-sel='" << liveText(SDeveloper) << "'"
              << " shell-sel='" << liveText(SShell) << "'\n";
            o.flush();
            routeFrom(SConsultant, SDeveloper);   // must refuse: consultant has nothing
            routeFrom(SDeveloper, SConsultant);   // must refuse: developer has nothing
            routeFrom(SShell, SConsultant);       // must refuse: shell has nothing
            routeFrom(SNotes, SDeveloper);        // must succeed with the NOTES text
            QTimer::singleShot(500, this, [this] { close(); });
        });
    }

    const QString st = qEnvironmentVariable("ARCHIDEV2_SELFTEST");
    if (!st.isEmpty()) {
        QTimer::singleShot(2500, this, [this, st] {
            m_editor->setPlainText(QStringLiteral("touch /tmp/ad2-iter-EXECUTED\nsecond line\n"));
            m_editor->selectAll();
            routeFrom(SNotes, SDeveloper);
            routeFrom(SNotes, SShell);
            routeFrom(SNotes, SConsultant);
            openFile(st);
            m_editor->appendPlainText(QStringLiteral("appended-by-selftest"));
            saveDoc();
            QTimer::singleShot(600, this, [this] { close(); });
        });
    }

    for (const QString &p : m_l.problems) say(QStringLiteral("Config - %1").arg(p), "warn");
    if (m_l.problems.isEmpty())
        say(QStringLiteral("Select text in an area, then use that area's own buttons. "
                           "Settings live in %1").arg(m_cfg.iniPath()));
}

// --------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------

QWidget *MainWindow::makePane(const QString &title, QWidget *inner, QLabel **hdr,
                              QVBoxLayout **slot, QFrame **frameOut) {
    auto *box = new QWidget;
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *h = new QLabel(title);
    h->setObjectName(QStringLiteral("paneHeader"));
    // The header must not set a floor on how narrow its pane can be dragged.
    h->setMinimumWidth(0);
    h->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    v->addWidget(h);
    auto *frame = new QFrame;
    frame->setMinimumWidth(0);
    frame->setStyleSheet(Theme::paneBorder(false));
    auto *fv = new QVBoxLayout(frame);
    fv->setContentsMargins(1, 1, 1, 1);
    if (inner) fv->addWidget(inner);
    v->addWidget(frame, 1);
    if (hdr) *hdr = h;
    if (slot) *slot = fv;
    if (frameOut) *frameOut = frame;
    box->setMinimumWidth(0);
    return box;
}

// One button group, owned by one source, reading only that source's selection.
//
// The buttons stay enabled at all times. Gating them on selection state meant
// depending on QTermWidget::copyAvailable, which does not reliably announce
// mouse selection changes here, so the terminal buttons sat permanently
// disabled even with a valid selection. routeFrom() reads the live selection
// and names the source when there is none, which is both dependable and more
// informative than a disabled button.
QHBoxLayout *MainWindow::makeRouteBar(Surface source, const QVector<Surface> &destinations,
                                      int stretchAfter) {
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(4);
    for (int i = 0; i < destinations.size(); ++i) {
        if (i == stretchAfter) bar->addStretch(1);   // pushes the rest to the far edge
        const Surface dest = destinations.at(i);
        QString label = QStringLiteral("→ %1").arg(QLatin1String(kShort[dest]));
        if (source == SConsultant && dest == SDeveloper)
            label = QStringLiteral("Developer →");
        else if (source == SDeveloper && dest == SConsultant)
            label = QStringLiteral("← Consultant");
        auto *b = new QPushButton(label);
        b->setObjectName(QStringLiteral("routeButton"));
        b->setFocusPolicy(Qt::NoFocus);   // must not steal the source's selection
        b->setToolTip(QStringLiteral("Send the text selected in %1 to %2.\n"
                                     "Inserted without being submitted; focus moves to %2.")
                          .arg(QLatin1String(kName[source]), QLatin1String(kName[dest])));
        connect(b, &QPushButton::clicked, this, [this, source, dest] { routeFrom(source, dest); });
        bar->addWidget(b);
    }
    // Without an interior gap the group stays left-aligned, as before.
    if (stretchAfter < 0) bar->addStretch(1);
    return bar;
}

QComboBox *MainWindow::addRoleControls(QHBoxLayout *bar, Term &t, Surface which, const QString &label) {
    t.combo = new QComboBox;
    t.combo->setFocusPolicy(Qt::NoFocus);
    for (const AiProfile &p : m_l.profiles)
        t.combo->addItem(p.available() ? p.name
                                       : QStringLiteral("%1  (not installed)").arg(p.name), p.name);
    t.pending = new QLabel;
    t.pending->setStyleSheet(QStringLiteral("color:%1; font-weight:bold;").arg(QLatin1String(Theme::Amber)));
    auto *btn = new QPushButton(QStringLiteral("Restart"));
    btn->setFocusPolicy(Qt::NoFocus);
    connect(btn, &QPushButton::clicked, this, [this, &t, which] { restartAi(t, which); });
    connect(t.combo, &QComboBox::currentTextChanged, this, [this, &t] { refreshPending(t); });
    bar->addWidget(new QLabel(QStringLiteral("%1:").arg(label)));
    bar->addWidget(t.combo);
    bar->addWidget(btn);
    bar->addWidget(t.pending);
    return t.combo;
}

QWidget *MainWindow::buildConsultantArea() {
    // The AI controls and the Consultant routing buttons belong to the terminal,
    // not to the whole area: they live inside the terminal side of the split so
    // they never visually span Consultant Files.
    m_consultantFiles = new ExplorerPane(m_l.consultantDir,
        m_cfg.flag(QStringLiteral("explorers/consultant_allow_above_start"), true));
    auto *sendBtn = new QPushButton(QStringLiteral("→ Consultant"));
    auto *openBtn = new QPushButton(QStringLiteral("Open"));
    sendBtn->setToolTip(QStringLiteral("Send File → Consultant: insert the selected text file "
                                       "into the CONSULTANT prompt.\n"
                                       "Never submitted; focus moves to CONSULTANT."));
    openBtn->setToolTip(QStringLiteral("Open the selected file in NOTES / EDITOR."));
    connect(sendBtn, &QPushButton::clicked, this, [this] { sendFileToConsultant(); });
    connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString p = m_consultantFiles->selectedPath();
        if (p.isEmpty()) { say(QStringLiteral("No file selected in Consultant Files"), "warn"); return; }
        openFile(p);
    });
    m_consultantFiles->addToolButton(sendBtn);
    m_consultantFiles->addToolButton(openBtn);
    connect(m_consultantFiles, &ExplorerPane::fileActivated, this, [this](const QString &p) { openFile(p); });
    connect(m_consultantFiles, &ExplorerPane::message, this,
            [this](const QString &t, const QString &l) { say(t, l.toLatin1().constData()); });
    m_consultantFilesHost = makePane(QStringLiteral("CONSULTANT FILES"), m_consultantFiles,
                                     nullptr, nullptr, nullptr);
    m_consultantFilesHost->setMinimumWidth(0);

    // Terminal side: its own controls, its own routing bar, then the terminal.
    auto *termSide = new QWidget;
    auto *tv = new QVBoxLayout(termSide);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(3);
    auto *roleBar = new QHBoxLayout;
    addRoleControls(roleBar, m_consultant, SConsultant, QStringLiteral("Consultant AI"));
    // Stays on the terminal side, so it is reachable while the explorer is hidden.
    m_consultantCollapse = new QPushButton(QStringLiteral("Hide Files"));
    m_consultantCollapse->setFocusPolicy(Qt::NoFocus);
    roleBar->addWidget(m_consultantCollapse);
    roleBar->addStretch(1);
    tv->addLayout(roleBar);
    // Developer sits at the right edge, nearest the Developer pane.
    tv->addLayout(makeRouteBar(SConsultant, {SNotes, SShell, SDeveloper}, 2));
    tv->addWidget(makePane(m_consultant.title, nullptr, &m_consultant.header,
                           &m_consultant.slot, &m_consultant.frame), 1);

    m_consultantSplit = new QSplitter(Qt::Horizontal);
    m_consultantSplit->addWidget(m_consultantFilesHost);
    m_consultantSplit->addWidget(termSide);
    m_consultantSplit->setStretchFactor(0, 1);
    m_consultantSplit->setStretchFactor(1, 3);
    m_consultantSplit->setChildrenCollapsible(true);

    connect(m_consultantCollapse, &QPushButton::clicked, this, [this] {
        const bool show = !m_consultantFilesHost->isVisible();
        m_consultantFilesHost->setVisible(show);
        m_consultantCollapse->setText(show ? QStringLiteral("Hide Files") : QStringLiteral("Show Files"));
        m_cfg.set(QStringLiteral("explorers/consultant_visible"),
                  show ? QStringLiteral("true") : QStringLiteral("false"));
        say(show ? QStringLiteral("Consultant Files shown") : QStringLiteral("Consultant Files hidden"));
    });

    auto *box = new QWidget;
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(m_consultantSplit, 1);
    return box;
}

QWidget *MainWindow::buildDeveloperArea() {
    auto *box = new QWidget;
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    auto *roleBar = new QHBoxLayout;
    addRoleControls(roleBar, m_developer, SDeveloper, QStringLiteral("Developer AI"));
    roleBar->addStretch(1);
    v->addLayout(roleBar);
    // Consultant sits at the left edge, nearest the Consultant pane.
    v->addLayout(makeRouteBar(SDeveloper, {SConsultant, SNotes, SShell}, 1));
    v->addWidget(makePane(m_developer.title, nullptr, &m_developer.header,
                          &m_developer.slot, &m_developer.frame), 1);
    return box;
}

QWidget *MainWindow::buildBottomRow() {
    // 1. Project Files
    m_projectFiles = new ExplorerPane(m_l.projectDir,
        m_cfg.flag(QStringLiteral("explorers/project_allow_above_start"), false));
    auto *openBtn = new QPushButton(QStringLiteral("Open"));
    openBtn->setToolTip(QStringLiteral("Open the selected file in NOTES / EDITOR."));
    connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString p = m_projectFiles->selectedPath();
        if (p.isEmpty()) { say(QStringLiteral("No file selected in Project Files"), "warn"); return; }
        openFile(p);
    });
    m_projectFiles->addToolButton(openBtn);
    connect(m_projectFiles, &ExplorerPane::fileActivated, this, [this](const QString &p) { openFile(p); });
    connect(m_projectFiles, &ExplorerPane::message, this,
            [this](const QString &t, const QString &l) { say(t, l.toLatin1().constData()); });
    auto *projectPane = makePane(QStringLiteral("PROJECT FILES"), m_projectFiles, nullptr, nullptr, nullptr);
    projectPane->setMinimumWidth(0);

    // 2. Notes / Editor
    auto *notesBox = new QWidget;
    auto *nv = new QVBoxLayout(notesBox);
    nv->setContentsMargins(0, 0, 0, 0);
    nv->setSpacing(3);
    auto *notesBar = new QHBoxLayout;
    notesBar->setContentsMargins(0, 0, 0, 0);
    notesBar->setSpacing(4);
    auto *openNotesBtn = new QPushButton(QStringLiteral("Open Notes"));
    auto *saveBtn = new QPushButton(QStringLiteral("Save  (Ctrl+S)"));
    for (auto *b : {openNotesBtn, saveBtn}) { b->setFocusPolicy(Qt::NoFocus); notesBar->addWidget(b); }
    connect(openNotesBtn, &QPushButton::clicked, this, [this] { openNotes(); });
    connect(saveBtn, &QPushButton::clicked, this, [this] { saveDoc(); });
    notesBar->addStretch(1);
    nv->addLayout(notesBar);
    nv->addLayout(makeRouteBar(SNotes, {SConsultant, SDeveloper, SShell}));
    m_editor = new QPlainTextEdit;
    m_editor->setPlaceholderText(QStringLiteral("NOTES / EDITOR - open a file, type, or route text in"));
    nv->addWidget(makePane(QStringLiteral("NOTES / EDITOR"), m_editor, &m_editorHeader,
                           nullptr, &m_editorFrame), 1);

    // 3. Project Shell
    auto *shellBox = new QWidget;
    auto *sv = new QVBoxLayout(shellBox);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(3);
    auto *shellBar = new QHBoxLayout;
    shellBar->setContentsMargins(0, 0, 0, 0);
    auto *restartShellBtn = new QPushButton(QStringLiteral("Restart Shell"));
    restartShellBtn->setFocusPolicy(Qt::NoFocus);
    connect(restartShellBtn, &QPushButton::clicked, this, [this] { restartShell(); });
    shellBar->addWidget(restartShellBtn);
    shellBar->addStretch(1);
    sv->addLayout(shellBar);
    sv->addLayout(makeRouteBar(SShell, {SConsultant, SDeveloper, SNotes}));
    sv->addWidget(makePane(m_shell.title, nullptr, &m_shell.header, &m_shell.slot, &m_shell.frame), 1);

    m_bottomSplit = new QSplitter(Qt::Horizontal);
    m_bottomSplit->setChildrenCollapsible(true);
    m_bottomSplit->addWidget(projectPane);
    m_bottomSplit->addWidget(notesBox);
    m_bottomSplit->addWidget(shellBox);
    return m_bottomSplit;
}

// --------------------------------------------------------------------------
// Terminals
// --------------------------------------------------------------------------

AiProfile MainWindow::profileByName(const QString &n) const {
    for (const AiProfile &p : m_l.profiles) if (p.name == n) return p;
    for (const AiProfile &p : m_l.profiles) if (p.available()) return p;
    return AiProfile{};
}

AiProfile MainWindow::shellProfile() const {
    QString prog = m_l.shellProgram;
    if (prog.isEmpty()) prog = qEnvironmentVariable("SHELL");
    if (prog.isEmpty()) prog = QStringLiteral("/bin/bash");
    return AiProfile{QStringLiteral("Shell"), prog, m_l.shellArguments};
}

MainWindow::Term *MainWindow::termFor(Surface s) {
    switch (s) {
    case SConsultant: return &m_consultant;
    case SDeveloper:  return &m_developer;
    case SShell:      return &m_shell;
    default:          return nullptr;
    }
}

void MainWindow::refreshPending(Term &t) {
    if (!t.pending) return;
    const bool differs = t.combo && t.combo->currentData().toString() != t.runningProfile;
    t.pending->setText((differs || !t.term) ? QStringLiteral("Restart to apply") : QString());
}

bool MainWindow::launchTerm(Term &t, Surface which, const AiProfile &p) {
    if (p.name.isEmpty()) { say(QStringLiteral("%1: no profile available").arg(kName[which]), "error"); return false; }
    if (!p.available()) {          // validate BEFORE disturbing a working session
        say(QStringLiteral("%1: executable not found for \"%2\" (%3) - session unchanged")
                .arg(kName[which], p.name, p.program), "error");
        return false;
    }
    retireTerm(t.term);
    t.term = new QTermWidget(0);                    // 0 = do not auto-start a shell
    if (which != SShell) {
        for (QWidget *child : t.term->findChildren<QWidget *>()) {
            if (qstrcmp(child->metaObject()->className(), "Konsole::TerminalDisplay") == 0) {
                child->installEventFilter(new TerminalSelectionFilter(child));
                break;
            }
        }
    }
    t.term->setShellProgram(p.resolvedProgram());   // absolute path; no shell involved
    t.term->setArgs(p.arguments);                   // preserved exactly, never re-parsed
    t.term->setWorkingDirectory(t.cwd);
    t.term->setTerminalSizeHint(false);
    t.term->setColorScheme(QLatin1String(Theme::TerminalScheme));
    // Deterministic paste behaviour, identical for all three terminals.
    // Trailing newlines are dropped so an external paste can never submit the
    // prompt; interior newlines are untouched (the trim is a /\r+$/ replace).
    t.term->setTrimPastedTrailingNewlines(true);
    t.term->setConfirmMultilinePaste(false);
    t.term->startShellProgram();
    t.slot->addWidget(t.term);
    t.runningProfile = p.name;
    t.header->setText(QStringLiteral("%1   %2 @ %3").arg(t.title, p.name, t.cwd));
    connect(t.term, &QTermWidget::termGetFocus, this, [this, which] { markFocus(which); });
    installClipboardShortcuts(t, which);
    refreshPending(t);
    return true;
}

// System-clipboard copy/paste for a terminal.
//
// QTermWidget 1.4.0 ships no keyboard shortcuts at all - grepping the library
// for QShortcut/setShortcut returns nothing, and its keytabs do not bind
// Ctrl+Shift+C/V either. Those keystrokes therefore fell straight through to
// the AI child in every terminal, which is why one child reported an
// image-paste error while another happened to handle the key itself. The
// widget does expose copyClipboard()/pasteClipboard(), so the application owns
// the bindings.
//
// Qt::WidgetWithChildrenShortcut scopes each action to its own terminal: the
// shortcut only fires for the focused terminal, three identical bindings in one
// window cannot become ambiguous, and Notes keeps QPlainTextEdit's own Ctrl+C /
// Ctrl+V untouched.
void MainWindow::installClipboardShortcuts(Term &t, Surface which) {
    auto add = [&](const QKeySequence &seq, std::function<void()> fn) {
        auto *a = new QAction(t.term);          // owned by the terminal, dies with it
        a->setShortcut(seq);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(a, &QAction::triggered, this, fn);
        t.term->addAction(a);
    };

    add(QKeySequence(QStringLiteral("Ctrl+Shift+C")), [this, &t, which] {
        if (!t.term) return;
        const QString sel = t.term->selectedText(true);
        if (sel.isEmpty()) {
            say(QStringLiteral("Nothing selected in %1 to copy").arg(kName[which]), "warn");
            return;
        }
        t.term->copyClipboard();                // -> QClipboard::Clipboard
        say(QStringLiteral("Copied %1 chars from %2 to the system clipboard")
                .arg(sel.size()).arg(kName[which]));
    });

    add(QKeySequence(QStringLiteral("Ctrl+Shift+V")), [this, &t, which] {
        if (!t.term) return;
        const QClipboard *cb = QGuiApplication::clipboard();
        const QString text = cb ? cb->text(QClipboard::Clipboard) : QString();
        if (text.isEmpty()) {
            // Do not hand the AI child a keystroke it will misread as something else.
            say(QStringLiteral("Clipboard has no text to paste into %1").arg(kName[which]), "warn");
            return;
        }
        t.term->pasteClipboard();   // bracketed where supported; never appends Enter
        say(QStringLiteral("Pasted %1 chars from the system clipboard into %2 - not submitted")
                .arg(text.size()).arg(kName[which]));
    });
}

void MainWindow::restartAi(Term &t, Surface which) {
    const AiProfile p = profileByName(t.combo->currentData().toString());
    if (!p.available()) {
        say(QStringLiteral("%1: cannot start \"%2\" - %3 not found. Existing session left running.")
                .arg(kName[which], p.name, p.program), "error");
        return;
    }
    if (t.term && QMessageBox::question(this, QStringLiteral("Restart %1").arg(kName[which]),
            QStringLiteral("End the current %1 session and start \"%2\" in\n%3 ?")
                .arg(kName[which], p.name, t.cwd),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        say(QStringLiteral("%1 restart cancelled - session untouched").arg(kName[which]));
        return;
    }
    if (launchTerm(t, which, p)) {
        m_cfg.set(which == SConsultant ? QStringLiteral("roles/consultant_profile")
                                       : QStringLiteral("roles/developer_profile"), p.name);
        say(QStringLiteral("%1 restarted with \"%2\" in %3").arg(kName[which], p.name, t.cwd));
    }
}

void MainWindow::restartShell() {
    if (m_shell.term && QMessageBox::question(this, QStringLiteral("Restart Project Shell"),
            QStringLiteral("End the current shell session in\n%1 ?").arg(m_shell.cwd),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        say(QStringLiteral("Shell restart cancelled - session untouched"));
        return;
    }
    if (launchTerm(m_shell, SShell, shellProfile()))
        say(QStringLiteral("Project shell restarted in %1").arg(m_shell.cwd));
}

// --------------------------------------------------------------------------
// Source-local routing
//
// There is deliberately no global "last selected surface". Each button group
// belongs to one source and reads only that source's live selection, so several
// surfaces can hold selections at once without interfering.
// --------------------------------------------------------------------------

QString MainWindow::liveText(Surface s) const {
    switch (s) {
    case SConsultant: return m_consultant.term ? m_consultant.term->selectedText(true) : QString();
    case SDeveloper:  return m_developer.term  ? m_developer.term->selectedText(true)  : QString();
    case SShell:      return m_shell.term      ? m_shell.term->selectedText(true)      : QString();
    case SNotes: {
        QString t = m_editor->textCursor().selectedText();
        return t.replace(QChar(0x2029), QLatin1Char('\n'));   // QTextCursor uses U+2029
    }
    default: return QString();
    }
}

void MainWindow::routeFrom(Surface source, Surface dest) {
    QString text = liveText(source);           // live, from this source only
    if (text.isEmpty()) {
        say(QStringLiteral("Nothing selected in %1").arg(kName[source]), "warn");
        return;
    }
    if (dest != SNotes) {
        while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r')))
            text.chop(1);                      // never submit
        if (text.isEmpty()) { say(QStringLiteral("Selection is only line breaks"), "warn"); return; }
        Term *t = termFor(dest);
        if (!t || !t->term) { say(QStringLiteral("%1 has no running session").arg(kName[dest]), "error"); return; }
        const int visible = text.size();       // count before bracketing adds marker bytes
        t->term->bracketText(text);   // wraps in \e[200~..\e[201~ only if the app enabled it
        t->term->sendText(text);      // exactly these bytes; no Enter is ever appended
        t->term->setFocus(Qt::OtherFocusReason);
        say(QStringLiteral("Sent %1 chars from %2 to %3 - focus moved, not submitted")
                .arg(visible).arg(kName[source], kName[dest]));
        return;
    }
    m_editor->insertPlainText(text);           // replaces selection, else inserts at cursor
    m_editor->setFocus(Qt::OtherFocusReason);
    say(QStringLiteral("Sent %1 chars from %2 to NOTES - focus moved").arg(text.size()).arg(kName[source]));
}

void MainWindow::markFocus(Surface s) {
    m_focused = s;
    QFrame *frames[] = {m_consultant.frame, m_developer.frame, m_shell.frame, m_editorFrame};
    QLabel *heads[]  = {m_consultant.header, m_developer.header, m_shell.header, m_editorHeader};
    for (int i = 0; i < SurfaceCount; ++i) {
        if (frames[i]) frames[i]->setStyleSheet(Theme::paneBorder(i == s));
        if (heads[i]) {
            heads[i]->setProperty("active", i == s);
            heads[i]->style()->unpolish(heads[i]);
            heads[i]->style()->polish(heads[i]);
        }
    }
}

// --------------------------------------------------------------------------
// Consultant file action
// --------------------------------------------------------------------------

void MainWindow::sendFileToConsultant() {
    const QString path = m_consultantFiles->selectedPath();
    if (path.isEmpty()) { say(QStringLiteral("No file selected in Consultant Files"), "warn"); return; }
    const QFileInfo fi(path);
    if (fi.isDir())       { say(QStringLiteral("%1 is a directory").arg(path), "warn"); return; }
    if (!fi.isReadable()) { say(QStringLiteral("Cannot read %1").arg(path), "error"); return; }
    if (fi.size() > kMaxSendBytes) {
        say(QStringLiteral("Refusing to send %1 (%2 bytes, limit %3) - open it in Notes instead")
                .arg(path).arg(fi.size()).arg(kMaxSendBytes), "warn");
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { say(QStringLiteral("Cannot open %1 - %2").arg(path, f.errorString()), "error"); return; }
    const QByteArray raw = f.readAll();
    QString text = QString::fromUtf8(raw);
    if (raw.contains('\0') || text.toUtf8() != raw) {
        say(QStringLiteral("Not a UTF-8 text file: %1").arg(path), "warn");
        return;
    }
    if (fi.size() > kConfirmSendBytes
        && QMessageBox::question(this, QStringLiteral("Send file to Consultant"),
               QStringLiteral("Insert %1 (%2 bytes) into the CONSULTANT prompt?")
                   .arg(fi.fileName()).arg(fi.size()),
               QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        say(QStringLiteral("Send cancelled"));
        return;
    }
    while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r'))) text.chop(1);
    if (text.isEmpty()) { say(QStringLiteral("%1 is empty").arg(path), "warn"); return; }
    if (!m_consultant.term) { say(QStringLiteral("CONSULTANT has no running session"), "error"); return; }
    const int visible = text.size();
    m_consultant.term->bracketText(text);
    m_consultant.term->sendText(text);          // never submitted
    m_consultant.term->setFocus(Qt::OtherFocusReason);
    say(QStringLiteral("Sent %1 (%2 chars) to CONSULTANT - focus moved, not submitted")
            .arg(fi.fileName()).arg(visible));
}

// --------------------------------------------------------------------------
// Editor and documents
// --------------------------------------------------------------------------

void MainWindow::wireEditor() {
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, [this](bool) { refreshDoc(); });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this] { markFocus(SNotes); });
    auto *save = new QAction(this);
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, [this] { saveDoc(); });
    addAction(save);
}

void MainWindow::refreshDoc() {
    const bool dirty = m_editor->document()->isModified();
    const QString name = m_docPath.isEmpty() ? QStringLiteral("(no file)") : m_docPath;
    m_editorHeader->setText(QStringLiteral("NOTES / EDITOR   %1%2")
                                .arg(name, dirty ? QStringLiteral(" *") : QString()));
    setWindowTitle(QStringLiteral("ArchiDev2 - %1%2").arg(name, dirty ? QStringLiteral(" *") : QString()));
}

bool MainWindow::saveDoc() {
    if (m_docPath.isEmpty()) { say(QStringLiteral("No file open - nothing to save (no Save As yet)"), "warn"); return false; }
    if (!m_editor->document()->isModified()) { say(QStringLiteral("No changes to save")); return true; }
    const QFileInfo now(m_docPath);
    if (now.exists() && (now.lastModified() != m_docMtime || now.size() != m_docSize)) {
        if (QMessageBox::warning(this, QStringLiteral("Changed on disk"),
                QStringLiteral("%1 changed on disk since it was opened.\n\nOverwrite it?").arg(m_docPath),
                QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save) {
            say(QStringLiteral("Save cancelled - %1 changed on disk").arg(m_docPath), "warn");
            return false;
        }
    }
    // QPlainTextEdit has no concept of a trailing newline, so editing the last
    // line would otherwise strip the file's final "\n" and produce a spurious diff.
    QString out = m_editor->toPlainText();
    if (m_docEndedWithNewline && !out.isEmpty() && !out.endsWith(QLatin1Char('\n')))
        out.append(QLatin1Char('\n'));
    QSaveFile f(m_docPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(out.toUtf8()) < 0 || !f.commit()) {
        say(QStringLiteral("SAVE FAILED: %1 - %2 (file left unchanged, still unsaved)")
                .arg(m_docPath, f.errorString()), "error");
        return false;                                    // stays dirty on purpose
    }
    const QFileInfo after(m_docPath);
    m_docMtime = after.lastModified();
    m_docSize = after.size();
    m_editor->document()->setModified(false);
    refreshDoc();
    say(QStringLiteral("Saved %1 (%2 bytes)").arg(m_docPath).arg(m_docSize));
    return true;
}

bool MainWindow::confirmDiscard(const QString &what) {
    if (!m_editor->document()->isModified()) return true;
    const auto r = QMessageBox::question(this, QStringLiteral("Unsaved changes"),
        QStringLiteral("%1 has unsaved changes.\n\n%2")
            .arg(m_docPath.isEmpty() ? QStringLiteral("The editor buffer") : m_docPath, what),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (r == QMessageBox::Cancel)  return false;
    if (r == QMessageBox::Discard) return true;
    return saveDoc();
}

void MainWindow::openFile(const QString &path) {
    const QFileInfo fi(path);
    if (fi.isDir()) return;
    if (!fi.isReadable())          { say(QStringLiteral("Cannot read %1").arg(path), "error"); return; }
    if (fi.size() > kMaxOpenBytes) { say(QStringLiteral("Refusing to open %1 (%2 bytes, limit %3)")
                                             .arg(path).arg(fi.size()).arg(kMaxOpenBytes), "warn"); return; }
    if (!confirmDiscard(QStringLiteral("Save before opening %1?").arg(fi.fileName()))) {
        say(QStringLiteral("Open cancelled")); return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { say(QStringLiteral("Cannot open %1 - %2").arg(path, f.errorString()), "error"); return; }
    const QByteArray raw = f.readAll();
    const QString text = QString::fromUtf8(raw);
    if (raw.contains('\0') || text.toUtf8() != raw) {
        say(QStringLiteral("Not a UTF-8 text file: %1 (left current document open)").arg(path), "warn");
        return;
    }
    m_editor->setPlainText(text);
    m_editor->document()->setModified(false);
    m_docPath = path;
    m_docMtime = fi.lastModified();
    m_docSize = fi.size();
    m_docEndedWithNewline = raw.isEmpty() || raw.endsWith('\n');
    refreshDoc();
    say(QStringLiteral("Opened %1 (%2 bytes)").arg(path).arg(fi.size()));
}

void MainWindow::openNotes() {
    const QString configured = m_cfg.str(QStringLiteral("workspace/notes_file")).trimmed();
    if (configured.isEmpty()) {
        say(QStringLiteral("No notes file configured - set notes_file= under [workspace] in %1")
                .arg(m_cfg.iniPath()), "warn");
        return;
    }
    const QString path = QFileInfo(configured).isAbsolute()
                             ? configured
                             : QDir(m_l.consultantDir).absoluteFilePath(configured);
    if (!QFileInfo::exists(path)) {
        if (QMessageBox::question(this, QStringLiteral("Create notes file"),
                QStringLiteral("%1 does not exist.\n\nCreate it?").arg(path),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes) != QMessageBox::Yes) {
            say(QStringLiteral("Notes file not created")); return;
        }
        if (!confirmDiscard(QStringLiteral("Save before opening the notes file?"))) return;
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || !f.commit()) {
            say(QStringLiteral("Cannot create %1 - %2").arg(path, f.errorString()), "error");
            return;
        }
        say(QStringLiteral("Created %1").arg(path));
    }
    openFile(path);
}

// --------------------------------------------------------------------------
// Layout persistence and shutdown
// --------------------------------------------------------------------------

void MainWindow::restoreLayout() {
    auto pct = [&](const char *key, int fallback) {
        const int v = m_cfg.num(QLatin1String(key), fallback);
        return (v > 0 && v < 100) ? v : fallback;          // ignore impossible values
    };
    m_topSplit->setSizes({pct("layout/top_consultant", 55) * 16, pct("layout/top_developer", 45) * 16});
    m_consultantSplit->setSizes({pct("layout/consultant_files", 24) * 8,
                                 pct("layout/consultant_term", 76) * 8});
    m_bottomSplit->setSizes({pct("layout/bottom_project_files", 16) * 16,
                             pct("layout/bottom_notes", 52) * 16,
                             pct("layout/bottom_shell", 32) * 16});

    const bool showConsultantFiles = m_cfg.flag(QStringLiteral("explorers/consultant_visible"), true);
    m_consultantFilesHost->setVisible(showConsultantFiles);
    m_consultantCollapse->setText(showConsultantFiles ? QStringLiteral("Hide Files")
                                                      : QStringLiteral("Show Files"));

    m_consultantFiles->setSortToken(m_cfg.str(QStringLiteral("explorers/consultant_sort"),
                                              QStringLiteral("name_asc")));
    m_projectFiles->setSortToken(m_cfg.str(QStringLiteral("explorers/project_sort"),
                                           QStringLiteral("name_asc")));
    const QString cc = m_cfg.str(QStringLiteral("explorers/consultant_current")).trimmed();
    if (!cc.isEmpty()) m_consultantFiles->setCurrentDir(cc);   // falls back to Start if unusable
    const QString pc = m_cfg.str(QStringLiteral("explorers/project_current")).trimmed();
    if (!pc.isEmpty()) m_projectFiles->setCurrentDir(pc);
}

void MainWindow::saveLayout() {
    auto store = [&](QSplitter *s, const QVector<const char *> &keys) {
        const QList<int> sizes = s->sizes();
        int total = 0;
        for (int v : sizes) total += v;
        if (total <= 0 || sizes.size() < keys.size()) return;
        for (int i = 0; i < keys.size(); ++i)
            m_cfg.set(QLatin1String(keys[i]), qRound(100.0 * sizes[i] / total));
    };
    // Written once, on close, rather than continuously while dragging.
    store(m_topSplit, {"layout/top_consultant", "layout/top_developer"});
    store(m_consultantSplit, {"layout/consultant_files", "layout/consultant_term"});
    store(m_bottomSplit, {"layout/bottom_project_files", "layout/bottom_notes", "layout/bottom_shell"});
    m_cfg.set(QStringLiteral("explorers/consultant_current"), m_consultantFiles->currentDir());
    m_cfg.set(QStringLiteral("explorers/project_current"), m_projectFiles->currentDir());
    m_cfg.set(QStringLiteral("explorers/consultant_sort"), m_consultantFiles->sortToken());
    m_cfg.set(QStringLiteral("explorers/project_sort"), m_projectFiles->sortToken());
    m_cfg.sync();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (!confirmDiscard(QStringLiteral("Save before closing?"))) { e->ignore(); return; }
    saveLayout();
    for (Term *t : {&m_consultant, &m_developer, &m_shell}) {
        retireTerm(t->term);                    // only PIDs this window started
        t->term = nullptr;
    }
    e->accept();
}

void MainWindow::say(const QString &m, const char *level) {
    m_status->setText(m);
    m_status->setProperty("level", QLatin1String(level));
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
    QTextStream(stderr) << "[status] " << m << "\n";
}
