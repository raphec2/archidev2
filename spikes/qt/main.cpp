// SPDX-License-Identifier: GPL-2.0-or-later
//
// ArchiDev2 - Qt/QTermWidget selection-routing spike.
// Proves: two live AI terminals, selection retrieval from every surface, and
// insertion into a terminal that is never submitted. Nothing else.
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <qtermwidget.h>

enum Surface { SConsultant, SDeveloper, SEditor, SNone };
static const char *kName[] = {"CONSULTANT", "DEVELOPER", "EDITOR"};

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
    editor->setPlaceholderText(QStringLiteral("EDITOR - type here, or route text in"));

    QLabel *hC = nullptr, *hD = nullptr, *hE = nullptr;
    auto *terms = new QSplitter(Qt::Horizontal);
    terms->addWidget(labelled(QStringLiteral("CONSULTANT"), consultantProg + " @ " + consultantDir, consultant, &hC));
    terms->addWidget(labelled(QStringLiteral("DEVELOPER"),  developerProg  + " @ " + projectDir,    developer,  &hD));
    auto *vsplit = new QSplitter(Qt::Vertical);
    vsplit->addWidget(terms);
    vsplit->addWidget(labelled(QStringLiteral("EDITOR"), QStringLiteral("(scratch buffer)"), editor, &hE));
    vsplit->setStretchFactor(0, 3);
    vsplit->setStretchFactor(1, 2);

    auto *status = new QLabel;
    status->setContentsMargins(6, 2, 6, 2);
    auto say = [status, &err](const QString &m) {
        status->setText(m);
        err << "[status] " << m << "\n";   // spike aid: the live log mirrors the label
        err.flush();
    };

    // --- selection -------------------------------------------------------
    // Read live at send time, never cached, so a selection that has since
    // vanished can never be routed as stale text.
    auto liveText = [consultant, developer, editor](Surface s) -> QString {
        if (s == SConsultant) return consultant->selectedText(true);
        if (s == SDeveloper)  return developer->selectedText(true);
        if (s == SEditor) {
            QString t = editor->textCursor().selectedText();
            return t.replace(QChar(0x2029), QLatin1Char('\n'));  // QTextCursor uses U+2029
        }
        return QString();
    };
    Surface lastSel = SNone, focused = SNone;
    auto pickSource = [&](Surface dest) -> Surface {
        for (Surface s : {focused, lastSel})                       // focused wins, then most recent
            if (s != SNone && s != dest && !liveText(s).isEmpty()) return s;
        for (Surface s : {SConsultant, SDeveloper, SEditor})
            if (s != dest && !liveText(s).isEmpty()) return s;
        return SNone;
    };

    // --- delivery --------------------------------------------------------
    auto sendTo = [&](Surface dest) {
        const Surface src = pickSource(dest);
        if (src == SNone) { say(QStringLiteral("Nothing selected")); return; }
        QString text = liveText(src);
        if (dest == SConsultant || dest == SDeveloper) {
            while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r')))
                text.chop(1);                                       // never submit
            if (text.isEmpty()) { say(QStringLiteral("Selection is only line breaks")); return; }
            QTermWidget *t = (dest == SConsultant) ? consultant : developer;
            const int visible = text.size();   // count before bracketing adds 12 marker bytes
            t->bracketText(text);      // wraps in \e[200~..\e[201~ only if the app enabled it
            t->sendText(text);         // exactly these bytes; no Enter is ever appended
            t->setFocus(Qt::OtherFocusReason);
            say(QStringLiteral("Sent %1 chars from %2 to %3 - focus moved, not submitted")
                    .arg(visible).arg(kName[src], kName[dest]));
            return;
        } else {
            editor->insertPlainText(text);   // replaces the selection, else inserts at cursor
            editor->setFocus(Qt::OtherFocusReason);
        }
        say(QStringLiteral("Sent %1 chars from %2 to %3 - focus moved, not submitted")
                .arg(text.size()).arg(kName[src], kName[dest]));
    };

    auto *bar = new QHBoxLayout;
    const struct { const char *label; Surface dest; } kBtn[] = {
        {"Send to Consultant", SConsultant}, {"Send to Developer", SDeveloper}, {"Send to Editor", SEditor}};
    for (const auto &b : kBtn) {
        auto *btn = new QPushButton(QString::fromLatin1(b.label));
        btn->setFocusPolicy(Qt::NoFocus);   // clicking must not move focus off the source
        QObject::connect(btn, &QPushButton::clicked, [&sendTo, d = b.dest] { sendTo(d); });
        bar->addWidget(btn);
    }
    bar->addStretch(1);

    // --- focus indication + selection tracking ---------------------------
    auto markFocus = [&](Surface s) {
        focused = s;
        const QLabel *hs[] = {hC, hD, hE};
        for (int i = 0; i < 3; ++i)
            const_cast<QLabel *>(hs[i])->setStyleSheet(
                i == s ? QStringLiteral("font-weight:bold; background:#cde8ff;") : QString());
    };
    QObject::connect(consultant, &QTermWidget::termGetFocus, [&] { markFocus(SConsultant); });
    QObject::connect(developer,  &QTermWidget::termGetFocus, [&] { markFocus(SDeveloper); });
    QObject::connect(editor, &QPlainTextEdit::cursorPositionChanged, [&] { markFocus(SEditor); });
    QObject::connect(consultant, &QTermWidget::copyAvailable, [&](bool on) {
        if (on) { lastSel = SConsultant; say(QStringLiteral("CONSULTANT selection: %1 chars")
                                                .arg(liveText(SConsultant).size())); }
    });
    QObject::connect(developer, &QTermWidget::copyAvailable, [&](bool on) {
        if (on) { lastSel = SDeveloper; say(QStringLiteral("DEVELOPER selection: %1 chars")
                                                .arg(liveText(SDeveloper).size())); }
    });
    QObject::connect(editor, &QPlainTextEdit::selectionChanged, [&] {
        if (editor->textCursor().hasSelection()) lastSel = SEditor;
    });

    auto *win = new QWidget;
    win->setWindowTitle(QStringLiteral("ArchiDev2 Qt spike - %1").arg(projectDir));
    auto *root = new QVBoxLayout(win);
    root->addLayout(bar);
    root->addWidget(vsplit, 1);
    root->addWidget(status);
    win->resize(1400, 850);
    win->show();
    say(QStringLiteral("Select text in any surface, then click a destination."));

    // Spike-only, non-interactive check (ARCHIDEV2_SPIKE_SELFTEST=1): drives the
    // real send path so "inserted but not submitted" can be proven without a GUI.
    if (qEnvironmentVariableIsSet("ARCHIDEV2_SPIKE_SELFTEST")) {
        QTimer::singleShot(2500, [&] {
            editor->setPlainText(QStringLiteral("touch /tmp/ad2-selftest-EXECUTED\nsecond line\n"));
            editor->selectAll();
            sendTo(SDeveloper);
        });
    }
    return app.exec();
}
