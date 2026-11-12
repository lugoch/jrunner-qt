#include "module_panel.h"

#include "theme.h"
#include "widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QResizeEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace jrunner {

namespace {

QString commandLineForArguments(const QStringList &arguments)
{
    QStringList encoded;
    for (const QString &argument : arguments) {
        QString text = argument;
        // QProcess uses triple quotes for literal quotes, not shell backslash escapes.
        text.replace(QLatin1Char('"'), QStringLiteral("\"\"\""));
        if (argument.isEmpty() || std::any_of(argument.begin(), argument.end(),
                                             [](QChar c) { return c.isSpace(); }))
            text = QLatin1Char('"') + text + QLatin1Char('"');
        encoded.append(text);
    }
    return encoded.join(QLatin1Char(' '));
}

/// 对话框通用 保存/取消 按钮盒
QDialogButtonBox *makeSaveCancelButtons(QDialog *dialog)
{
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", "primary");
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

class ElidedPathLabel : public QLabel {
public:
    explicit ElidedPathLabel(const QString &path, QWidget *parent)
        : QLabel(parent), m_path(path)
    {
        setTextFormat(Qt::PlainText);
        setToolTip(path);
        setAccessibleName(path);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
        setText(path);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        setText(fontMetrics().elidedText(m_path, Qt::ElideMiddle, contentsRect().width()));
    }

private:
    QString m_path;
};

/// 可点击的行容器(左键选中 / Ctrl+左键勾选 / 右键菜单),模块行与分组标题共用
class ModuleRowWidget : public QWidget {
public:
    explicit ModuleRowWidget(QWidget *parent) : QWidget(parent) { setCursor(Qt::PointingHandCursor); }
    std::function<void()> onClicked;
    std::function<void()> onCtrlClicked;
    std::function<void(QPoint)> onContextMenu;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton) {
            if (onContextMenu) onContextMenu(event->globalPosition().toPoint());
        } else if (event->button() == Qt::LeftButton) {
            if ((event->modifiers() & Qt::ControlModifier) && onCtrlClicked) onCtrlClicked();
            else if (onClicked) onClicked();
        }
        QWidget::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        // 快速连点时第二次 press 会被 Qt 合成为 DblClick,仍需响应选中
        if (event->button() == Qt::LeftButton) {
            if ((event->modifiers() & Qt::ControlModifier) && onCtrlClicked) onCtrlClicked();
            else if (onClicked) onClicked();
        }
        QWidget::mouseDoubleClickEvent(event);
    }
};

/// 模块配置对话框(对应 ModuleConfigSheet):profile / JDK / 环境变量 / 额外参数
class ModuleConfigDialog : public QDialog {
public:
    explicit ModuleConfigDialog(AppStore *store, const Project &project,
                                const Module &module, QWidget *parent)
        : QDialog(parent), m_store(store), m_project(project), m_module(module)
    {
        setWindowTitle(QStringLiteral("模块配置 · %1").arg(module.name));
        setMinimumWidth(460);
        const ModuleConfig cfg = store->config().config(project.path, module.path);
        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout();
        form->setSpacing(10);
        m_profileEdit = new QLineEdit(cfg.profile, this);
        m_profileEdit->setPlaceholderText(module.hasDefaultProfile
            ? QStringLiteral("跟随项目配置(%1)").arg(module.defaultProfile)
            : QStringLiteral("跟随项目配置"));
        form->addRow(QStringLiteral("Spring Profile"), m_profileEdit);
        m_jdkPicker = new QComboBox(this);
        m_jdkPicker->addItem(QStringLiteral("跟随项目"), 0);
        QSet<int> seenVersions;
        for (const JDK &jdk : store->jdks()) {
            if (seenVersions.contains(jdk.version)) continue;
            seenVersions.insert(jdk.version);
            m_jdkPicker->addItem(QStringLiteral("JDK %1 (%2)").arg(jdk.version).arg(jdk.vendor), jdk.version);
        }
        const int idx = m_jdkPicker->findData(cfg.javaVersion);
        m_jdkPicker->setCurrentIndex(idx >= 0 ? idx : 0);
        form->addRow(QStringLiteral("JDK 版本"), m_jdkPicker);
        m_envEdit = new QPlainTextEdit(this);
        m_envEdit->setPlaceholderText(QStringLiteral("KEY=VALUE,每行一个"));
        m_envEdit->setFont(QFont(theme::monospaceFontFamily())); m_envEdit->setFixedHeight(96);
        QStringList envLines;
        for (auto it = cfg.envVars.constBegin(); it != cfg.envVars.constEnd(); ++it)
            envLines.append(it.key() + QLatin1Char('=') + it.value());
        std::sort(envLines.begin(), envLines.end());
        m_envEdit->setPlainText(envLines.join(QLatin1Char('\n')));
        form->addRow(QStringLiteral("环境变量"), m_envEdit);
        m_originalArgs = cfg.extraArgs;
        m_argsEdit = new QLineEdit(commandLineForArguments(m_originalArgs), this);
        m_argsEdit->setPlaceholderText(QStringLiteral("如 --refresh-dependencies -DskipTests"));
        form->addRow(QStringLiteral("额外参数"), m_argsEdit);
        layout->addLayout(form);
        layout->addWidget(makeSaveCancelButtons(this));
    }

