// SPDX-License-Identifier: GPL-2.0-or-later
#include "SessionHistory.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

// These are the CLIs' private on-disk formats, read as they were observed in
// Codex CLI 0.166.0 and Claude Code 2.1.317. If a format changes, sessions
// simply stop being listed; the ID passed to the CLI is always validated by
// the CLI itself, so a parsing mistake cannot resume the wrong conversation
// silently - the picker shows what will be resumed before anything happens.

static const qint64 kMaxPromptScanBytes = 4 * 1024 * 1024;

HistoryKind historyKindFor(const AiProfile &p) {
    const QString path = p.resolvedProgram().isEmpty() ? p.program : p.resolvedProgram();
    const QString base = QFileInfo(path).fileName();
    if (base == QLatin1String("codex"))  return HistoryKind::Codex;
    if (base == QLatin1String("claude")) return HistoryKind::Claude;
    return HistoryKind::None;
}

QString historyKindName(HistoryKind k) {
    switch (k) {
    case HistoryKind::Codex:  return QStringLiteral("Codex");
    case HistoryKind::Claude: return QStringLiteral("Claude");
    default:                  return QString();
    }
}

bool isSessionId(const QString &id) {
    static const QRegularExpression re(QStringLiteral(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return re.match(id).hasMatch();
}

QStringList resumeArguments(HistoryKind k, const QString &id, const QStringList &profileArgs) {
    if (!isSessionId(id)) return {};
    if (k == HistoryKind::Codex)
        return QStringList{QStringLiteral("resume")} + profileArgs + QStringList{id};
    if (k == HistoryKind::Claude)
        return profileArgs + QStringList{QStringLiteral("--resume"), id};
    return {};
}

// --------------------------------------------------------------------------

static bool sameDir(const QString &a, const QString &b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    if (QDir::cleanPath(a) == QDir::cleanPath(b)) return true;
    const QString ca = QFileInfo(a).canonicalFilePath();
    return !ca.isEmpty() && ca == QFileInfo(b).canonicalFilePath();
}

static QString oneLine(QString s, int max = 160) {
    s = s.simplified();
    if (s.size() > max) s = s.left(max - 1) + QChar(0x2026);
    return s;
}

static QDateTime isoTime(const QJsonValue &v) {
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toLocalTime() : QDateTime();
}

static QString homeFor(const char *envVar, const char *dotDir) {
    const QString env = qEnvironmentVariable(envVar);
    return env.isEmpty() ? QDir::home().filePath(QLatin1String(dotDir)) : env;
}

static QVector<QFileInfo> newestFirst(QVector<QFileInfo> files) {
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });
    return files;
}

// ---- Codex ---------------------------------------------------------------
// $CODEX_HOME/sessions/YYYY/MM/DD/rollout-<time>-<id>.jsonl. The first line is
// a session_meta record carrying id, cwd and start time. Titles live in
// $CODEX_HOME/session_index.jsonl ({"id","thread_name"}); the last entry wins.

static QString codexFirstPrompt(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    while (!f.atEnd() && f.pos() < kMaxPromptScanBytes) {
        const QByteArray line = f.readLine();
        if (!line.contains("\"role\":\"user\"") && !line.contains("\"user_message\"")) continue;
        const QJsonObject p = QJsonDocument::fromJson(line).object().value(QLatin1String("payload")).toObject();
        QString text;
        if (p.value(QLatin1String("type")).toString() == QLatin1String("user_message")) {
            text = p.value(QLatin1String("message")).toString();
        } else if (p.value(QLatin1String("role")).toString() == QLatin1String("user")) {
            for (const QJsonValue &c : p.value(QLatin1String("content")).toArray())
                if (c.toObject().value(QLatin1String("type")).toString() == QLatin1String("input_text")) {
                    text = c.toObject().value(QLatin1String("text")).toString();
                    break;
                }
        }
        const QString t = text.trimmed();
        // Codex injects instructions and environment context as user messages.
        if (t.isEmpty() || t.startsWith(QLatin1Char('<')) || t.startsWith(QLatin1String("# AGENTS.md")))
            continue;
        return oneLine(t);
    }
    return QString();
}

static QVector<SavedSession> listCodex(const QString &cwd, QString *note, int limit) {
    const QString home = homeFor("CODEX_HOME", ".codex");
    const QString root = QDir(home).filePath(QStringLiteral("sessions"));
    if (!QFileInfo(root).isDir()) { *note = QStringLiteral("No Codex history at %1").arg(root); return {}; }

    QHash<QString, QString> titles;
    QFile idx(QDir(home).filePath(QStringLiteral("session_index.jsonl")));
    if (idx.open(QIODevice::ReadOnly)) {
        while (!idx.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(idx.readLine()).object();
            const QString id = o.value(QLatin1String("id")).toString();
            const QString name = o.value(QLatin1String("thread_name")).toString();
            if (!id.isEmpty() && !name.isEmpty()) titles.insert(id, name);
        }
    }

    QVector<QFileInfo> files;
    QDirIterator it(root, {QStringLiteral("rollout-*.jsonl")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); files << it.fileInfo(); }

    QVector<SavedSession> out;
    for (const QFileInfo &fi : newestFirst(files)) {
        if (out.size() >= limit) break;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject rec = QJsonDocument::fromJson(f.readLine()).object();
        if (rec.value(QLatin1String("type")).toString() != QLatin1String("session_meta")) continue;
        const QJsonObject p = rec.value(QLatin1String("payload")).toObject();
        if (!sameDir(p.value(QLatin1String("cwd")).toString(), cwd)) continue;
        // Non-interactive `codex exec` runs are not conversations to continue.
        if (p.value(QLatin1String("originator")).toString() == QLatin1String("codex_exec")
            || p.value(QLatin1String("source")).toString() == QLatin1String("exec"))
            continue;
        SavedSession s;
        s.id = p.value(QLatin1String("id")).toString();
        if (!isSessionId(s.id)) continue;
        s.title = oneLine(titles.value(s.id));
        s.started = isoTime(p.value(QLatin1String("timestamp")));
        s.lastActive = fi.lastModified();
        s.path = fi.absoluteFilePath();
        s.firstPrompt = codexFirstPrompt(s.path);
        out << s;
    }
    if (out.isEmpty()) *note = QStringLiteral("No Codex conversations recorded for %1").arg(cwd);
    return out;
}

