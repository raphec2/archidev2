// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QStandardPaths>

QString AiProfile::resolvedProgram() const {
    if (program.isEmpty()) return QString();
    if (program.contains(QLatin1Char('/'))) {           // absolute or relative path
        const QFileInfo fi(program);
        return (fi.isFile() && fi.isExecutable()) ? fi.absoluteFilePath() : QString();
    }
    return QStandardPaths::findExecutable(program);     // PATH lookup, no shell
}

QString aiProfilesPath() {
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty() || !base.startsWith(QLatin1Char('/')))
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/archidev2/ai-profiles.json");
}

QVector<AiProfile> builtinAiProfiles() {
    QVector<AiProfile> out;
    for (const auto &p : {AiProfile{QStringLiteral("Codex"), QStringLiteral("codex"), {}},
                          AiProfile{QStringLiteral("Claude"), QStringLiteral("claude"), {}}})
        if (p.available()) out.append(p);
    return out;
}

QVector<AiProfile> loadAiProfiles(const QString &path, QStringList *problems) {
    QVector<AiProfile> out;
    QFile f(path);
    if (!f.exists()) return out;                        // absent is normal, not an error
    if (!f.open(QIODevice::ReadOnly)) {
        *problems << QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
        return out;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        *problems << QStringLiteral("%1: %2 at offset %3").arg(path, perr.errorString()).arg(perr.offset);
        return out;
    }
    if (!doc.isObject() || !doc.object().value(QStringLiteral("profiles")).isArray()) {
        *problems << QStringLiteral("%1: expected an object with a \"profiles\" array").arg(path);
        return out;
    }

    const QJsonArray arr = doc.object().value(QStringLiteral("profiles")).toArray();
    QSet<QString> seen;
    for (int i = 0; i < arr.size(); ++i) {
        const auto where = [i] { return QStringLiteral("profile #%1").arg(i + 1); };
        if (!arr.at(i).isObject()) { *problems << QStringLiteral("%1: not an object").arg(where()); continue; }
        const QJsonObject o = arr.at(i).toObject();

        const QJsonValue nameV = o.value(QStringLiteral("name"));
        const QJsonValue progV = o.value(QStringLiteral("program"));
        const QJsonValue argsV = o.value(QStringLiteral("arguments"));
        if (!nameV.isString() || nameV.toString().trimmed().isEmpty()) {
            *problems << QStringLiteral("%1: \"name\" must be a non-empty string").arg(where()); continue;
        }
        const QString name = nameV.toString().trimmed();
        if (!progV.isString() || progV.toString().trimmed().isEmpty()) {
            *problems << QStringLiteral("\"%1\": \"program\" must be a non-empty string").arg(name); continue;
        }
        if (!argsV.isArray()) {
            *problems << QStringLiteral("\"%1\": \"arguments\" must be an array (use [] for none)").arg(name); continue;
        }
        QStringList args;
        bool argsOk = true;
        for (const QJsonValue &v : argsV.toArray()) {
            if (!v.isString()) {
                *problems << QStringLiteral("\"%1\": every argument must be a string").arg(name);
                argsOk = false; break;
            }
            args << v.toString();                       // preserved exactly; never shell-parsed
        }
        if (!argsOk) continue;
        if (seen.contains(name)) {
            *problems << QStringLiteral("\"%1\": duplicate name, later entry ignored").arg(name); continue;
        }
        if (o.contains(QStringLiteral("workingDirectory")) || o.contains(QStringLiteral("cwd")))
            *problems << QStringLiteral("\"%1\": working-directory field ignored (the role owns cwd)").arg(name);

        // QTermWidget's Session::setProgram/setArguments/setInitialWorkingDirectory
        // all run ShellCommand::expand(), which substitutes $VAR. ArchiDev2 cannot
        // switch that off through the public API, and its "\\$" escape leaves the
        // backslash behind, so a literal '$' simply cannot be delivered. Warn
        // rather than let it surprise anyone; the profile is still usable.
        const QString prog = progV.toString().trimmed();
        if (prog.contains(QLatin1Char('$')) || args.join(QLatin1Char(' ')).contains(QLatin1Char('$')))
            *problems << QStringLiteral("\"%1\": contains '$' - QTermWidget expands $VAR itself "
                                        "and ArchiDev2 cannot prevent it").arg(name);

        seen.insert(name);
        out.append(AiProfile{name, prog, args});
    }
    return out;
}

QVector<AiProfile> mergeWithBuiltins(const QVector<AiProfile> &user) {
    QVector<AiProfile> out = user;
    QSet<QString> taken;
    for (const auto &p : user) taken.insert(p.name);
    for (const auto &b : builtinAiProfiles())
        if (!taken.contains(b.name)) out.append(b);
    return out;
}
