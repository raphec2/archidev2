// SPDX-License-Identifier: GPL-2.0-or-later
//
// AI command profiles. An "AI" is only a display name, an executable and an
// argument list - no provider adapters, no SDKs, no model-specific behaviour.
// Profiles come from the project-local .archidev2.ini; there is no central file.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class ProjectConfig;

struct AiProfile {
    QString name;
    QString program;
    QStringList arguments;

    // Absolute path to the executable, or empty when it cannot be found.
    // Absolute/relative paths are used as given; bare names go through PATH.
    QString resolvedProgram() const;
    bool available() const { return !resolvedProgram().isEmpty(); }
};

// Codex and Claude, compiled in. Returned whether or not they are installed;
// availability is reported separately so the UI can mark them.
QVector<AiProfile> builtinAiProfiles();

// Reads [profiles/<Name>] groups. Each rejected entry appends one line to
// *problems; one bad entry never hides the good ones. Built-ins are appended
// only for names the INI does not define.
QVector<AiProfile> loadAiProfiles(const ProjectConfig &cfg, QStringList *problems);
