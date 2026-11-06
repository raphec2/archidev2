// SPDX-License-Identifier: GPL-2.0-or-later
#include "ExplorerPane.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QResizeEvent>
#include <QPushButton>
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

ExplorerPane::ExplorerPane(const QString &startDir, bool allowAboveStart, QWidget *parent)
    : QWidget(parent), m_allowAbove(allowAboveStart) {
    m_start = QFileInfo(startDir).canonicalFilePath();
    if (m_start.isEmpty()) m_start = startDir;
    m_current = m_start;

    m_model = new QFileSystemModel(this);
    m_model->setRootPath(m_start);
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

    // Compact glyph buttons; the wording lives in the tooltips so the toolbar
    // stops dictating a wide minimum for the whole pane.
    m_parentBtn = new QPushButton(QStringLiteral("↑"));
    auto *startBtn = new QPushButton(QStringLiteral("⌂"));
    m_parentBtn->setToolTip(QStringLiteral("Parent directory. Does not change any terminal's cwd."));
    startBtn->setToolTip(QStringLiteral("Back to the starting directory %1.\n"
                                        "Does not change any terminal's cwd.").arg(m_start));

    m_sort = new QComboBox;
    struct { const char *label; const char *full; const char *token; } kSorts[] = {
        {"Name ↑",     "Name A to Z",                 "name_asc"},
        {"Name ↓",     "Name Z to A",                 "name_desc"},
        {"Modified ↓", "Modified, newest first",      "modified_newest"},
        {"Modified ↑", "Modified, oldest first",      "modified_oldest"},
        {"Size ↓",     "Size, largest first",         "size_largest"},
        {"Size ↑",     "Size, smallest first",        "size_smallest"}};
    for (const auto &k : kSorts) {
        m_sort->addItem(QString::fromUtf8(k.label), QString::fromLatin1(k.token));
        m_sort->setItemData(m_sort->count() - 1, QString::fromUtf8(k.full), Qt::ToolTipRole);
    }
    // Without this the closed combo reserves the width of its longest entry.
    m_sort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sort->setMinimumContentsLength(6);
    m_sort->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_sort->setToolTip(QStringLiteral("Sort order. Folders always come first."));

    for (QWidget *w : QList<QWidget *>{m_parentBtn, startBtn, m_sort}) {
        w->setFocusPolicy(Qt::NoFocus);
        w->setMinimumWidth(0);
    }
    for (QPushButton *b : {m_parentBtn, startBtn}) b->setMaximumWidth(34);

    m_path = new QLabel;
    m_path->setObjectName(QStringLiteral("pathLabel"));
    m_path->setMinimumWidth(0);
    // Ignored horizontally: a long path must never widen the pane; it elides.
    m_path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_tree->setMinimumWidth(0);
    m_tree->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    setMinimumWidth(0);

    // Two compact rows: navigation, then area-specific file actions.
    m_navRow = new QHBoxLayout;
    m_navRow->setContentsMargins(0, 0, 0, 0);
    m_navRow->setSpacing(3);
    m_navRow->addWidget(m_parentBtn);
    m_navRow->addWidget(startBtn);
    m_navRow->addWidget(m_sort, 1);

    m_actionRow = new QHBoxLayout;
    m_actionRow->setContentsMargins(0, 0, 0, 0);
    m_actionRow->setSpacing(3);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    v->addLayout(m_navRow);
    v->addLayout(m_actionRow);
    v->addWidget(m_path);
    v->addWidget(m_tree, 1);

    connect(m_parentBtn, &QPushButton::clicked, this, [this] {
        navigateTo(QFileInfo(m_current).absolutePath());
    });
    connect(startBtn, &QPushButton::clicked, this, [this] { navigateTo(m_start); });
    connect(m_sort, &QComboBox::currentTextChanged, this, [this] {
        m_proxy->setMode(ExplorerSortProxy::modeFromToken(m_sort->currentData().toString()));
    });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &i) {
        const QString p = m_model->filePath(m_proxy->mapToSource(i));
        if (QFileInfo(p).isDir()) return;          // the tree expands directories itself
        emit fileActivated(p);
    });

    navigateTo(m_start);
}

QString ExplorerPane::selectedPath() const {
    const QModelIndex i = m_tree->currentIndex();
    if (!i.isValid()) return QString();
    return m_model->filePath(m_proxy->mapToSource(i));
}

QString ExplorerPane::sortToken() const { return m_sort->currentData().toString(); }

void ExplorerPane::setSortToken(const QString &token) {
    const int idx = m_sort->findData(ExplorerSortProxy::tokenForMode(
        ExplorerSortProxy::modeFromToken(token)));
    m_sort->setCurrentIndex(idx < 0 ? 0 : idx);
}

void ExplorerPane::addToolButton(QPushButton *b) {
    b->setFocusPolicy(Qt::NoFocus);
    b->setMinimumWidth(0);
    b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_actionRow->addWidget(b, 1);
}

// Canonical comparison, so a symlink cannot step outside a bounded root.
bool ExplorerPane::withinBoundary(const QString &canonical) const {
    if (m_allowAbove) return true;
    return canonical == m_start || canonical.startsWith(m_start + QLatin1Char('/'));
}

void ExplorerPane::setCurrentDir(const QString &path) { navigateTo(path); }

void ExplorerPane::navigateTo(const QString &path) {
    const QFileInfo fi(path);
    const QString canonical = fi.canonicalFilePath();
    if (canonical.isEmpty() || !fi.isDir() || !fi.isReadable() || !withinBoundary(canonical)) {
        if (m_current != m_start) {
            emit message(QStringLiteral("Cannot show %1 - returning to %2").arg(path, m_start),
                         QStringLiteral("warn"));
            navigateTo(m_start);
        }
        return;
    }
    m_current = canonical;
    m_model->setRootPath(canonical);
    m_tree->setRootIndex(m_proxy->mapFromSource(m_model->index(canonical)));
    refreshToolbar();
}

void ExplorerPane::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    updatePathLabel();
}

// Elide from the left: the tail of a path is the useful part when narrow.
void ExplorerPane::updatePathLabel() {
    const int avail = qMax(20, m_path->width() - 8);
    m_path->setText(m_path->fontMetrics().elidedText(m_current, Qt::ElideLeft, avail));
    m_path->setToolTip(m_current);
}

void ExplorerPane::refreshToolbar() {
    updatePathLabel();
    const bool atRoot = m_current == QLatin1String("/");
    const bool atBoundary = !m_allowAbove && m_current == m_start;
    m_parentBtn->setEnabled(!atRoot && !atBoundary);
}
