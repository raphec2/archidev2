// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "SessionHistory.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

// Lists one CLI's recorded conversations for one working directory and lets
// the user pick exactly one. Nothing is preselected: Resume stays disabled
// until a row is chosen, so the dialog can never resume by default.
class ResumeDialog : public QDialog {
    Q_OBJECT
public:
    ResumeDialog(const QString &role, const AiProfile &profile, HistoryKind kind,
                 const QString &cwd, QWidget *parent = nullptr);

    // Valid after exec() returns Accepted.
    SavedSession chosen() const { return m_chosen; }

    // The equivalent shell command, for running the same resume elsewhere.
    static QString commandLine(const AiProfile &profile, HistoryKind kind,
                               const QString &cwd, const QString &id);

signals:
    void message(const QString &text);

private:
    void populate();
    void applyFilter(const QString &text);
    void selectionChanged();
    const SavedSession *current() const;

    AiProfile m_profile;
    HistoryKind m_kind;
    QString m_cwd;
    QVector<SavedSession> m_sessions;
    SavedSession m_chosen;

    QTreeWidget *m_list = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_detail = nullptr;
    QPushButton *m_resume = nullptr;
    QPushButton *m_copy = nullptr;
};