    void accept() override
    {
        ModuleConfig cfg;
        cfg.profile = m_profileEdit->text().trimmed();
        cfg.javaVersion = m_jdkPicker->currentData().toInt();
        const QStringList lines = m_envEdit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0) continue;
            cfg.envVars.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
        }
        // splitCommand cannot represent empty arguments; keep untouched lists losslessly.
        cfg.extraArgs = m_argsEdit->text() == commandLineForArguments(m_originalArgs)
            ? m_originalArgs : QProcess::splitCommand(m_argsEdit->text());
        m_store->updateModuleConfig(m_project.path, m_module.path, cfg);
        QDialog::accept();
    }

private:
    AppStore *m_store;
    Project m_project;
    Module m_module;
    QLineEdit *m_profileEdit = nullptr;
    QComboBox *m_jdkPicker = nullptr;
    QPlainTextEdit *m_envEdit = nullptr;
    QLineEdit *m_argsEdit = nullptr;
    QStringList m_originalArgs;
};

/// 分组编辑对话框:名称 + 勾选成员;originalName 为空表示新建
class GroupEditDialog : public QDialog {
public:
    GroupEditDialog(AppStore *store, const Project &project, const QString &originalName,
                    const QStringList &members, QWidget *parent)
        : QDialog(parent), m_store(store), m_project(project), m_originalName(originalName)
    {
        setWindowTitle(originalName.isEmpty() ? QStringLiteral("新建分组") : QStringLiteral("编辑分组 · %1").arg(originalName));
        setMinimumWidth(380);
        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout();
        m_nameEdit = new QLineEdit(originalName, this);
        m_nameEdit->setPlaceholderText(QStringLiteral("分组名称"));
        form->addRow(QStringLiteral("名称"), m_nameEdit);
        layout->addLayout(form);
        m_memberList = new QListWidget(this);
        m_memberList->setMinimumHeight(220);
        for (const Module &m : project.modules) {
            auto *item = new QListWidgetItem(m.displayPath(), m_memberList);
            item->setData(Qt::UserRole, m.path);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(members.contains(m.path) ? Qt::Checked : Qt::Unchecked);
        }
        layout->addWidget(m_memberList);
        layout->addWidget(makeSaveCancelButtons(this));
    }

    void accept() override
    {
        const QString name = m_nameEdit->text().trimmed();
        if (name.isEmpty()) return;
        QStringList members;
        for (int i = 0; i < m_memberList->count(); ++i)
            if (m_memberList->item(i)->checkState() == Qt::Checked)
                members.append(m_memberList->item(i)->data(Qt::UserRole).toString());
        QList<ModuleGroup> groups = m_store->projectGroups(m_project.path);
        ModuleGroup *original = nullptr, *target = nullptr;
        for (ModuleGroup &g : groups) {
            if (!m_originalName.isEmpty() && g.name == m_originalName) original = &g;
            if (g.name == name) target = &g;
        }
        auto merge = [&members](ModuleGroup &g) { for (const QString &p : members) if (!g.modulePaths.contains(p)) g.modulePaths.append(p); };
        if (original && target && original != target) { merge(*target); groups.removeOne(*original); }
        else if (original) { original->name = name; original->modulePaths = members; }
        else if (target) merge(*target);
        else groups.append(ModuleGroup{name, members});
        m_store->updateGroups(m_project.path, groups);
        QDialog::accept();
    }

private:
    AppStore *m_store;
    Project m_project;
    QString m_originalName;
    QLineEdit *m_nameEdit = nullptr;
    QListWidget *m_memberList = nullptr;
};