// ---- Claude --------------------------------------------------------------
// $CLAUDE_CONFIG_DIR/projects/<cwd with separators as '-'>/<id>.jsonl, one
// JSON record per line. Records carry cwd, sessionId and timestamp; titles are
// "custom-title" (/rename, --name), "ai-title" or "summary" records.

static QString claudeText(const QJsonValue &content) {
    if (content.isString()) return content.toString();
    for (const QJsonValue &c : content.toArray()) {
        const QJsonObject o = c.toObject();
        if (o.value(QLatin1String("type")).toString() == QLatin1String("text"))
            return o.value(QLatin1String("text")).toString();
    }
    return QString();
}

static bool readClaudeSession(const QFileInfo &fi, const QString &cwd, SavedSession *s) {
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    QString custom, ai, summary, prompt;
    bool cwdOk = false, cwdSeen = false;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        const bool user = !prompt.isEmpty() ? false : line.contains("\"type\":\"user\"");
        const bool title = line.contains("\"customTitle\"") || line.contains("\"aiTitle\"")
                           || line.contains("\"type\":\"summary\"");
        if (!user && !title && cwdSeen) continue;
        if (!user && !title && !line.contains("\"cwd\"")) continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.contains(QLatin1String("cwd")) && !cwdSeen) {
            cwdSeen = true;
            cwdOk = sameDir(o.value(QLatin1String("cwd")).toString(), cwd);
            if (!cwdOk) return false;
        }
        if (o.contains(QLatin1String("customTitle"))) custom = o.value(QLatin1String("customTitle")).toString();
        if (o.contains(QLatin1String("aiTitle")))     ai = o.value(QLatin1String("aiTitle")).toString();
        if (o.value(QLatin1String("type")).toString() == QLatin1String("summary"))
            summary = o.value(QLatin1String("summary")).toString();
        if (user && o.value(QLatin1String("type")).toString() == QLatin1String("user")
            && !o.value(QLatin1String("isMeta")).toBool() && !o.value(QLatin1String("isSidechain")).toBool()) {
            const QString t = claudeText(o.value(QLatin1String("message")).toObject()
                                             .value(QLatin1String("content"))).trimmed();
            if (!t.isEmpty() && !t.startsWith(QLatin1String("<command-"))
                && !t.startsWith(QLatin1String("<local-command")) && !t.startsWith(QLatin1String("Caveat:"))) {
                prompt = oneLine(t);
                s->started = isoTime(o.value(QLatin1String("timestamp")));
            }
        }
    }
    if (!cwdOk || prompt.isEmpty()) return false;     // never used, or another directory
    s->firstPrompt = prompt;
    s->title = oneLine(!custom.isEmpty() ? custom : !ai.isEmpty() ? ai : summary);
    return true;
}

static QVector<SavedSession> listClaude(const QString &cwd, QString *note, int limit) {
    const QString projects = QDir(homeFor("CLAUDE_CONFIG_DIR", ".claude")).filePath(QStringLiteral("projects"));
    // Claude names the folder after the directory with separators turned into
    // '-'. Try the plausible spellings; each file's own cwd is still checked.
    QSet<QString> names;
    const QString clean = QDir::cleanPath(cwd);
    for (const QString &p : {clean, QFileInfo(cwd).canonicalFilePath()}) {
        if (p.isEmpty()) continue;
        QString a = p, b = p, c = p;
        names << a.replace(QLatin1Char('/'), QLatin1Char('-'))
              << b.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9.]")), QStringLiteral("-"))
              << c.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")), QStringLiteral("-"));
    }
    QVector<QFileInfo> files;
    for (const QString &n : names) {
        const QDir d(QDir(projects).filePath(n));
        if (d.exists())
            for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("*.jsonl")}, QDir::Files))
                files << fi;
    }
    QVector<SavedSession> out;
    for (const QFileInfo &fi : newestFirst(files)) {
        if (out.size() >= limit) break;
        SavedSession s;
        s.id = fi.completeBaseName();
        if (!isSessionId(s.id)) continue;
        if (!readClaudeSession(fi, cwd, &s)) continue;
        s.lastActive = fi.lastModified();
        s.path = fi.absoluteFilePath();
        out << s;
    }
    if (out.isEmpty()) *note = QStringLiteral("No Claude conversations recorded for %1").arg(cwd);
    return out;
}

QVector<SavedSession> listSessions(HistoryKind k, const QString &cwd, QString *note, int limit) {
    QString dummy;
    if (!note) note = &dummy;
    note->clear();
    switch (k) {
    case HistoryKind::Codex:  return listCodex(cwd, note, limit);
    case HistoryKind::Claude: return listClaude(cwd, note, limit);
    default:
        *note = QStringLiteral("This profile's program is neither codex nor claude, so it has no "
                               "history ArchiDev2 can read");
        return {};
    }
}
