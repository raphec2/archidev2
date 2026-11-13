// SPDX-License-Identifier: GPL-2.0-or-later
//
// A file explorer pane: stock QFileSystemModel + QTreeView, plus a compact
// toolbar (root switch / Parent / Sort / current path / caller's actions). The only custom model code is
// a narrow sort proxy that keeps folders first - no crawling, no watching, no
// caching of our own.
#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include <QVector>
#include <QWidget>

class QActionGroup;
class QFileSystemModel;
class QHBoxLayout;
class QLabel;
class QToolButton;
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

// One explorer that can show several roots (here: Consultant and Project).
// Each root keeps its own start folder, last location, sort order and
// boundary; switching back returns to where that root was left.
class ExplorerPane : public QWidget {
    Q_OBJECT
public:
    explicit ExplorerPane(QWidget *parent = nullptr);

    // label is shown on the root's switch button; shortLabel when narrow.
    int addRoot(const QString &key, const QString &label, const QString &shortLabel,
                const QString &startDir, bool allowAboveStart);
    void setActiveRoot(int root);              // restores that root's last folder and sort
    int activeRoot() const { return m_active; }
    int rootIndex(const QString &key) const;   // -1 if unknown
    QString rootKey(int root) const;
    QString rootLabel(int root) const;
    QString startDir(int root) const;
    QString currentDir(int root) const;
    QString sortToken(int root) const;
    void setCurrentDir(int root, const QString &path);   // checked when shown; falls back to Start
    void setSortToken(int root, const QString &token);

    QString selectedPath() const;              // empty when nothing is selected
    void addActionWidget(QWidget *w);          // second row, after the sort box

signals:
    void fileActivated(const QString &path);   // double-clicked a regular file
    void rootChanged(int root);
    void message(const QString &text, const QString &level);

protected:
    void resizeEvent(QResizeEvent *e) override;   // re-elide the path, shorten root labels

private:
    struct Root {
        QString key, label, shortLabel, start, current, sort;
        QString selected;                  // restored when the root is shown again
        bool allowAbove = true;
        QToolButton *button = nullptr;
    };
    bool usable(const QString &canonical, const Root &r) const;
    void updatePathLabel();
    void navigateTo(const QString &path);
    void refreshToolbar();
    void fitRootLabels();
    void showSort(const QString &token);

    QVector<Root> m_roots;
    int m_active = -1;
    QFileSystemModel *m_model = nullptr;
    ExplorerSortProxy *m_proxy = nullptr;
    QTreeView *m_tree = nullptr;
    QToolButton *m_parentBtn = nullptr;
    QToolButton *m_sortBtn = nullptr;      // compact: a menu of sort orders
    QActionGroup *m_sortGroup = nullptr;
    QLabel *m_path = nullptr;
    QHBoxLayout *m_rootRow = nullptr;
    QHBoxLayout *m_actionRow = nullptr;
};