/// 分组管理对话框:列出全部分组,支持新建 / 编辑 / 删除
class GroupManagerDialog : public QDialog {
public:
    GroupManagerDialog(AppStore *store, const Project &project, QWidget *parent)
        : QDialog(parent), m_store(store), m_project(project)
    {
        setWindowTitle(QStringLiteral("管理分组"));
        setMinimumWidth(380);
        auto *layout = new QVBoxLayout(this);
        m_rows = new QVBoxLayout();
        m_rows->setSpacing(6);
        layout->addLayout(m_rows);
        auto *newButton = new QPushButton(QStringLiteral("新建分组"), this);
        newButton->setProperty("role", "primary");
        connect(newButton, &QPushButton::clicked, this, [this]() { openEditor(QString(), QStringList()); });
        auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
        connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
        auto *buttonRow = new QHBoxLayout();
        buttonRow->addWidget(newButton); buttonRow->addStretch(1); buttonRow->addWidget(closeButton);
        layout->addLayout(buttonRow);
        refresh();
    }

private:
    void openEditor(const QString &name, const QStringList &members)
    {
        GroupEditDialog dialog(m_store, m_project, name, members, this);
        if (dialog.exec() == QDialog::Accepted) refresh();
    }
    void refresh()
    {
        while (m_rows->count() > 0) {
            QLayoutItem *item = m_rows->takeAt(0);
            if (QWidget *w = item->widget()) { w->hide(); w->deleteLater(); }
            delete item;
        }
        for (const ModuleGroup &g : m_store->projectGroups(m_project.path)) {
            auto *rowWidget = new QWidget(this);
            auto *row = new QHBoxLayout(rowWidget);
            row->setContentsMargins(0, 0, 0, 0); row->setSpacing(6);
            row->addWidget(new QLabel(g.name, rowWidget));
            row->addWidget(new Badge(QString::number(g.modulePaths.size()), theme::primary(), false, rowWidget));
            row->addStretch(1);
            auto *editButton = new QPushButton(QStringLiteral("编辑"), rowWidget);
            connect(editButton, &QPushButton::clicked, this, [this, g]() { openEditor(g.name, g.modulePaths); });
            auto *deleteButton = new QPushButton(QStringLiteral("删除"), rowWidget);
            deleteButton->setProperty("role", "danger");
            connect(deleteButton, &QPushButton::clicked, this, [this, g]() {
                QList<ModuleGroup> gs = m_store->projectGroups(m_project.path);
                gs.erase(std::remove_if(gs.begin(), gs.end(), [&g](const ModuleGroup &x) { return x.name == g.name; }), gs.end());
                m_store->updateGroups(m_project.path, gs);
                refresh();
            });
            row->addWidget(editButton); row->addWidget(deleteButton); m_rows->addWidget(rowWidget);
        }
    }
    AppStore *m_store;
    Project m_project;
    QVBoxLayout *m_rows = nullptr;
};

} // namespace

ModulePanel::ModulePanel(AppStore *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    setObjectName(QStringLiteral("jrMiddlePanel")); setMinimumWidth(260);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    m_header = new QWidget(this);
    layout->addWidget(m_header);
    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("color: %1;").arg(theme::border().name(QColor::HexArgb)));
    layout->addWidget(divider);
    m_listScroll = new QScrollArea(this);
    m_listScroll->setWidgetResizable(true); m_listScroll->setFrameShape(QFrame::NoFrame);
    m_listContainer = new QWidget(m_listScroll);
    m_listContainer->setObjectName(QStringLiteral("jrMiddlePanel"));
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(14, 12, 14, 12); m_listLayout->setSpacing(6);
    m_listLayout->addStretch(1); m_listScroll->setWidget(m_listContainer);
    layout->addWidget(m_listScroll, 1);
    connect(m_store, &AppStore::selectionChanged, this, [this]() { rebuild(); });
    connect(m_store, &AppStore::projectsChanged, this, [this]() { rebuild(); });
    connect(m_store, &AppStore::taskStateChanged, this, [this](const QString &) { rebuild(); });
    connect(m_store, &AppStore::configChanged, this, [this]() { rebuild(); });
    connect(m_store, &AppStore::groupsChanged, this, [this]() { rebuild(); });
    connect(m_store, &AppStore::jdksChanged, this, [this]() { rebuildHeader(); });
    rebuild();
}

void ModulePanel::refreshTheme()
{
    rebuild();
}

void ModulePanel::rebuild()
{
    if (m_rebuilding) return;
    m_rebuilding = true;
    rebuildHeader();
    rebuildModuleList();
    m_rebuilding = false;
}

