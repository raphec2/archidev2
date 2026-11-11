// SPDX-License-Identifier: GPL-2.0-or-later
#include "MainWindow.h"

#include "CodeEditor.h"
#include "EditorTabs.h"
#include "ExplorerPane.h"
#include "ProjectConfig.h"
#include "ResumeDialog.h"
#include "SessionHistory.h"
#include "Theme.h"

#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFontMetrics>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <signal.h>
#include <qtermwidget.h>

static const char *kName[]  = {"CONSULTANT", "DEVELOPER", "PROJECT SHELL", "NOTES"};
static const char *kShort[] = {"Consultant", "Developer", "Shell", "Notes"};
static const qint64 kMaxSendBytes     = 256 * 1024;       // hard limit for file -> terminal
static const qint64 kConfirmSendBytes = 16 * 1024;        // ask above this

static const int kAiHistoryLines = 20000;           // QTermWidget default is 1000

// QTermWidget forwards ordinary left drags to a child program when it enables
// mouse reporting. Its own selection path is selected by Shift. Apply that
// modifier only to left-button events on the terminal display; wheel events
// still reach the child program so Codex can scroll its conversation.
//
// Dragging past the top or bottom edge scrolls the terminal's own history
// (TerminalDisplay::extendSelection, repeated by its AutoScrollHandler timer
// while the pointer stays outside). The alternate screen has no history at
// all, so a program drawing there keeps everything off-screen to itself and
// no terminal-side selection can reach it. onDragPastEdge fires once per drag
// so the window can say so instead of failing silently.
//
// QTermWidget's own edge scrolling is slow for long transcripts: a 100 ms
// timer moving 1 + (lines beyond the edge) per tick, and the room above an
// AI pane is only a few lines before the pointer leaves the window. While a
// drag is held outside the pane this filter adds its own faster tick that
// pushes the reported pointer further out the longer it stays there, so
// QTermWidget's extendSelection scrolls further each time. The selection
// logic itself stays QTermWidget's.
//
// The wheel scrolls the terminal's history while a drag is held, but
// QTermWidget only extends a selection on mouse movement. After each wheel
// step during a drag the filter replays the pointer position, so
// "hold the button, wheel, release" selects across any number of screens.
class TerminalSelectionFilter final : public QObject {
public:
    struct Hooks {
        std::function<void()> dragPastEdge;     // once per drag
        std::function<void()> selectionDone;    // after each left release
        std::function<void(const QPoint &)> contextMenu;
    };

    TerminalSelectionFilter(QWidget *display, Hooks hooks)
        : QObject(display), m_display(display), m_hooks(std::move(hooks)) {
        m_edgeTimer.setInterval(40);
        connect(&m_edgeTimer, &QTimer::timeout, this, [this] { edgeTick(); });
    }

protected:
    bool eventFilter(QObject *target, QEvent *event) override {
        if (m_forwarding || target != m_display) return false;
        if (event->type() == QEvent::ContextMenu) return true;
        if (event->type() == QEvent::Wheel) {
            // Let QTermWidget scroll first, then re-extend to the pointer.
            if (m_dragging) QTimer::singleShot(0, this, [this] { replayPointer(0); });
            return false;
        }
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
        if (mouse->button() == Qt::RightButton) {
            if (event->type() == QEvent::MouseButtonRelease && m_hooks.contextMenu)
                m_hooks.contextMenu(mouse->globalPos());
            return true;
        }
        if (mouse->button() != Qt::LeftButton && !(mouse->buttons() & Qt::LeftButton))
            return false;

        const bool outside = mouse->pos().y() < 0 || mouse->pos().y() >= m_display->height();
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            m_dragging = true;
            m_edgeReported = false;
            m_edgeTimer.stop();
            break;
        case QEvent::MouseMove:
            if (!m_dragging) break;     // e.g. QTermWidget's own timer after release
            if (outside && !m_edgeReported) {
                m_edgeReported = true;
                if (m_hooks.dragPastEdge) m_hooks.dragPastEdge();
            }
            if (outside && !m_edgeTimer.isActive()) { m_outsideFor.start(); m_edgeTimer.start(); }
            else if (!outside) m_edgeTimer.stop();
            break;
        default:
            break;
        }

