#include "project_sidebar.h"

#include "theme.h"
#include "widgets.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace jrunner {

const char *ProjectSidebar::kSpinFrames[] = { ".", "..", "...", ".." };

namespace {

/// 添加项目对话框(对应 AddProjectSheet)
class AddProjectDialog : public QDialog {
public:
    explicit AddProjectDialog(AppStore *store, QWidget *parent)
        : QDialog(parent)
        , m_store(store)
    {
        setWindowTitle(QStringLiteral("添加项目"));
        setMinimumWidth(520);

        auto *layout = new QVBoxLayout(this);
        layout->setSpacing(14);

        auto *title = new QLabel(QStringLiteral("添加项目"), this);
        QFont tf = title->font();
        tf.setBold(true);
        tf.setPointSize(11);
        title->setFont(tf);
        auto *hint = new QLabel(
            QStringLiteral("输入项目的绝对路径,例如 /home/you/IdeaProjects/community-backend"), this);
        hint->setProperty("tone", "secondary");
        hint->setWordWrap(true);

        auto *row = new QHBoxLayout();
        m_pathEdit = new QLineEdit(this);
        m_pathEdit->setPlaceholderText(QStringLiteral("/path/to/project"));
        m_pathEdit->setProperty("mono", true);
        connect(m_pathEdit, &QLineEdit::returnPressed, this, &AddProjectDialog::accept);
        auto *browse = new QPushButton(QStringLiteral("选择…"), this);
        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString dir = QFileDialog::getExistingDirectory(
                this, QStringLiteral("选择项目目录"), m_pathEdit->text());
            if (!dir.isEmpty())
                m_pathEdit->setText(dir);
        });
        row->addWidget(m_pathEdit, 1);
        row->addWidget(browse);

        auto *buttons = new QHBoxLayout();
        buttons->addStretch(1);
        auto *cancel = new QPushButton(QStringLiteral("取消"), this);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        auto *ok = new QPushButton(QStringLiteral("添加"), this);
        ok->setProperty("role", "primary");
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        buttons->addWidget(cancel);
        buttons->addWidget(ok);

        layout->addWidget(title);
        layout->addWidget(hint);
        layout->addLayout(row);
        layout->addLayout(buttons);
    }

    QString projectPath() const { return m_pathEdit->text().trimmed(); }

private:
    AppStore *m_store;
    QLineEdit *m_pathEdit = nullptr;
};

} // namespace

ProjectSidebar::ProjectSidebar(AppStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("jrSidePanel"));
    setMinimumWidth(220);
    setMaximumWidth(380);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 顶部:搜索 + 刷新
    auto *searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(12, 9, 12, 9);
    searchRow->setSpacing(8);

    auto *searchIcon = new QLabel(QStringLiteral("⌕"), this);
    searchIcon->setStyleSheet(
        QStringLiteral("color: %1; font-size: 13px;").arg(theme::textTertiary().name()));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索项目或模块"));
    m_searchEdit->setFrame(false);
    m_searchEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; font-size: 12px; }"));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { rebuildList(); });

    m_refreshButton = makeIconButton(QStringLiteral("view-refresh"), QStyle::SP_BrowserReload,
                                     QStringLiteral("重新扫描"), this);
    m_refreshButton->setProperty("role", "flat");
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() { m_store->scanProjects(); });

    searchRow->addWidget(searchIcon);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_refreshButton);
    layout->addLayout(searchRow);

    auto *divider1 = new QFrame(this);
    divider1->setFrameShape(QFrame::HLine);
    divider1->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::border().name(QColor::HexArgb)));
    layout->addWidget(divider1);

    // 项目列表
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            m_store->selectProject(path);
    });
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &ProjectSidebar::showContextMenu);
    layout->addWidget(m_list, 1);

    auto *divider2 = new QFrame(this);
    divider2->setFrameShape(QFrame::HLine);
    divider2->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::border().name(QColor::HexArgb)));
    layout->addWidget(divider2);

    // 底部:添加 + 设置
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(10, 8, 10, 8);
    bottomRow->setSpacing(6);
    auto *addButton = new QPushButton(QStringLiteral("＋ 添加项目"), this);
    connect(addButton, &QPushButton::clicked, this, [this]() {
        AddProjectDialog dialog(m_store, this);
        if (dialog.exec() == QDialog::Accepted && !dialog.projectPath().isEmpty())
            m_store->addProject(dialog.projectPath());
    });
    bottomRow->addWidget(addButton);
    bottomRow->addStretch(1);
    auto *settingsButton = new QPushButton(QStringLiteral("⚙︎"), this);
    settingsButton->setObjectName(QStringLiteral("jrSettingsButton"));
    settingsButton->setFlat(true);
    settingsButton->setFixedSize(30, 30);
    settingsButton->setToolTip(QStringLiteral("设置"));
    settingsButton->setAccessibleName(QStringLiteral("设置"));
    settingsButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 14px; padding: 0; border: none; background: transparent; }"
        "QPushButton:hover { color: %2; }")
        .arg(theme::textSecondary().name(), theme::primary().name()));
    connect(settingsButton, &QPushButton::clicked, this, &ProjectSidebar::openSettingsRequested);
    bottomRow->addWidget(settingsButton);
    layout->addLayout(bottomRow);

    m_spinTimer = new QTimer(this);
    m_spinTimer->setInterval(120);
    connect(m_spinTimer, &QTimer::timeout, this, &ProjectSidebar::refreshSpinState);

    connect(m_store, &AppStore::projectsChanged, this, &ProjectSidebar::rebuildList);
    connect(m_store, &AppStore::selectionChanged, this, &ProjectSidebar::rebuildList);
    connect(m_store, &AppStore::taskStateChanged, this, &ProjectSidebar::rebuildList);
    connect(m_store, &AppStore::isScanningChanged, this, [this]() {
        const bool scanning = m_store->isScanning();
        m_refreshButton->setEnabled(!scanning);
        const QString label = scanning ? QStringLiteral("正在扫描项目") : QStringLiteral("重新扫描");
        m_refreshButton->setToolTip(label);
        m_refreshButton->setAccessibleName(label);
        if (scanning) {
            m_refreshButton->setIcon(QIcon());
            m_spinFrame = 0;
            m_refreshButton->setText(QString::fromLatin1(kSpinFrames[0]));
            m_spinTimer->start();
        } else {
            m_spinTimer->stop();
            m_refreshButton->setText(QString());
            m_refreshButton->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh"),
                m_refreshButton->style()->standardIcon(QStyle::SP_BrowserReload)));
        }
    });

    rebuildList();
}