void ModulePanel::rebuildHeader()
{
    // 重建头部(选择变化时整个替换,保持代码简单)
    const auto project = m_store->selectedProject();
    const auto module = m_store->selectedModule();
    m_header->hide();
    m_header->deleteLater();
    m_modulePicker = nullptr;
    m_profilePicker = nullptr;
    m_actionButton = nullptr;
    m_buildButton = nullptr;
    m_configButton = nullptr;
    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("jrMiddlePanel"));
    auto swapHeader = [this]() {
        if (auto *v = qobject_cast<QVBoxLayout *>(layout())) {
            if (v->count() > 0) {
                QLayoutItem *old = v->takeAt(0);
                delete old;      // 旧 widget 已标记 deleteLater,这里只删布局项
            }
            v->insertWidget(0, m_header);
        }
    };
    if (!project || !module) {
        auto *empty = new EmptyStateView(QStringLiteral("▤"), QStringLiteral("未选择项目"),
            QStringLiteral("从左侧选择一个项目开始\n支持 Gradle / Maven 的 Java、Kotlin 项目"), m_header);
        auto *l = new QVBoxLayout(m_header);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(empty);
        swapHeader();
        return;
    }
    auto *headerLayout = new QVBoxLayout(m_header);
    headerLayout->setContentsMargins(12, 13, 12, 13); headerLayout->setSpacing(10);
    auto *texts = new QVBoxLayout(); texts->setSpacing(3);
    auto *name = new QLabel(project->name, m_header);
    name->setTextFormat(Qt::PlainText);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    name->setWordWrap(true); name->setToolTip(project->name);
    QFont nameFont = name->font();
    nameFont.setPointSize(12); nameFont.setBold(true);
    name->setFont(nameFont);
    auto *path = new ElidedPathLabel(project->path, m_header);
    path->setFont(QFont(theme::monospaceFontFamily()));
    path->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    texts->addWidget(name); texts->addWidget(path);
    headerLayout->addLayout(texts);
    auto *selectorRow = new QHBoxLayout(); selectorRow->setSpacing(8);
    m_modulePicker = new QComboBox(m_header);
    m_modulePicker->setAccessibleName(QStringLiteral("模块"));
    m_modulePicker->setToolTip(module->displayPath());
    m_modulePicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_modulePicker->setMinimumContentsLength(6); m_modulePicker->setMinimumWidth(0);
    m_modulePicker->setMinimumHeight(30); m_modulePicker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (const Module &m : project->modules) {
        const bool running = m_store->isRunning(project->path, m.path);
        const QIcon icon = running
            ? QIcon::fromTheme(QStringLiteral("media-playback-start"), style()->standardIcon(QStyle::SP_MediaPlay)) : QIcon();
        m_modulePicker->addItem(icon, m.displayPath(), m.path);
        m_modulePicker->setItemData(m_modulePicker->count() - 1, m.displayPath(), Qt::ToolTipRole);
    }
    const int moduleIdx = m_modulePicker->findData(module->path);
    if (moduleIdx >= 0) m_modulePicker->setCurrentIndex(moduleIdx);
    connect(m_modulePicker, &QComboBox::activated, this, [this, picker = m_modulePicker](int index) {
        const QString path = picker->itemData(index).toString();
        if (!path.isEmpty()) m_store->selectModule(path);
    });
    selectorRow->addWidget(m_modulePicker, 1);
    if (module->isSpringBoot) {
        m_profilePicker = new QComboBox(m_header);
        m_profilePicker->setAccessibleName(QStringLiteral("Spring Profile"));
        m_profilePicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_profilePicker->setMinimumContentsLength(6); m_profilePicker->setMinimumWidth(0);
        m_profilePicker->setMinimumHeight(30); m_profilePicker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const QString current = m_store->effectiveProfile(*project, *module);
        m_profilePicker->setToolTip(QStringLiteral("Spring Profile: %1").arg(current));
        const bool isOverridden = !m_store->config().config(project->path, module->path).profile.isEmpty();
        const QString followLabel = QStringLiteral("跟随项目配置")
            + (module->hasDefaultProfile ? QStringLiteral("(%1)").arg(module->defaultProfile) : QString());
        m_profilePicker->addItem(followLabel, QString());
        for (const QString &p : module->availableProfiles) m_profilePicker->addItem(p, p);
        if (isOverridden && !current.isEmpty()) {
            int cur = m_profilePicker->findData(current);
            if (cur < 0) {
                m_profilePicker->addItem(current, current); cur = m_profilePicker->count() - 1;
            }
            m_profilePicker->setCurrentIndex(cur);
        }
        for (int i = 0; i < m_profilePicker->count(); ++i)
            m_profilePicker->setItemData(i, m_profilePicker->itemText(i), Qt::ToolTipRole);
        if (isOverridden)
            m_profilePicker->setStyleSheet(QStringLiteral("QComboBox { color: %1; font-weight: 600; }").arg(theme::success().name()));
        connect(m_profilePicker, &QComboBox::activated, this,
                [this, picker = m_profilePicker, project_ = *project, module_ = *module](int index) {
                    m_store->setProfile(picker->itemData(index).toString(), project_, module_);
                });
        selectorRow->addWidget(m_profilePicker, 1);
    }
    headerLayout->addLayout(selectorRow);
    auto *commandRow = new QHBoxLayout(); commandRow->setSpacing(6);
    const bool running = m_store->isRunning(project->path, module->path);
    if (running) {
        m_actionButton = new QPushButton(QStringLiteral("停止"), m_header);
        m_actionButton->setProperty("role", "danger");
        m_actionButton->setToolTip(QStringLiteral("停止服务 (Ctrl+.)"));
        connect(m_actionButton, &QPushButton::clicked, this, [this, module_ = *module]() { m_store->stopModule(module_); });
    } else {
        m_actionButton = new QPushButton(QStringLiteral("启动"), m_header);
        m_actionButton->setProperty("role", "primary");
        m_actionButton->setToolTip(QStringLiteral("启动服务 (Ctrl+Shift+R)"));
        connect(m_actionButton, &QPushButton::clicked, this, [this, module_ = *module]() { m_store->runModule(module_); });
        m_buildButton = new QPushButton(QStringLiteral("打包"), m_header);
        m_buildButton->setMinimumHeight(30);
        m_buildButton->setToolTip(module->hasJib ? QStringLiteral("打包推送镜像 (Ctrl+B)") : QStringLiteral("构建项目 (Ctrl+B)"));
        connect(m_buildButton, &QPushButton::clicked, this, [this, module_ = *module]() { m_store->buildModule(module_); });
    }
    m_actionButton->setMinimumHeight(30);
    m_configButton = makeIconButton(QStringLiteral("configure"), QStyle::SP_FileDialogDetailedView, QStringLiteral("模块配置"), m_header);
    connect(m_configButton, &QPushButton::clicked, this,
            [this, project_ = *project, module_ = *module]() { showModuleConfig(project_, module_); });
    auto *rescanButton = makeIconButton(QStringLiteral("view-refresh"), QStyle::SP_BrowserReload, QStringLiteral("重新扫描项目"), m_header);
    connect(rescanButton, &QPushButton::clicked, this, [this, path_ = project->path]() { m_store->rescanProject(path_); });
    auto *openButton = makeIconButton(QStringLiteral("folder-open"), QStyle::SP_DirOpenIcon, QStringLiteral("在文件管理器中打开"), m_header);
    connect(openButton, &QPushButton::clicked, this, [path_ = project->path]() { QDesktopServices::openUrl(QUrl::fromLocalFile(path_)); });
    commandRow->addWidget(m_actionButton);
    if (m_buildButton) commandRow->addWidget(m_buildButton);
    commandRow->addWidget(m_configButton); commandRow->addStretch(1);
    commandRow->addWidget(rescanButton); commandRow->addWidget(openButton);
    headerLayout->addLayout(commandRow);
    swapHeader();      // 替换旧头部(保持布局位置)
}

