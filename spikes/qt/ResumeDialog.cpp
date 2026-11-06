// SPDX-License-Identifier: GPL-2.0-or-later
#include "ResumeDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>

static const int kIndexRole = Qt::UserRole + 1;

static QString when(const QDateTime &t) {
    return t.isValid() ? t.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("-");
}

static QString shellQuote(const QString &s) {
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_./=:,+@%-]+$"));
    if (!s.isEmpty() && safe.match(s).hasMatch()) return s;
    QString q = s;
    return QLatin1Char('\'') + q.replace(QLatin1Char('\''), QLatin1String("'\\''")) + QLatin1Char('\'');
}

QString ResumeDialog::commandLine(const AiProfile &profile, HistoryKind kind,
                                  const QString &cwd, const QString &id) {
    QStringList parts{QStringLiteral("cd"), shellQuote(cwd), QStringLiteral("&&"), shellQuote(profile.program)};
    for (const QString &a : resumeArguments(kind, id, profile.arguments)) parts << shellQuote(a);
    return parts.join(QLatin1Char(' '));
}

ResumeDialog::ResumeDialog(const QString &role, const AiProfile &profile, HistoryKind kind,
                           const QString &cwd, QWidget *parent)
    : QDialog(parent), m_profile(profile), m_kind(kind), m_cwd(cwd) {
    const QString cli = historyKindName(kind);
    setWindowTitle(QStringLiteral("Resume a %1 conversation in %2").arg(cli, role));
    resize(1100, 520);

    auto *v = new QVBoxLayout(this);
    auto *intro = new QLabel(QStringLiteral(
        "Choose a previous <b>%1</b> conversation recorded by %1 itself for<br><tt>%2</tt><br>"
        "Resuming <b>ends the current %3 session</b> and starts <tt>%4</tt> on the chosen "
        "conversation. Nothing is typed into the prompt and nothing is submitted. "
        "If the conversation is still open somewhere else, close it there first.<br>"
        "To start a fresh conversation instead, cancel and use <b>New Session</b>.")
        .arg(cli.toHtmlEscaped(), cwd.toHtmlEscaped(), role.toHtmlEscaped(),
             profile.name.toHtmlEscaped()));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    v->addWidget(intro);

    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(QStringLiteral("Filter by title, first prompt or session ID"));
    m_filter->setClearButtonEnabled(true);
    v->addWidget(m_filter);

    m_list = new QTreeWidget;
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHeaderLabels({QStringLiteral("Last active"), QStringLiteral("Title"),
                             QStringLiteral("First prompt"), QStringLiteral("Started"),
                             QStringLiteral("Session ID")});
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    v->addWidget(m_list, 1);

    m_detail = new QLabel;
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_detail);

    auto *buttons = new QDialogButtonBox;
    m_copy = buttons->addButton(QStringLiteral("Copy Resume Command"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    m_resume = buttons->addButton(QStringLiteral("Resume Selected Conversation"), QDialogButtonBox::AcceptRole);
    // Enter in the filter box must never resume whatever happens to be listed.
    for (QPushButton *b : buttons->findChildren<QPushButton *>()) { b->setAutoDefault(false); b->setDefault(false); }
    m_resume->setEnabled(false);
    m_copy->setEnabled(false);
    v->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_resume, &QPushButton::clicked, this, [this] {
        if (const SavedSession *s = current()) { m_chosen = *s; accept(); }
    });
    connect(m_copy, &QPushButton::clicked, this, [this] {
        const SavedSession *s = current();
        if (!s) return;
        QGuiApplication::clipboard()->setText(commandLine(m_profile, m_kind, m_cwd, s->id));
        emit message(QStringLiteral("Copied the resume command for %1 to the clipboard").arg(s->id));
    });
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] { selectionChanged(); });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *) {
        if (const SavedSession *s = current()) { m_chosen = *s; accept(); }
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &t) { applyFilter(t); });

    populate();
}

void ResumeDialog::populate() {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString note;
    m_sessions = listSessions(m_kind, m_cwd, &note);
    QApplication::restoreOverrideCursor();

    m_list->clear();
    for (int i = 0; i < m_sessions.size(); ++i) {
        const SavedSession &s = m_sessions.at(i);
        auto *item = new QTreeWidgetItem({when(s.lastActive),
                                          s.title.isEmpty() ? QStringLiteral("(untitled)") : s.title,
                                          s.firstPrompt, when(s.started), s.id});
        item->setData(0, kIndexRole, i);
        item->setToolTip(2, s.firstPrompt);
        m_list->addTopLevelItem(item);
    }
    for (int c : {0, 1, 3, 4}) m_list->resizeColumnToContents(c);
    if (m_list->columnWidth(1) > 320) m_list->setColumnWidth(1, 320);
    m_list->clearSelection();
    m_list->setCurrentItem(nullptr);
    m_detail->setText(m_sessions.isEmpty()
        ? note
        : QStringLiteral("%1 conversation(s), most recently active first. Select one to see its "
                         "details.").arg(m_sessions.size()));
}

void ResumeDialog::applyFilter(const QString &text) {
    const QString t = text.trimmed();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        bool show = t.isEmpty();
        for (int c = 1; !show && c <= 4; ++c) show = item->text(c).contains(t, Qt::CaseInsensitive);
        item->setHidden(!show);
        if (!show && item->isSelected()) item->setSelected(false);
    }
}

const SavedSession *ResumeDialog::current() const {
    const QList<QTreeWidgetItem *> sel = m_list->selectedItems();
    if (sel.size() != 1 || sel.first()->isHidden()) return nullptr;
    const int i = sel.first()->data(0, kIndexRole).toInt();
    return (i >= 0 && i < m_sessions.size()) ? &m_sessions.at(i) : nullptr;
}

void ResumeDialog::selectionChanged() {
    const SavedSession *s = current();
    m_resume->setEnabled(s != nullptr);
    m_copy->setEnabled(s != nullptr);
    if (!s) { m_detail->clear(); return; }
    m_detail->setText(QStringLiteral(
        "<b>%1</b><br>First prompt: %2<br>Session %3 &nbsp;·&nbsp; started %4 &nbsp;·&nbsp; "
        "last active %5<br><tt>%6</tt>")
        .arg((s->title.isEmpty() ? QStringLiteral("(untitled)") : s->title).toHtmlEscaped(),
             s->firstPrompt.toHtmlEscaped(), s->id, when(s->started), when(s->lastActive),
             commandLine(m_profile, m_kind, m_cwd, s->id).toHtmlEscaped()));
}
