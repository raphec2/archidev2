// SPDX-License-Identifier: GPL-2.0-or-later
//
// AI command profiles. An "AI" here is nothing more than a display name, an
// executable and an argument list - there are no provider adapters, no SDKs
// and no model-specific behaviour anywhere in ArchiDev2.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct AiProfile {
    QString name;
    QString program;
    QStringList arguments;

    // Absolute path to the executable, or empty when it cannot be found.
    // Absolute program paths are accepted as-is; bare names go through PATH.
    QString resolvedProgram() const;
    bool available() const { return !resolvedProgram().isEmpty(); }
};

// ${XDG_CONFIG_HOME:-$HOME/.config}/archidev2/ai-profiles.json
QString aiProfilesPath();

// Codex and Claude, included only when their executables are present.
QVector<AiProfile> builtinAiProfiles();

// Reads `path` and returns every profile that validates. Each rejected entry
// appends one human-readable line to *problems; one bad entry never hides the
// good ones. A missing file is not a problem and yields an empty list.
QVector<AiProfile> loadAiProfiles(const QString &path, QStringList *problems);

// User profiles win; a built-in is appended only when its name is not already
// taken by a user profile.
QVector<AiProfile> mergeWithBuiltins(const QVector<AiProfile> &user);