void ModulePanel::rebuildModuleList()
{
    const auto project = m_store->selectedProject();
    // 清空旧列表(保留结尾 stretch)
    while (m_listLayout->count() > 1) {
        QLayoutItem *item = m_listLayout->takeAt(0);
        QWidget *widget = item->widget();
        widget->hide();
        // Selection signals can rebuild the list inside the row's mouse event.
        widget->deleteLater();
        delete item;
    }
    if (!project) { m_checkedModules.clear(); return; }
    // 勾选集合只保留当前项目仍存在的模块(切换项目 / 重扫后自动清理)
    QSet<QString> validPaths;
    for (const Module &m : project->modules) validPaths.insert(m.path);
    m_checkedModules.intersect(validPaths);
    // 标题行:模块 (N) + 管理分组
    auto *titleRow = new QWidget(m_listContainer);
    auto *titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0); titleLayout->setSpacing(6);
    auto *title = new QLabel(QStringLiteral("模块 (%1)").arg(project->modules.size()), titleRow);
    QFont titleFont = title->font();
    titleFont.setPointSize(9); titleFont.setBold(true);
    title->setFont(titleFont);
    title->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textSecondary().name()));
    auto *manageButton = new QPushButton(QStringLiteral("管理分组"), titleRow);
    manageButton->setProperty("role", "flat");
    manageButton->setStyleSheet(QStringLiteral("font-size: 11px; padding: 2px 6px;"));
    manageButton->setToolTip(QStringLiteral("新建 / 编辑 / 删除分组"));
    connect(manageButton, &QPushButton::clicked, this, [this, project_ = *project]() {
        GroupManagerDialog dialog(m_store, project_, this);
        dialog.exec();
    });
    titleLayout->addWidget(title); titleLayout->addStretch(1); titleLayout->addWidget(manageButton);
    m_listLayout->insertWidget(m_listLayout->count() - 1, titleRow);
    // 批量操作栏(有勾选时显示)
    if (!m_checkedModules.isEmpty()) {
        const QStringList checked = checkedModulePaths(*project);
        const QString projectPath = project->path;
        auto *bar = new QFrame(m_listContainer);
        bar->setObjectName(QStringLiteral("jrBatchBar"));
        bar->setStyleSheet(QStringLiteral("QFrame#jrBatchBar { background: %1; border: 1px solid %2; border-radius: 6px; }")
            .arg(Badge::hexWithAlpha(theme::primary(), 0.08), Badge::hexWithAlpha(theme::primary(), 0.30)));
        auto *barLayout = new QHBoxLayout(bar);
        barLayout->setContentsMargins(8, 4, 8, 4); barLayout->setSpacing(6);
        auto *countLabel = new QLabel(QStringLiteral("已选 %1 个").arg(checked.size()), bar);
        countLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; border: none;").arg(theme::textSecondary().name()));
        barLayout->addWidget(countLabel); barLayout->addStretch(1);
        struct BatchAction { QString text; const char *role; std::function<void()> fn; };
        const QList<BatchAction> actions = {
            {QStringLiteral("启动"), "primary", [this, projectPath, checked]() { m_store->runModules(projectPath, checked); }},
            {QStringLiteral("停止"), "danger", [this, projectPath, checked]() { m_store->stopModules(projectPath, checked); }},
            {QStringLiteral("打包"), "", [this, projectPath, checked]() { m_store->buildModules(projectPath, checked); }},
            {QStringLiteral("取消选择"), "flat", [this]() { m_checkedModules.clear(); rebuild(); }},
        };
        for (const BatchAction &action : actions) {
            auto *button = new QPushButton(action.text, bar);
            if (*action.role) button->setProperty("role", action.role);
            connect(button, &QPushButton::clicked, this, action.fn);
            barLayout->addWidget(button);
        }
        m_listLayout->insertWidget(m_listLayout->count() - 1, bar);
    }
    // 分组:标题行 + 缩进成员行;折叠时仅隐藏成员
    const QList<ModuleGroup> groups = m_store->projectGroups(project->path);
    const QString selectedModulePath = m_store->selectedModulePath();
    QSet<QString> groupedPaths;
    for (const ModuleGroup &g : groups) {
        m_listLayout->insertWidget(m_listLayout->count() - 1, buildGroupHeader(*project, g));
        for (const Module &m : project->modules)
            if (g.modulePaths.contains(m.path)) groupedPaths.insert(m.path);
        if (m_collapsedGroups.contains(g.name)) continue;
        for (const Module &m : project->modules)
            if (g.modulePaths.contains(m.path))
                m_listLayout->insertWidget(m_listLayout->count() - 1, buildModuleRow(*project, m, m.path == selectedModulePath, 16));
    }
    // 未分组模块
    const bool hasUngrouped = std::any_of(project->modules.begin(), project->modules.end(),
        [&groupedPaths](const Module &m) { return !groupedPaths.contains(m.path); });
    if (!groups.isEmpty() && hasUngrouped) {
        auto *separator = new QLabel(QStringLiteral("未分组"), m_listContainer);
        separator->setStyleSheet(QStringLiteral("color: %1; font-size: 10px; font-weight: 600;").arg(theme::textTertiary().name()));
        m_listLayout->insertWidget(m_listLayout->count() - 1, separator);
    }
    for (const Module &m : project->modules)
        if (!groupedPaths.contains(m.path))
            m_listLayout->insertWidget(m_listLayout->count() - 1, buildModuleRow(*project, m, m.path == selectedModulePath));
    // 立即 polish + 布局:重建发生在鼠标事件内,若等事件循环再布局,
    // 快速连点的下一次点击会落在旧几何上导致"切换不过去"
    const auto children = m_listContainer->findChildren<QWidget *>();
    for (QWidget *w : children)
        w->ensurePolished();
    m_listLayout->activate();
}

