// SPDX-License-Identifier: GPL-2.0-or-later
//
// Read-only view of an AI CLI's own conversation history, so a role can
// resume a conversation after a crash or restart. ArchiDev2 stores nothing:
// it reads what Codex and Claude already record, lists it, and hands the
// chosen session ID back to that CLI's own resume option.
#pragma once

#include "Profiles.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

enum class HistoryKind { None, Codex, Claude };

// Decided by the basename of the profile's program: "codex" or "claude".
// A profile running anything else has no resumable history.
HistoryKind historyKindFor(const AiProfile &p);
QString historyKindName(HistoryKind k);

struct SavedSession {
    QString id;             // the CLI's own session ID (a UUID)
    QString title;          // CLI-generated or user-set title; may be empty
    QString firstPrompt;    // first thing the user typed; may be empty
    QDateTime started;
    QDateTime lastActive;   // modification time of the CLI's session file
    QString path;           // the CLI's session file, for reference only
};

// Conversations recorded for exactly this working directory, most recently
// active first. Never more than `limit`. *note explains an empty result.
QVector<SavedSession> listSessions(HistoryKind k, const QString &cwd, QString *note, int limit = 60);

// A CLI session ID is a UUID; anything else is refused before it reaches argv.
bool isSessionId(const QString &id);

// argv (after the program) that resumes `id` with the profile's own arguments.
//   Codex:  resume <profile args...> <id>
//   Claude: <profile args...> --resume <id>
// No prompt argument is ever added, so nothing is submitted on resume.
QStringList resumeArguments(HistoryKind k, const QString &id, const QStringList &profileArgs);