void ProjectSidebar::refreshSpinState()
{
    m_spinFrame = (m_spinFrame + 1) % 4;
    m_refreshButton->setText(QString::fromLatin1(kSpinFrames[m_spinFrame]));
}

void ProjectSidebar::refreshTheme()
{
    if (auto *settings = findChild<QPushButton *>(QStringLiteral("jrSettingsButton")))
        settings->setStyleSheet(QStringLiteral(
            "QPushButton { color: %1; font-size: 14px; padding: 0; border: none; background: transparent; }"
            "QPushButton:hover { color: %2; }")
            .arg(theme::textSecondary().name(), theme::primary().name()));
    rebuildList();
}

void ProjectSidebar::rebuildList()
{
    const QString selected = m_store->selectedProjectPath();
    const QString query = m_searchEdit->text().trimmed().toLower();

    // 重名项目需要显示父目录加以区分
    QHash<QString, int> counts;
    for (const Project &p : m_store->projects())
        counts[p.name] += 1;

    QList<Project> filtered;
    for (const Project &p : m_store->projects()) {
        if (query.isEmpty()) {
            filtered.append(p);
            continue;
        }
        if (p.name.toLower().contains(query) || p.path.toLower().contains(query)) {
            filtered.append(p);
            continue;
        }
        const bool moduleMatch = std::any_of(p.modules.begin(), p.modules.end(),
                                             [&](const Module &m) {
                                                 return m.name.toLower().contains(query);
                                             });
        if (moduleMatch)
            filtered.append(p);
    }

    m_list->clear();
    if (filtered.isEmpty()) {
        auto *empty = new EmptyStateView(
            m_store->projects().isEmpty() ? QStringLiteral("empty") : QStringLiteral("⌕"),
            m_store->projects().isEmpty() ? QStringLiteral("未发现项目")
                                          : QStringLiteral("无匹配结果"),
            m_store->projects().isEmpty() ? QStringLiteral("点击左下角「添加项目」加入目录")
                                          : QStringLiteral("换个关键词试试"),
            this);
        auto *item = new QListWidgetItem(m_list);
        item->setFlags(Qt::NoItemFlags);
        m_list->addItem(item);
        m_list->setItemWidget(item, empty);
        for (QLabel *l : empty->findChildren<QLabel *>())
            l->ensurePolished();
        empty->ensurePolished();
        m_list->item(0)->setSizeHint(empty->sizeHint());
        return;
    }

    for (const Project &p : filtered) {
        const bool isSelected = p.path == selected;
        const bool running = m_store->isProjectRunning(p.path);
        const bool showParent = counts.value(p.name) > 1;

        auto *row = buildRowWidget(p, isSelected, running, showParent);
        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, p.path);
        m_list->addItem(item);
        m_list->setItemWidget(item, row);
        // 样式表字体需先 polish,否则 sizeHint 用默认字体算高,导致行错位/裁切
        for (QLabel *l : row->findChildren<QLabel *>())
            l->ensurePolished();
        row->ensurePolished();
        item->setSizeHint(row->sizeHint());
    }
}