QWidget *ModulePanel::buildGroupHeader(const Project &project, const ModuleGroup &group)
{
    auto *header = new ModuleRowWidget(m_listContainer);
    header->onContextMenu = [this, project, group](const QPoint &pos) {
        const Project p = project; const ModuleGroup g = group;   // 栈拷贝:菜单嵌套循环期间行可能被重建销毁
        QMenu menu(this);
        QAction *renameAction = menu.addAction(QStringLiteral("重命名"));
        QAction *editAction = menu.addAction(QStringLiteral("编辑成员"));
        QAction *deleteAction = menu.addAction(QStringLiteral("删除分组"));
        QAction *chosen = menu.exec(pos);
        if (chosen == renameAction) renameGroup(p, g.name);
        else if (chosen == editAction) { GroupEditDialog dialog(m_store, p, g.name, g.modulePaths, this); dialog.exec(); }
        else if (chosen == deleteAction) mutateGroups(p, [&g](QList<ModuleGroup> &gs) {
            gs.erase(std::remove_if(gs.begin(), gs.end(), [&g](const ModuleGroup &x) { return x.name == g.name; }), gs.end());
        });
    };
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(4, 2, 4, 2); layout->setSpacing(6);
    const bool collapsed = m_collapsedGroups.contains(group.name);
    auto *arrow = new QPushButton(collapsed ? QStringLiteral("▶") : QStringLiteral("▼"), header);
    arrow->setProperty("role", "flat"); arrow->setFixedSize(20, 20);
    arrow->setToolTip(collapsed ? QStringLiteral("展开分组") : QStringLiteral("折叠分组"));
    connect(arrow, &QPushButton::clicked, this, [this, name = group.name]() {
        if (m_collapsedGroups.contains(name)) m_collapsedGroups.remove(name);
        else m_collapsedGroups.insert(name);
        rebuild();
    });
    auto *nameLabel = new QLabel(group.name, header);
    nameLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; font-weight: 600; border: none;").arg(theme::textSecondary().name()));
    layout->addWidget(arrow); layout->addWidget(nameLabel);
    layout->addWidget(new Badge(QString::number(group.modulePaths.size()), theme::primary(), false, header));
    layout->addStretch(1);
    auto *runAllButton = new QPushButton(QStringLiteral("全部启动"), header);
    runAllButton->setProperty("role", "primary");
    runAllButton->setStyleSheet(QStringLiteral("font-size: 11px; padding: 2px 8px;"));
    connect(runAllButton, &QPushButton::clicked, this, [this, path_ = project.path, name = group.name]() { m_store->runGroup(path_, name); });
    auto *stopAllButton = new QPushButton(QStringLiteral("全部停止"), header);
    stopAllButton->setProperty("role", "danger");
    stopAllButton->setStyleSheet(QStringLiteral("font-size: 11px; padding: 2px 8px;"));
    connect(stopAllButton, &QPushButton::clicked, this, [this, path_ = project.path, name = group.name]() { m_store->stopGroup(path_, name); });
    layout->addWidget(runAllButton); layout->addWidget(stopAllButton);
    return header;
}

