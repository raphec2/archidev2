// SPDX-License-Identifier: GPL-2.0-or-later
//
// A file explorer pane: stock QFileSystemModel + QTreeView, plus a compact
// toolbar (Parent / Start / Sort / current path). The only custom model code is
// a narrow sort proxy that keeps folders first - no crawling, no watching, no
// caching of our own.
#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include <QWidget>

class QComboBox;
class QFileSystemModel;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QTreeView;

// Folders before files in every mode, with name as the stable tiebreaker.
// Direction is encoded here rather than via sortOrder so that grouping never
// inverts along with the ordering.
class ExplorerSortProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    enum Mode { NameAsc, NameDesc, ModifiedNewest, ModifiedOldest, SizeLargest, SizeSmallest };

    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setMode(Mode m);
    Mode mode() const { return m_mode; }

    static Mode modeFromToken(const QString &token);   // invalid => NameAsc
    static QString tokenForMode(Mode m);

protected:
    bool lessThan(const QModelIndex &l, const QModelIndex &r) const override;

private:
    Mode m_mode = NameAsc;
};

class ExplorerPane : public QWidget {
    Q_OBJECT
public:
    ExplorerPane(const QString &startDir, bool allowAboveStart, QWidget *parent = nullptr);

    QString startDir() const { return m_start; }
    QString currentDir() const { return m_current; }
    QString selectedPath() const;          // empty when nothing is selected
    QString sortToken() const;

    void setCurrentDir(const QString &path);   // falls back to Start when unusable
    void setSortToken(const QString &token);
    void addToolButton(QPushButton *b);        // area-specific actions (second row)

signals:
    void fileActivated(const QString &path);   // double-clicked a regular file
    void message(const QString &text, const QString &level);

protected:
    void resizeEvent(QResizeEvent *e) override;   // re-elide the path label

private:
    bool withinBoundary(const QString &canonical) const;
    void updatePathLabel();
    void navigateTo(const QString &path);
    void refreshToolbar();

    QString m_start;
    QString m_current;
    bool m_allowAbove = true;
    QFileSystemModel *m_model = nullptr;
    ExplorerSortProxy *m_proxy = nullptr;
    QTreeView *m_tree = nullptr;
    QPushButton *m_parentBtn = nullptr;
    QComboBox *m_sort = nullptr;
    QLabel *m_path = nullptr;
    QHBoxLayout *m_navRow = nullptr;
    QHBoxLayout *m_actionRow = nullptr;
};
