// SPDX-License-Identifier: GPL-2.0-or-later
#include "ExplorerPane.h"

#include <QActionGroup>
#include <QMenu>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QResizeEvent>
#include <QItemSelectionModel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

// --------------------------------------------------------------------------
// Sort proxy
// --------------------------------------------------------------------------

void ExplorerSortProxy::setMode(Mode m) {
    m_mode = m;
    invalidate();
    sort(0, Qt::AscendingOrder);        // direction lives in lessThan()
}

ExplorerSortProxy::Mode ExplorerSortProxy::modeFromToken(const QString &token) {
    const QString t = token.trimmed().toLower();
    if (t == QLatin1String("name_desc"))        return NameDesc;
    if (t == QLatin1String("modified_newest"))  return ModifiedNewest;
    if (t == QLatin1String("modified_oldest"))  return ModifiedOldest;
    if (t == QLatin1String("size_largest"))     return SizeLargest;
    if (t == QLatin1String("size_smallest"))    return SizeSmallest;
    return NameAsc;                     // invalid values fall back
}

QString ExplorerSortProxy::tokenForMode(Mode m) {
    switch (m) {
    case NameDesc:       return QStringLiteral("name_desc");
    case ModifiedNewest: return QStringLiteral("modified_newest");
    case ModifiedOldest: return QStringLiteral("modified_oldest");
    case SizeLargest:    return QStringLiteral("size_largest");
    case SizeSmallest:   return QStringLiteral("size_smallest");
    default:             return QStringLiteral("name_asc");
    }
}

bool ExplorerSortProxy::lessThan(const QModelIndex &l, const QModelIndex &r) const {
    const auto *fs = qobject_cast<const QFileSystemModel *>(sourceModel());
    if (!fs) return QSortFilterProxyModel::lessThan(l, r);
    const QFileInfo a = fs->fileInfo(l), b = fs->fileInfo(r);

    if (a.isDir() != b.isDir()) return a.isDir();          // folders always first
    const int byName = QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive);

    switch (m_mode) {
    case NameDesc:       return byName > 0;
    case ModifiedNewest:
        if (a.lastModified() != b.lastModified()) return a.lastModified() > b.lastModified();
        return byName < 0;
    case ModifiedOldest:
        if (a.lastModified() != b.lastModified()) return a.lastModified() < b.lastModified();
        return byName < 0;
    case SizeLargest:
        if (a.size() != b.size()) return a.size() > b.size();
        return byName < 0;
    case SizeSmallest:
        if (a.size() != b.size()) return a.size() < b.size();
        return byName < 0;
    case NameAsc:
    default:             return byName < 0;
    }
}

// --------------------------------------------------------------------------
// Pane
// --------------------------------------------------------------------------

ExplorerPane::ExplorerPane(QWidget *parent) : QWidget(parent) {
    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(true);
    m_proxy = new ExplorerSortProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setDynamicSortFilter(true);
    m_proxy->setMode(ExplorerSortProxy::NameAsc);

    m_tree = new QTreeView;
    m_tree->setModel(m_proxy);
    for (int c = 1; c < m_model->columnCount(); ++c) m_tree->hideColumn(c);
    m_tree->setHeaderHidden(true);
    m_tree->setAnimated(false);
    m_tree->setSortingEnabled(false);      // the proxy owns ordering

    m_parentBtn = new QToolButton;
    m_parentBtn->setText(QStringLiteral("↑"));
    m_parentBtn->setToolTip(QStringLiteral("Parent folder. Does not change any terminal's cwd."));
    m_parentBtn->setFocusPolicy(Qt::NoFocus);

    // Sort is a small menu button rather than a combo box: the second row
    // must keep Open and Send readable in a narrow pane.
    m_sortBtn = new QToolButton;
    m_sortBtn->setText(QStringLiteral("⇅"));
    m_sortBtn->setPopupMode(QToolButton::InstantPopup);
    m_sortBtn->setFocusPolicy(Qt::NoFocus);
    auto *sortMenu = new QMenu(m_sortBtn);
    m_sortGroup = new QActionGroup(this);
    struct { const char *full; const char *token; } kSorts[] = {
        {"Name A to Z",            "name_asc"},
        {"Name Z to A",            "name_desc"},
        {"Modified, newest first", "modified_newest"},
        {"Modified, oldest first", "modified_oldest"},
        {"Size, largest first",    "size_largest"},
        {"Size, smallest first",   "size_smallest"}};
    for (const auto &k : kSorts) {
        QAction *a = sortMenu->addAction(QString::fromUtf8(k.full));
        a->setData(QString::fromLatin1(k.token));
        a->setCheckable(true);
        m_sortGroup->addAction(a);
    }
    m_sortBtn->setMenu(sortMenu);
    connect(m_sortGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        const QString token = a->data().toString();
        if (m_active >= 0) m_roots[m_active].sort = token;
        showSort(token);
    });
    showSort(QStringLiteral("name_asc"));

    m_path = new QLabel;
    m_path->setObjectName(QStringLiteral("pathLabel"));
    m_path->setMinimumWidth(0);
    // Ignored horizontally: a long path must never widen the pane; it elides.
    m_path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_tree->setMinimumWidth(0);
    m_tree->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    setMinimumWidth(0);

    // Row 1: root switch + Parent. Row 2: sort + the caller's file actions.
    m_rootRow = new QHBoxLayout;
    m_rootRow->setContentsMargins(0, 0, 0, 0);
    m_rootRow->setSpacing(0);
    m_rootRow->addStretch(0);
    m_rootRow->addSpacing(4);
    m_rootRow->addWidget(m_parentBtn);
    m_parentBtn->setMinimumWidth(m_parentBtn->sizeHint().width());

    m_actionRow = new QHBoxLayout;
    m_actionRow->setContentsMargins(0, 0, 0, 0);
    m_actionRow->setSpacing(3);
    m_actionRow->addStretch(0);
    m_actionRow->addWidget(m_sortBtn);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    v->addLayout(m_rootRow);
    v->addLayout(m_actionRow);
    v->addWidget(m_path);
    v->addWidget(m_tree, 1);

    connect(m_parentBtn, &QToolButton::clicked, this, [this] {
        if (m_active < 0) return;
        navigateTo(QFileInfo(m_roots.at(m_active).current).absolutePath());
    });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &i) {
        const QString p = m_model->filePath(m_proxy->mapToSource(i));
        if (QFileInfo(p).isDir()) return;          // the tree expands directories itself
        emit fileActivated(p);
    });
}

