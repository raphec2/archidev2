// SPDX-License-Identifier: GPL-2.0-or-later
//
// Project-local configuration: <project>/.archidev2.ini and nothing else.
// ArchiDev2 never reads or writes any central/user-wide configuration.
#pragma once

#include <QMap>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <memory>

class ProjectConfig {
public:
    // Opens (does not create) <project>/.archidev2.ini.
    explicit ProjectConfig(const QString &projectDir);

    QString iniPath() const { return m_path; }
    bool exists() const;

    // Writes the commented default template. Returns false (with *err set) if
    // the project directory is not writable - never falls back elsewhere.
    bool createTemplate(const QString &consultantDir, QString *err);

    QString  str(const QString &key, const QString &fallback = QString()) const;
    bool     flag(const QString &key, bool fallback) const;
    int      num(const QString &key, int fallback) const;
    QStringList strList(const QString &key) const;
    void     set(const QString &key, const QVariant &v);
    void     sync();

    QStringList profileNames() const;                 // group names under [profiles/...]
    bool  hasProfileGroup() const;

    static QString defaultTemplate(const QString &consultantDir);

private:
    QString m_path;
    std::unique_ptr<QSettings> m_s;
    QMap<QString, QString> m_pending;      // key -> value, flushed by sync()
};