QWidget *ModulePanel::buildModuleRow(const Project &project, const Module &module, bool selected, int indent)
{
    ProcessTask *task = m_store->task(project.path, module.path);
    const TaskState state = task ? task->state() : TaskState{};
    const int port = task ? task->detectedPort() : 0;
    const bool checked = m_checkedModules.contains(module.path);
    auto *row = new ModuleRowWidget(m_listContainer);
    row->setProperty("jrCard", true);
    row->onClicked = [this, modulePath = module.path]() { m_store->selectModule(modulePath); };
    row->onCtrlClicked = [this, modulePath = module.path]() { toggleModuleChecked(modulePath); };
    row->onContextMenu = [this, project, module](const QPoint &pos) { showModuleContextMenu(project, module, pos); };
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(11 + indent, 8, 8, 8);
    layout->setSpacing(10);
    auto *check = new QCheckBox(row); check->setChecked(checked);
    check->setToolTip(QStringLiteral("勾选以批量操作(Ctrl+点击行亦可)"));
    connect(check, &QCheckBox::clicked, this, [this, modulePath = module.path]() { toggleModuleChecked(modulePath); });
    layout->addWidget(check);
    auto *dot = new StatusDot(row); dot->setTaskState(state);
    layout->addWidget(dot);
    auto *texts = new QVBoxLayout(); texts->setSpacing(2);
    auto *name = new QLabel(module.name, row);
    name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    name->setToolTip(module.name + QLatin1Char('\n') + module.absolutePath);
    name->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-weight: %2; border: none;")
        .arg(theme::textPrimary().name(), selected ? QStringLiteral("600") : QStringLiteral("500")));
    auto *badges = new QHBoxLayout(); badges->setSpacing(5);
    auto *langLabel = new QLabel(module.language, row);
    langLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 9px; border: none;").arg(theme::textTertiary().name()));
    badges->addWidget(langLabel);
    if (module.isSpringBoot) badges->addWidget(new Badge(QStringLiteral("SpringBoot"), theme::success(), false, row));
    if (module.hasJib) badges->addWidget(new Badge(QStringLiteral("jib"), theme::purple(), false, row));
    if (port > 0 && state.isActive()) badges->addWidget(new Badge(QStringLiteral(":%1").arg(port), theme::primary(), true, row));
    if (module.javaVersion) {
        auto *jdk = new QLabel(QStringLiteral("JDK %1").arg(*module.javaVersion), row);
        jdk->setStyleSheet(QStringLiteral("color: %1; font-size: 9px; border: none;").arg(theme::textTertiary().name()));
        badges->addWidget(jdk);
    }
    badges->addStretch(1);
    texts->addWidget(name); texts->addLayout(badges);
    layout->addLayout(texts, 1);
    // 内联按钮
    if (state.isActive()) {
        auto *stopButton = makeIconButton(QStringLiteral("media-playback-stop"), QStyle::SP_MediaStop, QStringLiteral("停止"), row);
        stopButton->setProperty("role", "danger");
        connect(stopButton, &QPushButton::clicked, this, [this, module_ = module]() { m_store->stopModule(module_); });
        layout->addWidget(stopButton);
    } else {
        auto *runButton = makeIconButton(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay, QStringLiteral("启动"), row);
        runButton->setProperty("role", "primary");
        connect(runButton, &QPushButton::clicked, this, [this, module_ = module]() { m_store->runModule(module_); });
        auto *buildButton = makeIconButton(QStringLiteral("package-x-generic"), QStyle::SP_DriveHDIcon, QStringLiteral("打包"), row);
        connect(buildButton, &QPushButton::clicked, this, [this, module_ = module]() { m_store->buildModule(module_); });
        layout->addWidget(runButton); layout->addWidget(buildButton);
    }
    // 选中态 / 勾选态样式
    const QString cardStyle = QStringLiteral("QWidget[jrCard=\"true\"] { background: %1; border: 1px solid %2; }");
    QString rowStyle;
    if (selected) rowStyle = cardStyle.arg(theme::primaryLight().name(), Badge::hexWithAlpha(theme::primary(), 0.45));
    else if (checked) rowStyle = cardStyle.arg(Badge::hexWithAlpha(theme::primary(), 0.10), Badge::hexWithAlpha(theme::primary(), 0.25));
    row->setStyleSheet(row->styleSheet() + rowStyle);
    return row;
}