int ExplorerPane::addRoot(const QString &key, const QString &label, const QString &shortLabel,
                          const QString &startDir, bool allowAboveStart) {
    Root r;
    r.key = key;
    r.label = label;
    r.shortLabel = shortLabel;
    r.start = QFileInfo(startDir).canonicalFilePath();
    if (r.start.isEmpty()) r.start = startDir;
    r.current = r.start;
    r.sort = QStringLiteral("name_asc");
    r.allowAbove = allowAboveStart;
    r.button = new QToolButton;
    r.button->setText(label);
    r.button->setCheckable(true);
    r.button->setFocusPolicy(Qt::NoFocus);
    r.button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    r.button->setMinimumWidth(QFontMetrics(r.button->font()).horizontalAdvance(shortLabel) + 20);
    r.button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    r.button->setToolTip(QStringLiteral("%1 root: %2\n"
                                        "Click to show it again where you left it; click it while it is shown "
                                        "to go back to the root.%3\n"
                                        "Does not change any terminal's cwd.")
                             .arg(label, r.start,
                                  allowAboveStart ? QString()
                                                  : QStringLiteral("\nParent stops at this root.")));
    const int index = m_roots.size();
    connect(r.button, &QToolButton::clicked, this, [this, index] {
        if (index == m_active) {
            m_roots.at(index).button->setChecked(true);   // a click toggles it; keep it shown as active
            navigateTo(m_roots.at(index).start);
            emit message(QStringLiteral("Files: back to the %1 root %2")
                             .arg(m_roots.at(index).label, m_roots.at(index).start), QString());
        } else {
            setActiveRoot(index);
        }
    });
    m_rootRow->insertWidget(index, r.button, 1);
    m_roots << r;
    if (m_active < 0) setActiveRoot(index);
    fitRootLabels();
    return index;
}

void ExplorerPane::setActiveRoot(int root) {
    if (root < 0 || root >= m_roots.size()) return;
    if (m_active >= 0 && m_active != root) m_roots[m_active].selected = selectedPath();
    m_active = root;
    for (int i = 0; i < m_roots.size(); ++i) m_roots[i].button->setChecked(i == root);
    showSort(m_roots.at(root).sort);
    navigateTo(m_roots.at(root).current);
    // Back where it was left: same folder, same selected item.
    const QString sel = m_roots.at(root).selected;
    if (!sel.isEmpty() && sel.startsWith(m_roots.at(root).current + QLatin1Char('/'))) {
        const QModelIndex i = m_proxy->mapFromSource(m_model->index(sel));
        if (i.isValid()) {
            m_tree->setCurrentIndex(i);
            m_tree->scrollTo(i);
        }
    }
    emit rootChanged(root);
}

