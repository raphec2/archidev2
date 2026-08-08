// SPDX-License-Identifier: GPL-2.0-or-later
#include "ProjectConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

ProjectConfig::ProjectConfig(const QString &projectDir)
    : m_path(QDir(projectDir).absoluteFilePath(QStringLiteral(".archidev2.ini"))),
      m_s(std::make_unique<QSettings>(m_path, QSettings::IniFormat)) {}

bool ProjectConfig::exists() const { return QFileInfo::exists(m_path); }

QString ProjectConfig::defaultTemplate(const QString &consultantDir) {
    return QStringLiteral(
        "# ArchiDev2 project-local configuration.\n"
        "# This is the ONLY place ArchiDev2 stores settings - nothing is written\n"
        "# to your home directory. It may contain machine-specific absolute paths,\n"
        "# so add .archidev2.ini to .gitignore if it should stay local.\n"
        "\n"
        "[workspace]\n"
        "consultant_directory=%1\n"
        "# notes_file: a plain text file opened by the \"Open Notes\" button.\n"
        "# Relative paths resolve against consultant_directory.\n"
        "notes_file=\n"
        "\n"
        "[roles]\n"
        "consultant_profile=Codex\n"
        "developer_profile=Claude\n"
        "\n"
        "# An AI is only a name, a program and arguments. No provider adapters.\n"
        "# arguments is a comma-separated list; QSettings quotes entries when needed.\n"
        "[profiles/Codex]\n"
        "program=codex\n"
        "arguments=\n"
        "\n"
        "[profiles/Claude]\n"
        "program=claude\n"
        "arguments=\n"
        "\n"
        "# Examples - uncomment and adjust if you have them installed:\n"
        "# [profiles/Gemini]\n"
        "# program=gemini\n"
        "# arguments=\n"
        "# [profiles/LocalQwen]\n"
        "# program=ollama\n"
        "# arguments=run, qwen3\n"
        "\n"
        "[shell]\n"
        "# Blank uses $SHELL, falling back to /bin/bash.\n"
        "program=\n"
        "arguments=-l\n"
        "\n"
        "# File explorers.\n"
        "#   Start   = the directory an explorer opens at and that [Start] returns to.\n"
        "#             Consultant Start is consultant_directory; Project Start is the project.\n"
        "#   Current = where it happens to be looking now, saved when ArchiDev2 closes.\n"
        "#             An invalid or out-of-bounds value simply falls back to Start.\n"
        "#   [Parent] walks up from Current. allow_above_start=false stops it at Start and\n"
        "#            resolves symlinks first, so the boundary cannot be side-stepped.\n"
        "#   Explorer navigation NEVER changes any terminal working directory.\n"
        "#   sort: name_asc | name_desc | modified_newest | modified_oldest |\n"
        "#         size_largest | size_smallest   (folders always come first)\n"
        "[explorers]\n"
        "consultant_visible=true\n"
        "consultant_current=\n"
        "consultant_sort=name_asc\n"
        "consultant_allow_above_start=true\n"
        "project_visible=true\n"
        "project_current=\n"
        "project_sort=name_asc\n"
        "project_allow_above_start=false\n"
        "\n"
        "# Splitter percentages, rewritten when ArchiDev2 closes.\n"
        "[layout]\n"
        "top_consultant=55\n"
        "top_developer=45\n"
        "consultant_files=24\n"
        "consultant_term=76\n"
        "bottom_project_files=16\n"
        "bottom_notes=52\n"
        "bottom_shell=32\n")
        .arg(consultantDir);
}

bool ProjectConfig::createTemplate(const QString &consultantDir, QString *err) {
    const QFileInfo dir(QFileInfo(m_path).absolutePath());
    if (!dir.isWritable()) {
        *err = QStringLiteral("project directory is not writable: %1").arg(dir.absoluteFilePath());
        return false;
    }
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)
        || f.write(defaultTemplate(consultantDir).toUtf8()) < 0 || !f.commit()) {
        *err = QStringLiteral("cannot write %1: %2").arg(m_path, f.errorString());
        return false;
    }
    m_s = std::make_unique<QSettings>(m_path, QSettings::IniFormat);   // re-read what we wrote
    return true;
}