QWidget *ProjectSidebar::buildRowWidget(const Project &project, bool selected,
                                        bool running, bool showParent)
{
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(8);

    auto *icon = new QLabel(project.tool == BuildTool::Gradle
                                ? QStringLiteral("◧")
                                : QStringLiteral("⬡"), row);
    icon->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;")
                            .arg(selected ? theme::primary().name()
                                          : theme::textTertiary().name()));
    icon->setFixedWidth(16);
    layout->addWidget(icon);

    QLabel *dot = nullptr;
    if (running) {
        dot = new QLabel(QStringLiteral("●"), row);
        dot->setStyleSheet(
            QStringLiteral("color: %1; font-size: 8px;").arg(theme::success().name()));
        dot->setFixedWidth(7);
        layout->addWidget(dot);
    }

    // 副标题拼成一行,避免多个标签在窄栏里被挤散
    QStringList parts;
    if (showParent) {
        const QString parent = QFileInfo(project.path).absolutePath();
        const QString last = parent.section(QLatin1Char('/'), -1);
        if (!last.isEmpty())
            parts.append(last);
    }
    parts.append(projectLanguageDisplayName(project.language));
    if (project.hasRequiredJava)
        parts.append(QStringLiteral("JDK %1").arg(project.requiredJava));
    parts.append(QStringLiteral("%1 模块").arg(project.modules.size()));

    auto *texts = new QVBoxLayout();
    texts->setSpacing(2);
    auto *name = new QLabel(project.name, row);
    name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    name->setToolTip(project.name + QLatin1Char('\n') + project.path);
    // 用 QFont 而非样式表字号,保证 sizeHint 高度确定(item 限高时不会裁切文字)
    QFont nameFont = name->font();
    nameFont.setPointSize(10);
    nameFont.setBold(selected);
    name->setFont(nameFont);
    name->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(theme::textPrimary().name()));
    // 全局样式表激活时,QStyleSheetStyle 会按错误度量压缩自带 QFont 的标签,
    // 必须用真实字体行高固定高度,否则中文 glyph 顶部被裁切
    const QFontMetrics nameFm(nameFont);
    const int lineH = qMax(nameFm.height(), nameFm.boundingRect(project.name).height());
    name->setFixedHeight(lineH);
    // 图标/运行点/分支与标题行对齐(而非整行垂直居中),避免视觉错位
    icon->setFixedHeight(lineH);
    layout->setAlignment(icon, Qt::AlignTop);
    if (dot) {
        dot->setFixedHeight(lineH);
        layout->setAlignment(dot, Qt::AlignTop);
    }
    auto *subtitle = new QLabel(parts.join(QStringLiteral(" · ")), row);
    QFont subFont = subtitle->font();
    subFont.setPointSize(8);
    subtitle->setFont(subFont);
    const QFontMetrics subFm(subFont);
    subtitle->setFixedHeight(qMax(subFm.height(), subFm.boundingRect(subtitle->text()).height()));
    subtitle->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(theme::textTertiary().name()));
    texts->addWidget(name);
    texts->addWidget(subtitle);
    layout->addLayout(texts, 1);

    // 行尾显示当前分支,一眼看出各项目在哪个分支
    const GitStatus status = m_store->git()->status(project.path);
    if (status.isRepo()) {
        auto *branch = new QLabel(QStringLiteral("⎇ %1").arg(status.branch), row);
        branch->setToolTip(status.tooltip());
        QFont bf = branch->font();
        bf.setFamily(theme::monospaceFontFamily());
        bf.setPointSize(8);
        branch->setFont(bf);
        branch->setStyleSheet(QStringLiteral("color: %1; font-size: 9px; border: none;")
                                  .arg(theme::textTertiary().name()));
        branch->setFixedHeight(lineH);
        layout->addWidget(branch, 0, Qt::AlignTop);
    }

    if (selected) {
        // 背景只作用于行本身(#id),避免半透明色叠加到子标签形成色带
        row->setObjectName(QStringLiteral("jrProjectRow"));
        row->setStyleSheet(QStringLiteral(
            "QWidget#jrProjectRow { background: %1; border-radius: 6px; } QLabel { border: none; }")
            .arg(Badge::hexWithAlpha(theme::primary(), 0.16)));
    }
    return row;
}

void ProjectSidebar::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty())
        return;

    QMenu menu(this);
    QAction *openAction = menu.addAction(QStringLiteral("在文件管理器中打开"));
    QAction *rescanAction = menu.addAction(QStringLiteral("重新识别该项目"));
    QAction *renameAction = menu.addAction(QStringLiteral("重命名项目"));
    menu.addSeparator();
    QAction *removeAction = menu.addAction(QStringLiteral("移除项目"));

    QAction *chosen = menu.exec(m_list->mapToGlobal(pos));
    if (chosen == openAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else if (chosen == rescanAction) {
        m_store->rescanProject(path);
    } else if (chosen == renameAction) {
        const auto project = m_store->projectAt(path);
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("重命名项目"), QStringLiteral("显示名称(留空恢复目录名):"),
            QLineEdit::Normal, project ? project->name : QString(), &ok);
        if (ok)
            m_store->renameProject(path, name);
    } else if (chosen == removeAction) {
        m_store->removeProject(path);
    }
}

} // namespace jrunner
