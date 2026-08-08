// SPDX-License-Identifier: GPL-2.0-or-later
#include "Profiles.h"
#include "ProjectConfig.h"

#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

QString AiProfile::resolvedProgram() const {
    if (program.isEmpty()) return QString();
    if (program.contains(QLatin1Char('/'))) {
        const QFileInfo fi(program);
        return (fi.isFile() && fi.isExecutable()) ? fi.absoluteFilePath() : QString();
    }
    return QStandardPaths::findExecutable(program);      // PATH lookup, no shell
}

QVector<AiProfile> builtinAiProfiles() {
    return {AiProfile{QStringLiteral("Codex"),  QStringLiteral("codex"),  {}},
            AiProfile{QStringLiteral("Claude"), QStringLiteral("claude"), {}}};
}

QVector<AiProfile> loadAiProfiles(const ProjectConfig &cfg, QStringList *problems) {
    QVector<AiProfile> out;
    QSet<QString> seen;

    for (const QString &name : cfg.profileNames()) {
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty()) { *problems << QStringLiteral("a [profiles/...] group has an empty name"); continue; }
        if (seen.contains(trimmed)) { *problems << QStringLiteral("\"%1\": duplicate profile, ignored").arg(trimmed); continue; }

        const QString prog = cfg.str(QStringLiteral("profiles/%1/program").arg(name)).trimmed();
        if (prog.isEmpty()) {
            *problems << QStringLiteral("\"%1\": program= must not be empty").arg(trimmed);
            continue;
        }
        // QSettings parses "a, b" into a QStringList; a single value arrives as
        // a string. Both are accepted; empty entries are dropped.
        QStringList args;
        for (const QString &a : cfg.strList(QStringLiteral("profiles/%1/arguments").arg(name)))
            if (!a.isEmpty()) args << a;

        // QTermWidget's Session::setProgram/setArguments/setInitialWorkingDirectory
        // all run ShellCommand::expand(), which substitutes $VAR, and its "\$"
        // escape leaves the backslash behind - a literal '$' cannot be delivered.
        if (prog.contains(QLatin1Char('$')) || args.join(QLatin1Char(' ')).contains(QLatin1Char('$')))
            *problems << QStringLiteral("\"%1\": contains '$' - QTermWidget expands $VAR itself "
                                        "and ArchiDev2 cannot prevent it").arg(trimmed);

        seen.insert(trimmed);
        out.append(AiProfile{trimmed, prog, args});
    }

    for (const AiProfile &b : builtinAiProfiles())
        if (!seen.contains(b.name)) out.append(b);
    return out;
}
