// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Profiles.h"

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QWidget>

class ExplorerPane;
class ProjectConfig;
class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTermWidget;
class QVBoxLayout;

// Everything main.cpp resolves before the window exists.
struct Launch {
    QString projectDir;
    QString consultantDir;
    QString consultantProfile;
    QString developerProfile;
    QString shellProgram;
    QStringList shellArguments;
    QVector<AiProfile> profiles;
    QStringList problems;
};

class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(Launch launch, ProjectConfig &cfg, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    // Routing surfaces. Every routing button belongs to exactly one source and
    // reads only that source's live selection.
    enum Surface { SConsultant, SDeveloper, SShell, SNotes, SurfaceCount };

    struct Term {
        QTermWidget *term = nullptr;
        QVBoxLayout *slot = nullptr;
        QLabel *header = nullptr;
        QFrame *frame = nullptr;
        QComboBox *combo = nullptr;
        QLabel *pending = nullptr;
        QString runningProfile;
        QString cwd;
        QString title;
    };

    // --- construction
    QWidget *makePane(const QString &title, QWidget *inner, QLabel **hdr,
                      QVBoxLayout **slot, QFrame **frame);
    QHBoxLayout *makeRouteBar(Surface source, const QVector<Surface> &destinations);
    QWidget *buildConsultantArea();
    QWidget *buildDeveloperArea();
    QWidget *buildBottomRow();
    void wireEditor();
    void restoreLayout();
    void saveLayout();

    // --- terminals
    bool launchTerm(Term &t, Surface which, const AiProfile &p);
    void restartAi(Term &t, Surface which);
    void restartShell();
    AiProfile profileByName(const QString &n) const;
    AiProfile shellProfile() const;
    void refreshPending(Term &t);
    Term *termFor(Surface s);
    QComboBox *addRoleControls(QHBoxLayout *bar, Term &t, Surface which, const QString &label);

    // --- source-local routing
    QString liveText(Surface s) const;
    void routeFrom(Surface source, Surface dest);
    void markFocus(Surface s);

    // --- editor / documents
    bool saveDoc();
    bool confirmDiscard(const QString &what);
    void openFile(const QString &path);
    void openNotes();
    void refreshDoc();
    void sendFileToConsultant();

    void say(const QString &m, const char *level = "");

    Launch m_l;
    ProjectConfig &m_cfg;

    Term m_consultant, m_developer, m_shell;
    QPlainTextEdit *m_editor = nullptr;
    QLabel *m_editorHeader = nullptr;
    QFrame *m_editorFrame = nullptr;
    ExplorerPane *m_consultantFiles = nullptr;
    ExplorerPane *m_projectFiles = nullptr;
    QWidget *m_consultantFilesHost = nullptr;
    QPushButton *m_consultantCollapse = nullptr;
    QSplitter *m_consultantSplit = nullptr;
    QSplitter *m_topSplit = nullptr;
    QSplitter *m_bottomSplit = nullptr;
    QLabel *m_status = nullptr;

    QString m_docPath;
    QDateTime m_docMtime;
    qint64 m_docSize = -1;
    bool m_docEndedWithNewline = true;
    Surface m_focused = SConsultant;
};