void ModulePanel::showModuleContextMenu(Project project, Module module, QPoint globalPos)
{
    QMenu menu(this);
    QHash<QAction *, std::function<void()>> handlers;
    const QStringList checked = checkedModulePaths(project);
    if (!checked.isEmpty()) {
        const QString suffix = QStringLiteral(" (%1)").arg(checked.size());
        handlers.insert(menu.addAction(QStringLiteral("启动选中") + suffix), [this, project, checked]() { m_store->runModules(project.path, checked); });
        handlers.insert(menu.addAction(QStringLiteral("停止选中") + suffix), [this, project, checked]() { m_store->stopModules(project.path, checked); });
        handlers.insert(menu.addAction(QStringLiteral("打包选中") + suffix), [this, project, checked]() { m_store->buildModules(project.path, checked); });
        menu.addSeparator();
    }
    const bool running = m_store->isRunning(project.path, module.path);
    handlers.insert(menu.addAction(running ? QStringLiteral("停止此模块") : QStringLiteral("启动此模块")), [this, project, module, running]() {
        if (running) m_store->stopModule(module); else m_store->runModule(module);
    });
    menu.addSeparator();
    QMenu *addMenu = menu.addMenu(QStringLiteral("添加到分组"));
    const QList<ModuleGroup> groups = m_store->projectGroups(project.path);
    for (const ModuleGroup &g : groups)
        handlers.insert(addMenu->addAction(g.name), [this, project, module, g]() {
            mutateGroups(project, [&g, &module](QList<ModuleGroup> &gs) {
                for (ModuleGroup &x : gs) if (x.name == g.name && !x.modulePaths.contains(module.path)) x.modulePaths.append(module.path);
            });
        });
    handlers.insert(addMenu->addAction(QStringLiteral("新建分组…")), [this, project, module]() {
        GroupEditDialog dialog(m_store, project, QString(), QStringList{module.path}, this);
        dialog.exec();
    });
    const bool inGroup = std::any_of(groups.begin(), groups.end(), [&module](const ModuleGroup &g) { return g.modulePaths.contains(module.path); });
    if (inGroup)
        handlers.insert(menu.addAction(QStringLiteral("从分组移除")), [this, project, module]() {
            mutateGroups(project, [&module](QList<ModuleGroup> &gs) { for (ModuleGroup &g : gs) g.modulePaths.removeAll(module.path); });
        });
    menu.addSeparator();
    handlers.insert(menu.addAction(QStringLiteral("模块配置")), [this, project, module]() { showModuleConfig(project, module); });
    QAction *chosen = menu.exec(globalPos);
    if (chosen && handlers.contains(chosen)) handlers.value(chosen)();
}

void ModulePanel::toggleModuleChecked(const QString &modulePath)
{
    if (m_checkedModules.contains(modulePath)) m_checkedModules.remove(modulePath);
    else m_checkedModules.insert(modulePath);
    rebuild();
}

QStringList ModulePanel::checkedModulePaths(const Project &project) const
{
    QStringList paths;
    for (const Module &m : project.modules)
        if (m_checkedModules.contains(m.path)) paths.append(m.path);
    return paths;
}

void ModulePanel::mutateGroups(const Project &project, const std::function<void(QList<ModuleGroup> &)> &mutator)
{
    QList<ModuleGroup> groups = m_store->projectGroups(project.path);
    mutator(groups);
    m_store->updateGroups(project.path, groups);
}

void ModulePanel::renameGroup(const Project &project, const QString &oldName)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名分组"),
        QStringLiteral("分组名称"), QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || name.isEmpty() || name == oldName) return;
    mutateGroups(project, [&oldName, &name](QList<ModuleGroup> &gs) {
        for (ModuleGroup &g : gs) if (g.name == oldName) g.name = name;
    });
}

void ModulePanel::showModuleConfig(const Project &project, const Module &module)
{
    ModuleConfigDialog dialog(m_store, project, module, this);
    dialog.exec();
}

} // namespace jrunner