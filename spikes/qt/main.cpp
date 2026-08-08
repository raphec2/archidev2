// SPDX-License-Identifier: GPL-2.0-or-later
//
// ArchiDev2 - a CLI-launched native workspace for working with terminal AIs.
// All configuration is project-local: <project>/.archidev2.ini and nothing else.
#include "MainWindow.h"
#include "ProjectConfig.h"
#include "Profiles.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <unistd.h>

static bool resolveDir(const QString &raw, QString *out, QString *err) {
    if (raw.trimmed().isEmpty()) { *err = QStringLiteral("empty path"); return false; }
    const QFileInfo fi(QDir::current().absoluteFilePath(QDir::fromNativeSeparators(raw)));
    if (!fi.exists())     { *err = QStringLiteral("no such directory: %1").arg(raw); return false; }
    if (!fi.isDir())      { *err = QStringLiteral("not a directory: %1").arg(raw);   return false; }
    if (!fi.isReadable()) { *err = QStringLiteral("not readable: %1").arg(raw);      return false; }
    *out = fi.canonicalFilePath();
    return true;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTextStream err(stderr), in(stdin);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ArchiDev2 - consultant/developer/shell AI workspace.\n"
                       "All settings live in <project>/.archidev2.ini; nothing is written to $HOME."));
    parser.addHelpOption();
    QCommandLineOption optCDir(QStringLiteral("consultant-dir"),
        QStringLiteral("Consultant working directory (written to the INI on first run)."), QStringLiteral("path"));
    QCommandLineOption optCProf(QStringLiteral("consultant-profile"),
        QStringLiteral("Consultant AI profile name for this launch only."), QStringLiteral("name"));
    QCommandLineOption optDProf(QStringLiteral("developer-profile"),
        QStringLiteral("Developer AI profile name for this launch only."), QStringLiteral("name"));
    QCommandLineOption optShell(QStringLiteral("shell"),
        QStringLiteral("Program for the project shell (default: $SHELL)."), QStringLiteral("program"));
    for (const auto &o : {optCDir, optCProf, optDProf, optShell}) parser.addOption(o);
    parser.addPositionalArgument(QStringLiteral("project-directory"),
        QStringLiteral("Project directory (default: current directory)."));
    parser.process(app);

    const QStringList pos = parser.positionalArguments();
    if (pos.size() > 1) { err << "archidev2: expected at most one project directory\n"; return 2; }

    QString projectDir, msg;
    if (!resolveDir(pos.isEmpty() ? QStringLiteral(".") : pos.at(0), &projectDir, &msg)) {
        err << "archidev2: project: " << msg << "\n";
        return 2;
    }

    ProjectConfig cfg(projectDir);
    const bool firstRun = !cfg.exists();

    // --- consultant directory --------------------------------------------
    QString consultantDir;
    if (parser.isSet(optCDir)) {
        if (!resolveDir(parser.value(optCDir), &consultantDir, &msg)) {
            err << "archidev2: --consultant-dir: " << msg << "\n";
            return 2;
        }
    } else if (!firstRun) {
        const QString saved = cfg.str(QStringLiteral("workspace/consultant_directory"));
        if (!saved.isEmpty() && !resolveDir(saved, &consultantDir, &msg))
            err << "archidev2: consultant_directory in " << cfg.iniPath() << ": " << msg << "\n";
    }

    if (consultantDir.isEmpty()) {
        // No desktop chooser: this is a CLI product. Prompt only on a real TTY.
        if (::isatty(STDIN_FILENO)) {
            err << "ArchiDev2 first run for " << projectDir << "\n"
                << "Consultant working directory (where the consultant AI starts): ";
            err.flush();
            const QString typed = in.readLine();
            if (!typed.trimmed().isEmpty() && !resolveDir(typed, &consultantDir, &msg))
                err << "archidev2: " << msg << "\n";
        }
        if (consultantDir.isEmpty()) {
            if (firstRun) {
                QString werr;
                if (!cfg.createTemplate(QString(), &werr)) { err << "archidev2: " << werr << "\n"; return 4; }
                err << "archidev2: created " << cfg.iniPath() << "\n";
            }
            err << "archidev2: no consultant directory. Run:\n"
                << "  archidev2 --consultant-dir /path/to/consulting " << projectDir << "\n"
                << "or set consultant_directory under [workspace] in " << cfg.iniPath() << "\n";
            return 3;
        }
    }

    // --- project-local INI -------------------------------------------------
    if (firstRun) {
        QString werr;
        if (!cfg.createTemplate(consultantDir, &werr)) { err << "archidev2: " << werr << "\n"; return 4; }
        err << "archidev2: created " << cfg.iniPath() << "\n";
    } else if (cfg.str(QStringLiteral("workspace/consultant_directory")) != consultantDir
               && parser.isSet(optCDir)) {
        cfg.set(QStringLiteral("workspace/consultant_directory"), consultantDir);
    }

    // --- profiles and roles -------------------------------------------------
    Launch launch;
    launch.projectDir = projectDir;
    launch.consultantDir = consultantDir;
    launch.profiles = loadAiProfiles(cfg, &launch.problems);
    launch.consultantProfile = parser.isSet(optCProf)
        ? parser.value(optCProf)
        : cfg.str(QStringLiteral("roles/consultant_profile"), QStringLiteral("Codex"));
    launch.developerProfile = parser.isSet(optDProf)
        ? parser.value(optDProf)
        : cfg.str(QStringLiteral("roles/developer_profile"), QStringLiteral("Claude"));
    launch.shellProgram = parser.isSet(optShell) ? parser.value(optShell)
                                                 : cfg.str(QStringLiteral("shell/program"));
    launch.shellArguments = cfg.strList(QStringLiteral("shell/arguments"));
    if (launch.shellArguments.isEmpty() && !cfg.exists())
        launch.shellArguments = QStringList{QStringLiteral("-l")};

    Theme::apply(app);
    MainWindow w(std::move(launch), cfg);
    w.show();
    return app.exec();
}