int ExplorerPane::rootIndex(const QString &key) const {
    for (int i = 0; i < m_roots.size(); ++i) if (m_roots.at(i).key == key) return i;
    return -1;
}
QString ExplorerPane::rootKey(int r) const { return (r >= 0 && r < m_roots.size()) ? m_roots.at(r).key : QString(); }
QString ExplorerPane::rootLabel(int r) const { return (r >= 0 && r < m_roots.size()) ? m_roots.at(r).label : QString(); }
QString ExplorerPane::startDir(int r) const { return (r >= 0 && r < m_roots.size()) ? m_roots.at(r).start : QString(); }
QString ExplorerPane::currentDir(int r) const { return (r >= 0 && r < m_roots.size()) ? m_roots.at(r).current : QString(); }
QString ExplorerPane::sortToken(int r) const { return (r >= 0 && r < m_roots.size()) ? m_roots.at(r).sort : QString(); }

void ExplorerPane::setCurrentDir(int root, const QString &path) {
    if (root < 0 || root >= m_roots.size()) return;
    if (root == m_active) { navigateTo(path); return; }
    m_roots[root].current = path;          // checked against its boundary when shown
}

void ExplorerPane::setSortToken(int root, const QString &token) {
    if (root < 0 || root >= m_roots.size()) return;
    m_roots[root].sort = ExplorerSortProxy::tokenForMode(ExplorerSortProxy::modeFromToken(token));
    if (root == m_active) setActiveRoot(root);
}

QString ExplorerPane::selectedPath() const {
    const QModelIndex i = m_tree->currentIndex();
    if (!i.isValid() || !m_tree->selectionModel()->isSelected(i)) return QString();
    return m_model->filePath(m_proxy->mapToSource(i));
}

void ExplorerPane::addActionWidget(QWidget *w) {
    w->setFocusPolicy(Qt::NoFocus);
    // Never narrower than its text: the pane gets a minimum width instead of
    // eliding "Open" into "O...n".
    w->setMinimumWidth(w->sizeHint().width());
    w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_actionRow->insertWidget(m_actionRow->count() - 2, w, 1);
}

void ExplorerPane::showSort(const QString &token) {
    const auto mode = ExplorerSortProxy::modeFromToken(token);
    const QString canonical = ExplorerSortProxy::tokenForMode(mode);
    for (QAction *a : m_sortGroup->actions())
        if (a->data().toString() == canonical) {
            a->setChecked(true);
            m_sortBtn->setToolTip(QStringLiteral("Sort: %1 (this root). Folders always come first.").arg(a->text()));
        }
    m_proxy->setMode(mode);
}

// Canonical comparison, so a symlink cannot step outside a bounded root.
bool ExplorerPane::usable(const QString &canonical, const Root &r) const {
    const QFileInfo fi(canonical);
    if (canonical.isEmpty() || !fi.isDir() || !fi.isReadable()) return false;
    if (r.allowAbove) return true;
    return canonical == r.start || canonical.startsWith(r.start + QLatin1Char('/'));
}

void ExplorerPane::navigateTo(const QString &path) {
    if (m_active < 0) return;
    Root &r = m_roots[m_active];
    QString canonical = QFileInfo(path).canonicalFilePath();
    if (!usable(canonical, r)) {
        emit message(QStringLiteral("Cannot show %1 in the %2 root - showing %3")
                         .arg(path, r.label, r.start), QStringLiteral("warn"));
        canonical = QFileInfo(r.start).canonicalFilePath();
        if (!usable(canonical, r)) {
            emit message(QStringLiteral("The %1 root %2 is not available").arg(r.label, r.start),
                         QStringLiteral("error"));
            return;
        }
    }
    r.current = canonical;
    m_model->setRootPath(canonical);
    m_tree->setRootIndex(m_proxy->mapFromSource(m_model->index(canonical)));
    m_tree->selectionModel()->clearSelection();
    m_tree->setCurrentIndex(QModelIndex());
    refreshToolbar();
}

void ExplorerPane::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    updatePathLabel();
    fitRootLabels();
}

// Full root names when they fit, short ones when the pane is narrow.
void ExplorerPane::fitRootLabels() {
    if (m_roots.isEmpty()) return;
    const QFontMetrics fm(font());
    int needed = m_parentBtn->sizeHint().width() + 4;
    for (const Root &r : m_roots) needed += fm.horizontalAdvance(r.label) + 18;
    const bool full = width() >= needed;
    for (const Root &r : m_roots) r.button->setText(full ? r.label : r.shortLabel);
}

// Elide from the left: the tail of a path is the useful part when narrow.
void ExplorerPane::updatePathLabel() {
    if (m_active < 0) return;
    const QString cur = m_roots.at(m_active).current;
    const int avail = qMax(20, m_path->width() - 8);
    m_path->setText(m_path->fontMetrics().elidedText(cur, Qt::ElideLeft, avail));
    m_path->setToolTip(cur);
}

void ExplorerPane::refreshToolbar() {
    updatePathLabel();
    const Root &r = m_roots.at(m_active);
    const bool atRoot = r.current == QLatin1String("/");
    const bool atBoundary = !r.allowAbove && r.current == r.start;
    m_parentBtn->setEnabled(!atRoot && !atBoundary);
}