QString ProjectConfig::str(const QString &key, const QString &fallback) const {
    const QVariant v = m_s->value(key);
    return v.isValid() ? v.toString() : fallback;
}
bool ProjectConfig::flag(const QString &key, bool fallback) const {
    const QString v = str(key).trimmed().toLower();
    if (v == QLatin1String("true") || v == QLatin1String("1")) return true;
    if (v == QLatin1String("false") || v == QLatin1String("0")) return false;
    return fallback;
}
int ProjectConfig::num(const QString &key, int fallback) const {
    bool ok = false;
    const int n = str(key).trimmed().toInt(&ok);
    return ok ? n : fallback;
}
QStringList ProjectConfig::strList(const QString &key) const {
    const QVariant v = m_s->value(key);
    if (!v.isValid()) return {};
    if (v.canConvert<QStringList>() && v.type() == QVariant::StringList) return v.toStringList();
    const QString s = v.toString().trimmed();
    if (s.isEmpty()) return {};
    QStringList out;
    for (const QString &part : s.split(QLatin1Char(','))) out << part.trimmed();
    return out;
}

void ProjectConfig::set(const QString &key, const QVariant &v) {
    m_pending.insert(key, v.toString());
}

// Writes pending values by editing the INI text in place.
//
// QSettings::sync() would rewrite the whole file in its own representation:
// every comment is discarded and "[profiles/Codex]" becomes "[profiles]" with
// "Codex\program=". This file is the only settings surface ArchiDev2 has and is
// meant to be hand-edited, so the comments and the documented section layout
// are worth preserving. Reads still go through QSettings.
void ProjectConfig::sync() {
    if (m_pending.isEmpty()) return;

    QFile in(m_path);
    QStringList lines;
    if (in.open(QIODevice::ReadOnly | QIODevice::Text))
        lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
    in.close();

    QMap<QString, QString> todo = m_pending;
    QString section;
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines.at(i).trimmed();
        if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']'))) {
            section = t.mid(1, t.size() - 2);
            continue;
        }
        if (t.isEmpty() || t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1Char(';'))) continue;
        const int eq = t.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString full = section.isEmpty() ? t.left(eq).trimmed()
                                               : section + QLatin1Char('/') + t.left(eq).trimmed();
        auto it = todo.find(full);
        if (it != todo.end()) {
            lines[i] = QStringLiteral("%1=%2").arg(t.left(eq).trimmed(), it.value());
            todo.erase(it);
        }
    }

    // Anything not already present is appended under its own section.
    for (auto it = todo.constBegin(); it != todo.constEnd(); ++it) {
        const int slash = it.key().lastIndexOf(QLatin1Char('/'));
        const QString sec = slash > 0 ? it.key().left(slash) : QString();
        const QString name = slash > 0 ? it.key().mid(slash + 1) : it.key();
        int at = -1;
        for (int i = 0; i < lines.size(); ++i)
            if (lines.at(i).trimmed() == QStringLiteral("[%1]").arg(sec)) { at = i; break; }
        if (at < 0) {
            if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) lines << QString();
            lines << QStringLiteral("[%1]").arg(sec);
            lines << QStringLiteral("%1=%2").arg(name, it.value());
        } else {
            int end = at + 1;
            while (end < lines.size() && !lines.at(end).trimmed().startsWith(QLatin1Char('['))) ++end;
            lines.insert(end, QStringLiteral("%1=%2").arg(name, it.value()));
        }
    }

    QSaveFile f(m_path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)
        && f.write(lines.join(QLatin1Char('\n')).toUtf8()) >= 0 && f.commit()) {
        m_pending.clear();
        m_s = std::make_unique<QSettings>(m_path, QSettings::IniFormat);   // re-read
    }
}

bool ProjectConfig::hasProfileGroup() const {
    return !profileNames().isEmpty();
}
QStringList ProjectConfig::profileNames() const {
    // Derived from allKeys() rather than beginGroup()/childGroups(): QSettings
    // loads lazily, and childGroups() returns nothing when it is the very first
    // access to the object. allKeys() always forces the parse.
    QStringList names;
    const QString prefix = QStringLiteral("profiles/");
    for (const QString &key : m_s->allKeys()) {
        if (!key.startsWith(prefix)) continue;
        const QString rest = key.mid(prefix.size());
        const int slash = rest.indexOf(QLatin1Char('/'));
        if (slash <= 0) continue;
        const QString name = rest.left(slash);
        if (!names.contains(name)) names << name;
    }
    return names;
}