        QMouseEvent selectionEvent(mouse->type(), mouse->localPos(), mouse->windowPos(),
                                   mouse->screenPos(), mouse->button(), mouse->buttons(),
                                   mouse->modifiers() | Qt::ShiftModifier, mouse->source());
        {
            QScopedValueRollback<bool> forwarding(m_forwarding, true);
            QCoreApplication::sendEvent(m_display, &selectionEvent);
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
            m_edgeTimer.stop();
            if (m_hooks.selectionDone) m_hooks.selectionDone();
        }
        return true;
    }

private:
    // A held-button move at the current pointer, pushed `extraPx` further out.
    void replayPointer(int extraPx) {
        if (!m_dragging || !(QGuiApplication::mouseButtons() & Qt::LeftButton)) {
            m_dragging = false;
            m_edgeTimer.stop();
            return;
        }
        QPoint local = m_display->mapFromGlobal(QCursor::pos());
        if (local.y() < 0) local.ry() -= extraPx;
        else if (local.y() >= m_display->height()) local.ry() += extraPx;
        QMouseEvent move(QEvent::MouseMove, local, m_display->mapToGlobal(local),
                         Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
        QScopedValueRollback<bool> forwarding(m_forwarding, true);
        QCoreApplication::sendEvent(m_display, &move);
    }

    void edgeTick() {
        const QPoint local = m_display->mapFromGlobal(QCursor::pos());
        if (local.y() >= 0 && local.y() < m_display->height()) { m_edgeTimer.stop(); return; }
        // +1 line per tick for every quarter second outside, capped: about
        // 25 lines/s at first, several hundred lines/s after a few seconds.
        const int boost = qMin<qint64>(24, m_outsideFor.elapsed() / 250);
        replayPointer(boost * QFontMetrics(m_display->font()).height());
    }

    QWidget *m_display;
    Hooks m_hooks;
    QTimer m_edgeTimer;
    QElapsedTimer m_outsideFor;
    bool m_forwarding = false;
    bool m_dragging = false;
    bool m_edgeReported = false;
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

    // Notes is the pinned first tab. A configured notes file that exists is
    // loaded into it straight away; otherwise it starts as a scratch buffer.
    const QString notes = notesFilePath();
    if (!notes.isEmpty() && QFileInfo::exists(notes)) m_editor->loadNotesFile(notes);
    refreshDoc();

    // Spike-only: prove there is no global selection fallback. NOTES holds a
    // selection; the terminals hold none. Every terminal's own buttons must
    // refuse rather than borrow the NOTES text.
    if (qEnvironmentVariableIsSet("ARCHIDEV2_SELFTEST_SIMUL")) {
        QTimer::singleShot(2500, this, [this] {
            QTextStream o(stderr);
            m_editor->notesEditor()->setPlainText(QStringLiteral("NOTESTEXT-BBB"));
            m_editor->notesEditor()->selectAll();
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
            m_editor->notesEditor()->setPlainText(QStringLiteral("touch /tmp/ad2-iter-EXECUTED\nsecond line\n"));
            m_editor->notesEditor()->selectAll();
            routeFrom(SNotes, SDeveloper);
            routeFrom(SNotes, SShell);
            routeFrom(SNotes, SConsultant);
            openFile(st);
            if (CodeEditor *e = m_editor->activeEditor()) e->appendPlainText(QStringLiteral("appended-by-selftest"));
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
    frame->setObjectName(QStringLiteral("paneFrame"));
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
        b->setProperty("routeSource", int(source));
        b->setProperty("routeDest", int(dest));
        b->setFocusPolicy(Qt::NoFocus);   // must not steal the source's selection
        if (source == SNotes)
            b->setToolTip(QStringLiteral("Send the text selected in the ACTIVE editor tab (Notes or a file) to %1.\n"
                                         "%2")
                              .arg(QLatin1String(kName[dest]),
                                   dest == SNotes ? QStringLiteral("Inserted at the Notes cursor; the Notes tab is shown.")
                                                  : QStringLiteral("Inserted without being submitted; focus moves there.")));
        else
            b->setToolTip(QStringLiteral("Send the text selected in %1 to %2.\n%3")
                              .arg(QLatin1String(kName[source]), QLatin1String(kName[dest]),
                                   dest == SNotes ? QStringLiteral("Always the pinned Notes tab, whatever tab is showing.")
                                                  : QStringLiteral("Inserted without being submitted; focus moves to %1.")
                                                        .arg(QLatin1String(kName[dest]))));
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
    auto *btn = new QPushButton(QStringLiteral("New Session"));
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(QStringLiteral("End the %1 session and start a NEW conversation with the "
                                   "profile selected on the left. Asks first.").arg(kName[which]));
    connect(btn, &QPushButton::clicked, this, [this, &t, which] { restartAi(t, which); });
    auto *resume = new QPushButton(QStringLiteral("Resume…"));
    resume->setFocusPolicy(Qt::NoFocus);
    resume->setToolTip(QStringLiteral("Pick a previous Codex or Claude conversation from that CLI's "
                                      "own history for %1 and continue it here.\n"
                                      "Nothing is preselected and nothing is submitted.").arg(t.cwd));
    connect(resume, &QPushButton::clicked, this, [this, &t, which] { resumeAi(t, which); });
    connect(t.combo, &QComboBox::currentTextChanged, this, [this, &t] { refreshPending(t); });
    bar->addWidget(new QLabel(QStringLiteral("%1:").arg(label)));
    bar->addWidget(t.combo);
    bar->addWidget(btn);
    bar->addWidget(resume);
    bar->addWidget(t.pending);
    return t.combo;
}

QWidget *MainWindow::buildConsultantArea() {
    // The AI controls and the Consultant routing buttons belong to the terminal,
    // not to the whole area: they live inside the terminal side of the split so
    // they never visually span Consultant Files.
    m_files = new ExplorerPane;
    m_files->addRoot(QStringLiteral("consultant"), QStringLiteral("Consultant"), QStringLiteral("Cons."),
                     m_l.consultantDir,
                     m_cfg.flag(QStringLiteral("explorers/consultant_allow_above_start"), true));
    m_files->addRoot(QStringLiteral("project"), QStringLiteral("Project"), QStringLiteral("Proj."),
                     m_l.projectDir,
                     m_cfg.flag(QStringLiteral("explorers/project_allow_above_start"), false));

    auto *openBtn = new QToolButton;
    openBtn->setText(QStringLiteral("Open"));
    openBtn->setToolTip(QStringLiteral("Open in Editor: the selected file opens in its own editor tab "
                                       "(double-click does the same). Notes is never replaced."));
    connect(openBtn, &QToolButton::clicked, this, [this] {
        const QString p = m_files->selectedPath();
        if (p.isEmpty()) { say(QStringLiteral("No file selected in Files"), "warn"); return; }
        if (QFileInfo(p).isDir()) { say(QStringLiteral("%1 is a folder - double-click expands it").arg(p), "warn"); return; }
        openFile(p);
    });

    auto *sendBtn = new QToolButton;
    sendBtn->setText(QStringLiteral("File actions ▾"));
    sendBtn->setPopupMode(QToolButton::InstantPopup);
    sendBtn->setToolTip(QStringLiteral("Insert the selected file's text into an AI prompt, or its path "
                                       "into the shell. Nothing is submitted or run."));
    auto *menu = new QMenu(sendBtn);
    QAction *toCons = menu->addAction(QStringLiteral("Insert file contents in Consultant prompt"));
    QAction *toDev = menu->addAction(QStringLiteral("Insert file contents in Developer prompt"));
    QAction *toShell = menu->addAction(QStringLiteral("Insert file path in Shell"));
    menu->addSeparator();
    QAction *copyPath = menu->addAction(QStringLiteral("Copy file path"));
    connect(toCons, &QAction::triggered, this, [this] { sendFileContents(SConsultant); });
    connect(toDev, &QAction::triggered, this, [this] { sendFileContents(SDeveloper); });
    connect(toShell, &QAction::triggered, this, [this] { sendFilePathToShell(); });
    connect(copyPath, &QAction::triggered, this, [this] {
        const QString p = m_files->selectedPath();
        if (p.isEmpty()) { say(QStringLiteral("No file selected in Files"), "warn"); return; }
        QGuiApplication::clipboard()->setText(p);
        say(QStringLiteral("Copied path %1 to the system clipboard").arg(p));
    });
    // Contents only make sense for a file; say why rather than failing later.
    connect(menu, &QMenu::aboutToShow, this, [this, toCons, toDev, toShell, copyPath] {
        const QString p = m_files->selectedPath();
        const bool file = !p.isEmpty() && QFileInfo(p).isFile();
        toCons->setEnabled(file);
        toDev->setEnabled(file);
        toShell->setEnabled(!p.isEmpty());
        copyPath->setEnabled(!p.isEmpty());
        if (p.isEmpty()) say(QStringLiteral("Select a file in Files first"), "warn");
    });
    sendBtn->setMenu(menu);
    m_files->addActionWidget(openBtn);
    m_files->addActionWidget(sendBtn);

    connect(m_files, &ExplorerPane::fileActivated, this, [this](const QString &p) { openFile(p); });
    connect(m_files, &ExplorerPane::message, this,
            [this](const QString &t, const QString &l) { say(t, l.toLatin1().constData()); });
    m_consultantFilesHost = makePane(QStringLiteral("FILES"), m_files, &m_filesHeader, nullptr, nullptr);
    m_consultantFilesHost->setMinimumWidth(0);
    connect(m_files, &ExplorerPane::rootChanged, this, [this](int r) {
        m_filesHeader->setText(QStringLiteral("FILES · %1").arg(m_files->rootLabel(r).toUpper()));
    });
    m_filesHeader->setText(QStringLiteral("FILES · %1").arg(m_files->rootLabel(m_files->activeRoot()).toUpper()));

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
        m_cfg.set(QStringLiteral("explorers/visible"),
                  show ? QStringLiteral("true") : QStringLiteral("false"));
        say(show ? QStringLiteral("Files shown") : QStringLiteral("Files hidden"));
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
    // 1. Notes + Editor tabs. The Project Files explorer that used to sit to
    // the left is gone; the Files explorer beside Consultant switches roots.
    auto *notesBox = new QWidget;
    auto *nv = new QVBoxLayout(notesBox);
    nv->setContentsMargins(0, 0, 0, 0);
    nv->setSpacing(3);
    m_editor = new EditorTabs;
    QHBoxLayout *routes = makeRouteBar(SNotes, {SConsultant, SDeveloper, SShell, SNotes});
    for (int i = 0; i < routes->count(); ++i)
        if (auto *b = qobject_cast<QPushButton *>(routes->itemAt(i)->widget()))
            if (b->property("routeDest").toInt() == SNotes) {
                m_editorToNotes = b;
                b->setText(QStringLiteral("→ Notes tab"));
            }
    nv->addLayout(routes);
    nv->addWidget(makePane(QStringLiteral("NOTES / EDITOR"), m_editor, &m_editorHeader,
                           nullptr, &m_editorFrame), 1);

    // 2. Project Shell
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
    t.pending->setText((differs || !t.term) ? QStringLiteral("Applies on New Session or Resume") : QString());
}

bool MainWindow::launchTerm(Term &t, Surface which, const AiProfile &p, const QString &resumedId) {
    if (p.name.isEmpty()) { say(QStringLiteral("%1: no profile available").arg(kName[which]), "error"); return false; }
    if (!p.available()) {          // validate BEFORE disturbing a working session
        say(QStringLiteral("%1: executable not found for \"%2\" (%3) - session unchanged")
                .arg(kName[which], p.name, p.program), "error");
        return false;
    }
    retireTerm(t.term);
    t.term = new QTermWidget(0);                    // 0 = do not auto-start a shell
    if (which != SShell) {
        t.term->setHistorySize(kAiHistoryLines);
        for (QWidget *child : t.term->findChildren<QWidget *>()) {
            if (qstrcmp(child->metaObject()->className(), "Konsole::TerminalDisplay") == 0) {
                // historyLinesCount() reports the screen being shown; the
                // alternate screen always reports 0.
                TerminalSelectionFilter::Hooks hooks;
                hooks.dragPastEdge = [this, &t, which] {
                    if (!t.term || t.term->historyLinesCount() > 0) return;
                    say(QStringLiteral("%1 has no terminal scrollback, so the selection cannot extend "
                                       "past the pane. Programs on the alternate screen keep their "
                                       "history to themselves; for Codex add --no-alt-screen to "
                                       "the profile arguments.").arg(kName[which]), "warn");
                };
                // Confirms the size of what was just selected, which is otherwise
                // invisible once it extends beyond the pane.
                hooks.selectionDone = [this, &t, which] {
                    if (!t.term) return;
                    const QString sel = t.term->selectedText(true);
                    if (sel.isEmpty()) return;
                    const int lines = sel.count(QLatin1Char('\n')) + 1;
                    say(QStringLiteral("%1: %2 line(s), %3 chars selected - Ctrl+C copies, "
                                       "the buttons above route it").arg(kName[which]).arg(lines).arg(sel.size()));
                };
                hooks.contextMenu = [this, &t, which](const QPoint &at) {
                    if (!t.term) return;
                    QMenu menu(t.term);
                    QAction *copy = menu.addAction(QStringLiteral("Copy selection"));
                    copy->setEnabled(!t.term->selectedText(true).isEmpty());
                    QAction *paste = menu.addAction(QStringLiteral("Paste"));
                    const QClipboard *cb = QGuiApplication::clipboard();
                    paste->setEnabled(cb && !cb->text(QClipboard::Clipboard).isEmpty());
                    QAction *chosen = menu.exec(at);
                    if (chosen == copy) copyTerminalSelection(t, which);
                    else if (chosen == paste) pasteTerminalClipboard(t, which);
                };
                child->installEventFilter(new TerminalSelectionFilter(child, std::move(hooks)));
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
    // The dropdown is what New Session and Resume act on, so it must start out
    // naming the profile that is actually running.
    if (t.combo) {
        const int i = t.combo->findData(p.name);
        if (i >= 0) t.combo->setCurrentIndex(i);
    }
    if (which == SShell)
        t.header->setText(QStringLiteral("%1   %2 @ %3").arg(t.title, p.name, t.cwd));
    else
        t.header->setText(QStringLiteral("%1   %2 · %3 @ %4").arg(t.title, p.name,
            resumedId.isEmpty() ? QStringLiteral("new session")
                                : QStringLiteral("resumed %1").arg(resumedId), t.cwd));
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

    add(QKeySequence(QStringLiteral("Ctrl+Shift+C")), [this, &t, which] { copyTerminalSelection(t, which); });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+V")), [this, &t, which] { pasteTerminalClipboard(t, which); });
    if (which != SShell) {
        add(QKeySequence(QStringLiteral("Ctrl+C")), [this, &t, which] {
            if (!t.term) return;
            if (!t.term->selectedText(true).isEmpty()) copyTerminalSelection(t, which);
            else t.term->sendText(QString(1, QChar(0x03))); // terminal interrupt (ETX)
        });
        add(QKeySequence(QStringLiteral("Ctrl+V")), [this, &t, which] { pasteTerminalClipboard(t, which); });
    }
}

bool MainWindow::copyTerminalSelection(Term &t, Surface which) {
    if (!t.term) return false;
    const QString sel = t.term->selectedText(true);
    if (sel.isEmpty()) {
        say(QStringLiteral("Nothing selected in %1 to copy").arg(kName[which]), "warn");
        return false;
    }
    t.term->copyClipboard();
    say(QStringLiteral("Copied %1 chars from %2 to the system clipboard")
            .arg(sel.size()).arg(kName[which]));
    return true;
}

void MainWindow::pasteTerminalClipboard(Term &t, Surface which) {
    if (!t.term) return;
    const QClipboard *cb = QGuiApplication::clipboard();
    const QString text = cb ? cb->text(QClipboard::Clipboard) : QString();
    if (text.isEmpty()) {
        say(QStringLiteral("Clipboard has no text to paste into %1").arg(kName[which]), "warn");
        return;
    }
    t.term->pasteClipboard();
    say(QStringLiteral("Pasted %1 chars from the system clipboard into %2 - not submitted")
            .arg(text.size()).arg(kName[which]));
}

void MainWindow::restartAi(Term &t, Surface which) {
    const AiProfile p = profileByName(t.combo->currentData().toString());
    if (!p.available()) {
        say(QStringLiteral("%1: cannot start \"%2\" - %3 not found. Existing session left running.")
                .arg(kName[which], p.name, p.program), "error");
        return;
    }
    if (t.term && QMessageBox::question(this, QStringLiteral("New %1 session").arg(kName[which]),
            QStringLiteral("End the current %1 session and start a NEW \"%2\" conversation in\n%3 ?\n\n"
                           "To continue an earlier conversation instead, cancel and use Resume…")
                .arg(kName[which], p.name, t.cwd),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        say(QStringLiteral("%1 new session cancelled - session untouched").arg(kName[which]));
        return;
    }
    if (launchTerm(t, which, p)) {
        m_cfg.set(which == SConsultant ? QStringLiteral("roles/consultant_profile")
                                       : QStringLiteral("roles/developer_profile"), p.name);
        say(QStringLiteral("%1 started a new \"%2\" session in %3").arg(kName[which], p.name, t.cwd));
    }
}

// Resume uses the CLI's own history and its own resume option; ArchiDev2
// records nothing. The dialog requires an explicit pick, and the chosen ID is
// passed as one argv entry, never through a shell and never as prompt text.
void MainWindow::resumeAi(Term &t, Surface which) {
    const AiProfile p = profileByName(t.combo->currentData().toString());
    if (!p.available()) {
        say(QStringLiteral("%1: cannot resume with \"%2\" - %3 not found. Existing session left running.")
                .arg(kName[which], p.name, p.program), "error");
        return;
    }
    const HistoryKind kind = historyKindFor(p);
    if (kind == HistoryKind::None) {
        say(QStringLiteral("%1: Resume works for profiles whose program is codex or claude; \"%2\" runs %3")
                .arg(kName[which], p.name, p.program), "warn");
        return;
    }
    ResumeDialog dlg(QLatin1String(kName[which]), p, kind, t.cwd, this);
    connect(&dlg, &ResumeDialog::message, this, [this](const QString &m) { say(m); });
    if (dlg.exec() != QDialog::Accepted) {
        say(QStringLiteral("%1 resume cancelled - session untouched").arg(kName[which]));
        return;
    }
    const SavedSession s = dlg.chosen();
    AiProfile r = p;
    r.arguments = resumeArguments(kind, s.id, p.arguments);
    if (r.arguments.isEmpty()) {
        say(QStringLiteral("%1: \"%2\" is not a session ID - session untouched").arg(kName[which], s.id), "error");
        return;
    }
    if (launchTerm(t, which, r, s.id)) {
        m_cfg.set(which == SConsultant ? QStringLiteral("roles/consultant_profile")
                                       : QStringLiteral("roles/developer_profile"), p.name);
        say(QStringLiteral("%1 resumed %2 conversation \"%3\" (%4) - nothing submitted")
                .arg(kName[which], historyKindName(kind),
                     s.title.isEmpty() ? s.firstPrompt : s.title, s.id));
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
    case SNotes:      return m_editor->activeSelection();   // the ACTIVE tab, Notes or a file
    default: return QString();
    }
}

QString MainWindow::sourceName(Surface s) const {
    if (s == SNotes) return m_editor->activeSourceName();   // "NOTES" or "EDITOR main.cpp"
    return QLatin1String(kName[s]);
}

void MainWindow::routeFrom(Surface source, Surface dest) {
    const QString from = sourceName(source);
    if (source == SNotes && dest == SNotes && m_editor->activeIsNotes()) {
        say(QStringLiteral("The selection is already in Notes"), "warn");
        return;
    }
    QString text = liveText(source);           // live, from this source only
    if (text.isEmpty()) {
        say(QStringLiteral("Nothing selected in %1").arg(from), "warn");
        return;
    }
    if (dest != SNotes) {
        while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r')))
            text.chop(1);                      // never submit
        if (text.isEmpty()) { say(QStringLiteral("Selection is only line breaks"), "warn"); return; }
        Term *t = termFor(dest);
        if (!t || !t->term) { say(QStringLiteral("%1 has no running session").arg(kName[dest]), "error"); return; }
        // A selection dragged across many screens is easy to make by accident.
        const int bytes = text.toUtf8().size();
        if (bytes > kConfirmSendBytes
            && QMessageBox::question(this, QStringLiteral("Send large selection"),
                   QStringLiteral("Insert %1 lines (%2 bytes) from %3 into the %4 prompt?\n"
                                  "It will not be submitted.")
                       .arg(text.count(QLatin1Char('\n')) + 1).arg(bytes).arg(from, QLatin1String(kName[dest])),
                   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
            say(QStringLiteral("Send cancelled"));
            return;
        }
        const int visible = text.size();       // count before bracketing adds marker bytes
        t->term->bracketText(text);   // wraps in \e[200~..\e[201~ only if the app enabled it
        t->term->sendText(text);      // exactly these bytes; no Enter is ever appended
        t->term->setFocus(Qt::OtherFocusReason);
        say(QStringLiteral("Sent %1 chars from %2 to %3 - focus moved, not submitted")
                .arg(visible).arg(from, QLatin1String(kName[dest])));
        return;
    }
    // Always the pinned Notes document, even while a file tab is showing.
    m_editor->insertIntoNotes(text);
    say(QStringLiteral("Sent %1 chars from %2 to NOTES - Notes tab shown, focus moved").arg(text.size()).arg(from));
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
// File actions from the Files explorer
// --------------------------------------------------------------------------

void MainWindow::sendFileContents(Surface dest) {
    const QString path = m_files->selectedPath();
    if (path.isEmpty()) { say(QStringLiteral("No file selected in Files"), "warn"); return; }
    const QFileInfo fi(path);
    if (fi.isDir())       { say(QStringLiteral("%1 is a folder - only a file's contents can be sent").arg(path), "warn"); return; }
    if (!fi.isReadable()) { say(QStringLiteral("Cannot read %1").arg(path), "error"); return; }
    if (fi.size() > kMaxSendBytes) {
        say(QStringLiteral("Refusing to insert %1 (%2 bytes, limit %3) - open it in the editor instead")
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
    Term *t = termFor(dest);
    if (!t || !t->term) { say(QStringLiteral("%1 has no running session").arg(kName[dest]), "error"); return; }
    if (fi.size() > kConfirmSendBytes
        && QMessageBox::question(this, QStringLiteral("Insert file in %1").arg(kShort[dest]),
               QStringLiteral("Insert %1 (%2 bytes) into the %3 prompt?\nIt will not be submitted.")
                   .arg(fi.fileName()).arg(fi.size()).arg(kName[dest]),
               QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        say(QStringLiteral("Insert cancelled"));
        return;
    }
    while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r'))) text.chop(1);
    if (text.isEmpty()) { say(QStringLiteral("%1 is empty").arg(path), "warn"); return; }
    const int visible = text.size();
    t->term->bracketText(text);
    t->term->sendText(text);                    // never submitted
    t->term->setFocus(Qt::OtherFocusReason);
    say(QStringLiteral("Inserted %1 (%2 chars) from FILES in %3 - focus moved, not submitted")
            .arg(fi.fileName()).arg(visible).arg(kName[dest]));
}

// The shell gets the path, quoted for a POSIX shell, never the contents.
void MainWindow::sendFilePathToShell() {
    const QString path = m_files->selectedPath();
    if (path.isEmpty()) { say(QStringLiteral("No file selected in Files"), "warn"); return; }
    if (!m_shell.term) { say(QStringLiteral("PROJECT SHELL has no running session"), "error"); return; }
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_./=:,+@%-]+$"));
    QString quoted = path;
    if (!safe.match(path).hasMatch())
        quoted = QLatin1Char('\'') + quoted.replace(QLatin1Char('\''), QLatin1String("'\\''")) + QLatin1Char('\'');
    QString text = quoted;
    m_shell.term->bracketText(text);
    m_shell.term->sendText(text);               // no Enter
    m_shell.term->setFocus(Qt::OtherFocusReason);
    say(QStringLiteral("Inserted the path %1 into PROJECT SHELL - focus moved, not run").arg(quoted));
}

// --------------------------------------------------------------------------
// Editor and documents
// --------------------------------------------------------------------------

void MainWindow::wireEditor() {
    connect(m_editor, &EditorTabs::message, this,
            [this](const QString &t, const QString &l) { say(t, l.toLatin1().constData()); });
    connect(m_editor, &EditorTabs::focused, this, [this] { markFocus(SNotes); });
    connect(m_editor, &EditorTabs::stateChanged, this, [this] { refreshDoc(); });
    connect(m_editor, &EditorTabs::openNotesRequested, this, [this] { openNotes(); });
    auto *save = new QAction(this);
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, [this] { saveDoc(); });
    addAction(save);
}

void MainWindow::refreshDoc() {
    m_editorHeader->setText(m_editor->headerText());
    const QString path = m_editor->activePath();
    const bool dirty = m_editor->activeEditor() && m_editor->activeEditor()->document()->isModified();
    const QString name = m_editor->activeIsNotes()
        ? (path.isEmpty() ? QStringLiteral("Notes") : QStringLiteral("Notes - %1").arg(path))
        : path;
    setWindowTitle(QStringLiteral("ArchiDev2 - %1%2").arg(name, dirty ? QStringLiteral(" *") : QString()));
    // From Notes itself "→ Notes" would be meaningless; from a file tab it copies into Notes.
    if (m_editorToNotes) m_editorToNotes->setVisible(!m_editor->activeIsNotes());
}

bool MainWindow::saveDoc() { return m_editor->saveActive(); }

void MainWindow::openFile(const QString &path) { m_editor->openFile(path); }

QString MainWindow::notesFilePath() const {
    const QString configured = m_cfg.str(QStringLiteral("workspace/notes_file")).trimmed();
    if (configured.isEmpty()) return QString();
    return QFileInfo(configured).isAbsolute() ? configured
                                              : QDir(m_l.consultantDir).absoluteFilePath(configured);
}

void MainWindow::openNotes() {
    const QString path = notesFilePath();
    if (path.isEmpty()) {
        say(QStringLiteral("No notes file configured - set notes_file= under [workspace] in %1")
                .arg(m_cfg.iniPath()), "warn");
        return;
    }
    if (!QFileInfo::exists(path)) {
        if (QMessageBox::question(this, QStringLiteral("Create notes file"),
                QStringLiteral("%1 does not exist.\n\nCreate it?").arg(path),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes) != QMessageBox::Yes) {
            say(QStringLiteral("Notes file not created")); return;
        }
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || !f.commit()) {
            say(QStringLiteral("Cannot create %1 - %2").arg(path, f.errorString()), "error");
            return;
        }
        say(QStringLiteral("Created %1").arg(path));
    }
    m_editor->loadNotesFile(path);
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
    // Older INIs had a Project Files column at the left of this row; its width
    // goes to the editor.
    const int editorPct = m_cfg.num(QStringLiteral("layout/bottom_editor"), 0) > 0
        ? pct("layout/bottom_editor", 68)
        : qBound(1, pct("layout/bottom_project_files", 16) + pct("layout/bottom_notes", 52), 99);
    m_bottomSplit->setSizes({editorPct * 16, pct("layout/bottom_shell", 32) * 16});

    const bool showConsultantFiles = m_cfg.flag(QStringLiteral("explorers/visible"),
        m_cfg.flag(QStringLiteral("explorers/consultant_visible"), true));
    m_consultantFilesHost->setVisible(showConsultantFiles);
    m_consultantCollapse->setText(showConsultantFiles ? QStringLiteral("Hide Files")
                                                      : QStringLiteral("Show Files"));

    // Each root keeps its own last folder and sort; an unusable folder falls
    // back to that root's start when it is shown.
    for (const QString &key : {QStringLiteral("consultant"), QStringLiteral("project")}) {
        const int r = m_files->rootIndex(key);
        m_files->setSortToken(r, m_cfg.str(QStringLiteral("explorers/%1_sort").arg(key), QStringLiteral("name_asc")));
        const QString cur = m_cfg.str(QStringLiteral("explorers/%1_current").arg(key)).trimmed();
        if (!cur.isEmpty()) m_files->setCurrentDir(r, cur);
    }
    const int active = m_files->rootIndex(m_cfg.str(QStringLiteral("explorers/root"), QStringLiteral("consultant")));
    m_files->setActiveRoot(active < 0 ? 0 : active);
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
    store(m_bottomSplit, {"layout/bottom_editor", "layout/bottom_shell"});
    for (int r = 0; r < 2; ++r) {
        const QString key = m_files->rootKey(r);
        m_cfg.set(QStringLiteral("explorers/%1_current").arg(key), m_files->currentDir(r));
        m_cfg.set(QStringLiteral("explorers/%1_sort").arg(key), m_files->sortToken(r));
    }
    m_cfg.set(QStringLiteral("explorers/root"), m_files->rootKey(m_files->activeRoot()));
    m_cfg.sync();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (!m_editor->confirmCloseAll()) { e->ignore(); say(QStringLiteral("Close cancelled")); return; }
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
