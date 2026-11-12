#include "main_window.h"

#include "../ui/code_editor.h"
#include "../ui/file_browser.h"
#include "../ui/log_console.h"
#include "../ui/module_panel.h"
#include "../ui/project_sidebar.h"
#include "../ui/settings_dialog.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#ifndef JRUNNER_VERSION
#define JRUNNER_VERSION "dev"
#endif

namespace jrunner {

namespace {

/// 中栏推荐宽度:模块行要放下名称 + 语言 + 徽章 + JDK;文件树只需放下路径
int preferredMiddleWidth(int workspace)
{
    return workspace == 0 ? 430 : 300;
}

/// 无边框窗口的可拖拽 resize 边环宽度(中央 widget 的外边距)
constexpr int kResizeBorder = 5;

} // namespace

MainWindow::MainWindow(AppStore *store, QWidget *parent)
    : QMainWindow(parent)
    , m_store(store)
{
    setWindowTitle(QStringLiteral("JRunner"));
    setMinimumSize(1080, 640);
    setWindowIcon(QIcon::fromTheme(QStringLiteral("jrunner")));
    // 自绘标题栏:去掉系统装饰(Wayland 下不请求 SSD),由本窗口绘制标题栏与窗口按钮
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    // 外圈留一条 resize 边环(无边框后靠它触发 startSystemResize)
    layout->setContentsMargins(kResizeBorder, kResizeBorder, kResizeBorder, kResizeBorder);
    layout->setSpacing(0);

    layout->addWidget(buildTitleBar());
    auto *divider = new QFrame(central);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("color: %1;").arg(theme::border().name(QColor::HexArgb)));
    layout->addWidget(divider);

    // 三栏分割(QSplitter 原生拖拽对应 macOS 的 SplitHandle)
    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setHandleWidth(7);
    m_splitter->setChildrenCollapsible(false);

    m_sidebar = new ProjectSidebar(m_store, m_splitter);
    m_modulePanel = new ModulePanel(m_store, m_splitter);
    m_logConsole = new LogConsole(m_store, m_splitter);
    m_fileBrowser = new FileBrowserPanel(m_store, m_splitter);
    m_fileEditor = new FileEditorPanel(m_store, m_splitter);

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_modulePanel);
    m_splitter->addWidget(m_logConsole);
    m_splitter->addWidget(m_fileBrowser);
    m_splitter->addWidget(m_fileEditor);

    // 初始显示模块工作区
    m_fileBrowser->hide();
    m_fileEditor->hide();
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 1);
    m_splitter->setStretchFactor(3, 0);
    m_splitter->setStretchFactor(4, 1);

    // 文件树 → 编辑器
    connect(m_fileBrowser, &FileBrowserPanel::fileOpenRequested, this,
            [this](const QString &path) {
                m_store->editor()->open(path);
            });

    // 分割条初始位置
    m_splitter->setSizes({ 268, preferredMiddleWidth(0), 500, 0, 0 });

    layout->addWidget(m_splitter, 1);
    layout->addWidget(buildStatusBar());
    setCentralWidget(central);

    buildMenus();

    // 边环 resize + 标题栏拖拽/双击最大化
    central->setMouseTracking(true);
    central->installEventFilter(this);
    m_titleBar->installEventFilter(this);

    // ---- 信号联动 ----
    connect(m_sidebar, &ProjectSidebar::openSettingsRequested, this, &MainWindow::showSettings);
    connect(m_store, &AppStore::selectionChanged, this, [this]() {
        refreshTitleBar();
        refreshStatusBar();
        if (m_branchMenuPath != m_store->selectedProjectPath())
            refreshBranchSwitcherMenu();
        const auto project = m_store->selectedProject();
        m_fileBrowser->setProject(project ? project->path : QString());
    });
    connect(m_store, &AppStore::projectsChanged, this, [this]() {
        refreshTitleBar();
        refreshStatusBar();
    });
    connect(m_store, &AppStore::taskStateChanged, this, [this](const QString &) {
        refreshTitleBar();
        refreshStatusBar();
    });
    connect(m_store, &AppStore::activeTaskChanged, this, [this]() {
        refreshTitleBar();
        refreshStatusBar();
    });
    connect(m_store, &AppStore::statusMessageChanged, this, [this](const QString &message) {
        m_statusMessage->setText(message);
    });
    connect(m_store, &AppStore::isScanningChanged, this, [this]() { refreshStatusBar(); });

    // Git 状态:标题栏分支切换器 + 侧边栏徽章
    connect(m_store->git(), &GitMonitor::statusUpdated, this,
            [this](const QString &path) {
                if (path != m_store->selectedProjectPath())
                    return;
                refreshTitleBar();
                refreshBranchSwitcherMenu();
            });
    connect(m_store->git(), &GitMonitor::branchesUpdated, this,
            [this](const QString &path) {
                if (path == m_store->selectedProjectPath())
                    refreshBranchSwitcherMenu();
            });
    connect(m_store->git(), &GitMonitor::checkingOutChanged,
            this, &MainWindow::refreshTitleBar);
    connect(m_store->git(), &GitMonitor::checkoutSucceeded, this,
            [this](const QString &path, const QString &branch) {
                if (path != m_store->selectedProjectPath())
                    return;
                m_store->rescanProject(path);
                m_store->setStatusMessage(QStringLiteral("已切换到 %1").arg(branch));
            });
    connect(m_store->git(), &GitMonitor::checkoutFailed, this,
            [this](const QString &title, const QString &message) {
                m_store->setStatusMessage(title);
                QMessageBox::warning(this, title, message);
            });

    // 回到前台时重读分支(可能刚在终端 / 其他工具里切过分支)
    connect(qApp, &QApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            m_store->refreshGit(true);
    });

    // 主题切换:重刷所有内联捕获的主题色,避免深色下残留浅色文字
    connect(qApp, &QApplication::paletteChanged, this, [this]() { refreshTheme(); });

    // 退出前停掉所有子进程,避免遗留 gradle / java 进程
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { m_store->stopAll(); });

    refreshTitleBar();
    refreshStatusBar();
}

QWidget *MainWindow::buildTitleBar()
{
    auto *bar = new QWidget(this);
    m_titleBar = bar;
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(10, 6, 6, 6);
    layout->setSpacing(10);

    // Logo(橙色渐变 + 咖啡杯,对应 macOS 的 cup.and.saucer)
    auto *logo = new QLabel(QStringLiteral("☕"), bar);
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(26, 26);
    logo->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: white; font-size: 13px;"
        "  border-radius: 7px;"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 rgb(255,199,92), stop:1 rgb(232,105,38));"
        "}"));

    auto *titles = new QVBoxLayout();
    titles->setSpacing(0);
    auto *name = new QLabel(QStringLiteral("JRunner"), bar);
    m_titleNameLabel = name;
    QFont nameFont = name->font();
    nameFont.setPointSize(11);
    nameFont.setBold(true);
    name->setFont(nameFont);
    name->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textPrimary().name()));
    auto *subtitle = new QLabel(QStringLiteral("Java / Kotlin 服务启动器"), bar);
    m_titleSubtitleLabel = subtitle;
    subtitle->setStyleSheet(
        QStringLiteral("color: %1; font-size: 9px;").arg(theme::textTertiary().name()));
    titles->addWidget(name);
    titles->addWidget(subtitle);
    layout->addWidget(logo);
    layout->addLayout(titles);

    // 菜单栏融入标题栏(内嵌 QMenuBar,不再占用独立一行)
    m_menuBar = new QMenuBar(bar);
    m_menuBar->setNativeMenuBar(false);
    m_menuBar->setStyleSheet(QStringLiteral(
        "QMenuBar { background: transparent; color: %1; font-size: 11px; }"
        "QMenuBar::item { background: transparent; padding: 4px 8px; border-radius: 6px; }"
        "QMenuBar::item:selected { background: %2; }")
        .arg(theme::textSecondary().name(), Badge::hexWithAlpha(theme::textTertiary(), 0.15)));
    layout->addWidget(m_menuBar);

    // 当前分支(跟随选中项目,点开可切换)
    m_branchButton = new QPushButton(bar);
    m_branchButton->setToolTip(QStringLiteral("点击切换分支"));
    m_branchButton->hide();
    m_branchMenu = new QMenu(m_branchButton);
    connect(m_branchButton, &QPushButton::clicked, this, &MainWindow::showBranchSwitcherMenu);
    layout->addWidget(m_branchButton);

    m_projectNameLabel = new QLabel(bar);
    m_projectNameLabel->setTextFormat(Qt::PlainText);
    m_projectNameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_projectNameLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    m_projectNameLabel->setMaximumWidth(140);
    layout->addWidget(m_projectNameLabel);

    layout->addStretch(1);

    // 外观切换
    m_appearanceButton = new QPushButton(bar);
    m_appearanceButton->setFlat(true);
    m_appearanceButton->setFixedSize(30, 30);
    m_appearanceButton->setToolTip(QStringLiteral("切换外观"));
    m_appearanceButton->setAccessibleName(QStringLiteral("切换外观"));
    connect(m_appearanceButton, &QPushButton::clicked, this, [this]() {
        auto *menu = new QMenu(m_appearanceButton);
        const auto current = theme::currentMode();
        const QList<QPair<QString, theme::ThemeMode>> modes = {
            { QStringLiteral("◐ 跟随系统"), theme::ThemeMode::System },
            { QStringLiteral("☀ 浅色"), theme::ThemeMode::Light },
            { QStringLiteral("☾ 深色"), theme::ThemeMode::Dark },
        };
        for (const auto &entry : modes) {
            QAction *action = menu->addAction(entry.first, this, [this, mode = entry.second]() {
                theme::applyTheme(mode);
                theme::storeThemeMode(mode);
                refreshTitleBar();
            });
            action->setCheckable(true);
            action->setChecked(current == entry.second);
        }
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        menu->popup(m_appearanceButton->mapToGlobal(QPoint(0, m_appearanceButton->height())));
    });
    layout->addWidget(m_appearanceButton);

    // 工作区切换(两组联动 → 一个选择器)
    m_workspacePicker = new QComboBox(bar);
    m_workspacePicker->setToolTip(
        QStringLiteral("模块列表(配调试日志)/ 文件树(配代码编辑器)"));
    m_workspacePicker->addItem(QStringLiteral("模块"));
    m_workspacePicker->addItem(QStringLiteral("文件"));
    m_workspacePicker->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_workspacePicker->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    connect(m_workspacePicker, &QComboBox::currentIndexChanged, this,
            [this](int index) { setWorkspace(index); });
    layout->addWidget(m_workspacePicker);

    // 快捷操作
    m_actionButton = new QPushButton(bar);
    m_actionButton->setProperty("role", "primary");
    connect(m_actionButton, &QPushButton::clicked, this, &MainWindow::toggleSelectedModule);
    m_buildButton = new QPushButton(QStringLiteral("打包"), bar);
    connect(m_buildButton, &QPushButton::clicked, this, [this]() {
        if (const auto m = m_store->selectedModule())
            m_store->buildModule(*m);
    });
    layout->addWidget(m_actionButton);
    layout->addWidget(m_buildButton);

    // ---- 窗口控制按钮(自绘标题栏右侧) ----
    layout->addSpacing(6);
    auto makeWindowButton = [bar](const QString &glyph, const QString &name, const QString &tip) {
        auto *b = new QPushButton(glyph, bar);
        b->setObjectName(name);
        b->setFixedSize(34, 28);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { color: %1; font-size: 12px; border: none; border-radius: 6px;"
            "  background: transparent; }"
            "QPushButton:hover { background: %2; }")
            .arg(theme::textSecondary().name(), Badge::hexWithAlpha(theme::textTertiary(), 0.18)));
        return b;
    };
    m_minButton = makeWindowButton(QStringLiteral("—"), QStringLiteral("jrMinButton"),
                                   QStringLiteral("最小化"));
    m_maxButton = makeWindowButton(QStringLiteral("□"), QStringLiteral("jrMaxButton"),
                                   QStringLiteral("最大化"));
    m_closeButton = makeWindowButton(QStringLiteral("✕"), QStringLiteral("jrCloseButton"),
                                     QStringLiteral("关闭"));
    m_closeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 12px; border: none; border-radius: 6px;"
        "  background: transparent; }"
        "QPushButton:hover { background: %2; color: white; }")
        .arg(theme::textSecondary().name(), theme::danger().name()));
    connect(m_minButton, &QPushButton::clicked, this, [this]() { showMinimized(); });
    connect(m_maxButton, &QPushButton::clicked, this, &MainWindow::toggleMaximize);
    connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);
    layout->addWidget(m_minButton);
    layout->addWidget(m_maxButton);
    layout->addWidget(m_closeButton);

    return bar;
}

QWidget *MainWindow::buildStatusBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 5, 14, 5);
    layout->setSpacing(12);

    m_statusMessage = new QLabel(bar);
    m_statusMessage->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textSecondary().name()));
    layout->addWidget(m_statusMessage, 1);

    m_statusProjectInfo = new QLabel(bar);
    m_statusProjectInfo->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    layout->addWidget(m_statusProjectInfo);

    m_statusDot = new StatusDot(bar);
    layout->addWidget(m_statusDot);
    m_statusStateText = new QLabel(bar);
    m_statusStateText->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    layout->addWidget(m_statusStateText);

    // 版本号:来自构建系统注入的 VERSION 文件
    m_statusVersion = new QLabel(QStringLiteral("v%1").arg(QLatin1String(JRUNNER_VERSION)), bar);
    m_statusVersion->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;")
            .arg(Badge::hexWithAlpha(theme::textTertiary(), 0.85)));
    layout->addWidget(m_statusVersion);
    return bar;
}

void MainWindow::buildMenus()
{
    // 操作菜单(内嵌标题栏的 QMenuBar;Wayland 无全局菜单)
    QMenu *operationMenu = m_menuBar->addMenu(QStringLiteral("操作"));

    QAction *rescanAction = operationMenu->addAction(QStringLiteral("重新扫描项目"));
    rescanAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(rescanAction, &QAction::triggered, this, [this]() { m_store->scanProjects(); });

    QAction *runAction = operationMenu->addAction(QStringLiteral("启动 / 停止服务"));
    runAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(runAction, &QAction::triggered, this, &MainWindow::toggleSelectedModule);

    QAction *buildAction = operationMenu->addAction(QStringLiteral("打包"));
    buildAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(buildAction, &QAction::triggered, this, [this]() {
        if (const auto m = m_store->selectedModule())
            m_store->buildModule(*m);
    });

    QAction *stopAction = operationMenu->addAction(QStringLiteral("停止"));
    stopAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+.")));
    connect(stopAction, &QAction::triggered, this, [this]() {
        if (const auto m = m_store->selectedModule())
            m_store->stopModule(*m);
    });

    operationMenu->addSeparator();

    QAction *saveAction = operationMenu->addAction(QStringLiteral("保存文件"));
    saveAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    connect(saveAction, &QAction::triggered, this, [this]() {
        if (saveCurrentFile())
            m_store->setStatusMessage(
                QStringLiteral("已保存 %1").arg(m_store->editor()->fileName()));
    });

    QAction *findAction = operationMenu->addAction(QStringLiteral("查找"));
    findAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    connect(findAction, &QAction::triggered, this, [this]() { m_fileEditor->showFindBar(); });

    operationMenu->addSeparator();

    QAction *settingsAction = operationMenu->addAction(QStringLiteral("设置"));
    settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);

    // 帮助菜单
    QMenu *helpMenu = m_menuBar->addMenu(QStringLiteral("帮助"));
    QAction *aboutAction = helpMenu->addAction(QStringLiteral("关于 JRunner"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("关于 JRunner"),
                           QStringLiteral("JRunner Linux v%1\n\n轻量级 Java / Kotlin 服务启动器\n"
                                          "MIT License · 基于 Qt 6 的 Wayland 适配版")
                               .arg(QLatin1String(JRUNNER_VERSION)));
    });

    // 内嵌菜单栏未经 QMainWindow::setMenuBar,显式挂到窗口以保证快捷键全局生效
    for (QMenu *menu : { operationMenu, helpMenu })
        for (QAction *a : menu->actions())
            if (!a->isSeparator())
                addAction(a);
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(m_store, this);
    dialog.exec();
}

void MainWindow::toggleSelectedModule()
{
    const auto project = m_store->selectedProject();
    const auto module = m_store->selectedModule();
    if (!project || !module)
        return;
    if (m_store->isRunning(project->path, module->path))
        m_store->stopModule(*module);
    else
        m_store->runModule(*module);
}

void MainWindow::setWorkspace(int workspace)
{
    if ((workspace != 0 && workspace != 1) || workspace == m_workspace)
        return;
    m_workspaceSizes[m_workspace] = m_splitter->sizes();
    m_workspace = workspace;
    const bool files = workspace == 1;
    m_modulePanel->setVisible(!files);
    m_logConsole->setVisible(!files);
    m_fileBrowser->setVisible(files);
    m_fileEditor->setVisible(files);
    if (files) {
        const auto project = m_store->selectedProject();
        m_fileBrowser->setProject(project ? project->path : QString());
    }
    updateMiddleWidth();
}

void MainWindow::updateMiddleWidth()
{
    // 仅在切换工作区时恢复对应的分割位置,不覆盖用户拖动后的宽度。
    const auto &previous = m_workspaceSizes[1 - m_workspace];
    const int sidebar = previous.isEmpty() ? 268 : previous.at(0);
    const auto &saved = m_workspaceSizes[m_workspace];
    const int middle = saved.isEmpty() ? preferredMiddleWidth(m_workspace)
                                      : saved.at(m_workspace == 0 ? 1 : 3);
    const int right = qMax(360, m_splitter->width() - sidebar - middle
                                   - 2 * m_splitter->handleWidth());
    m_splitter->setSizes(m_workspace == 0 ? QList<int>{ sidebar, middle, right, 0, 0 }
                                         : QList<int>{ sidebar, 0, 0, middle, right });
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
}

void MainWindow::toggleMaximize()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

void MainWindow::updateMaximizeIcon()
{
    if (!m_maxButton)
        return;
    const bool max = isMaximized();
    m_maxButton->setText(max ? QStringLiteral("❐") : QStringLiteral("□"));
    m_maxButton->setToolTip(max ? QStringLiteral("还原") : QStringLiteral("最大化"));
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateMaximizeIcon();
        if (isMaximized())
            centralWidget()->unsetCursor();
    }
}

Qt::Edges MainWindow::resizeEdgesAt(const QPoint &pos) const
{
    const QWidget *central = centralWidget();
    Qt::Edges edges;
    if (pos.x() <= kResizeBorder)
        edges |= Qt::LeftEdge;
    if (pos.x() >= central->width() - kResizeBorder)
        edges |= Qt::RightEdge;
    if (pos.y() <= kResizeBorder)
        edges |= Qt::TopEdge;
    if (pos.y() >= central->height() - kResizeBorder)
        edges |= Qt::BottomEdge;
    return edges;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_titleBar) {
        // 空白标题栏:拖拽移动 / 双击最大化(Wayland 走 xdg-shell 协议)
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton && windowHandle()) {
                windowHandle()->startSystemMove();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                toggleMaximize();
                return true;
            }
        }
    } else if (watched == centralWidget() && !isMaximized()) {
        if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            const Qt::Edges e = resizeEdgesAt(me->position().toPoint());
            QWidget *central = centralWidget();
            const bool l = e.testFlag(Qt::LeftEdge), r = e.testFlag(Qt::RightEdge);
            const bool t = e.testFlag(Qt::TopEdge), b = e.testFlag(Qt::BottomEdge);
            if ((l && t) || (r && b))
                central->setCursor(Qt::SizeFDiagCursor);
            else if ((r && t) || (l && b))
                central->setCursor(Qt::SizeBDiagCursor);
            else if (l || r)
                central->setCursor(Qt::SizeHorCursor);
            else if (t || b)
                central->setCursor(Qt::SizeVerCursor);
            else
                central->unsetCursor();
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const Qt::Edges edges = resizeEdgesAt(me->position().toPoint());
                if (edges && windowHandle()) {
                    windowHandle()->startSystemResize(edges);
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshBranchSwitcherMenu()
{
    // 异步更新只刷新同一个菜单的数据,绝不弹出菜单。
    const QString path = m_store->selectedProjectPath();
    if (m_branchMenuPath != path)
        m_branchMenu->hide();
    m_branchMenuPath = path;
    m_branchMenu->clear();
    const GitStatus status = m_store->git()->status(path);
    if (path.isEmpty() || !status.isRepo() || m_store->git()->isCheckingOut()) {
        m_branchMenu->hide();
        return;
    }

    const QStringList list = m_store->git()->branches(path);
    if (list.isEmpty()) {
        m_branchMenu->addAction(QStringLiteral("暂无本地分支"))->setEnabled(false);
    } else {
        for (const QString &branch : list) {
            QAction *action = m_branchMenu->addAction(branch, this, [this, branch, path]() {
                if (path != m_store->selectedProjectPath() || m_store->git()->isCheckingOut())
                    return;
                const GitStatus current = m_store->git()->status(path);
                if (!current.isRepo() || (!current.isDetached && current.branch == branch))
                    return;
                m_store->setStatusMessage(QStringLiteral("正在切换到 %1...").arg(branch));
                m_store->git()->checkoutBranch(path, branch);
            });
            action->setCheckable(true);
            action->setChecked(!status.isDetached && branch == status.branch);
        }
    }
    m_branchMenu->addSeparator();
    m_branchMenu->addAction(QStringLiteral("刷新分支列表"), this, [this, path]() {
        if (path != m_store->selectedProjectPath() || m_store->git()->isCheckingOut())
            return;
        m_store->git()->loadBranches(path);
        m_store->git()->refresh(path, true);
    });
}

void MainWindow::showBranchSwitcherMenu()
{
    if (m_branchMenu->isVisible()) {
        m_branchMenu->hide();
        return;
    }
    const QString path = m_store->selectedProjectPath();
    if (path.isEmpty() || !m_store->git()->status(path).isRepo()
        || m_store->git()->isCheckingOut())
        return;
    refreshBranchSwitcherMenu();
    m_branchMenu->popup(m_branchButton->mapToGlobal(QPoint(0, m_branchButton->height())));
    m_store->git()->refresh(path);
}

void MainWindow::refreshTitleBar()
{
    // 主题切换后重刷内联颜色(构造时捕获的旧主题色会残留)
    m_titleNameLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textPrimary().name()));
    m_titleSubtitleLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 9px;").arg(theme::textTertiary().name()));
    m_projectNameLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    m_menuBar->setStyleSheet(QStringLiteral(
        "QMenuBar { background: transparent; color: %1; font-size: 11px; }"
        "QMenuBar::item { background: transparent; padding: 4px 8px; border-radius: 6px; }"
        "QMenuBar::item:selected { background: %2; }")
        .arg(theme::textSecondary().name(), Badge::hexWithAlpha(theme::textTertiary(), 0.15)));

    const auto project = m_store->selectedProject();
    const auto module = m_store->selectedModule();

    // 分支切换器
    const bool checkingOut = m_store->git()->isCheckingOut();
    m_branchButton->setEnabled(!checkingOut);
    if (checkingOut || !project || m_branchMenuPath != project->path)
        m_branchMenu->hide();
    if (project) {
        const GitStatus status = m_store->git()->status(project->path);
        if (status.isRepo()) {
            const QColor tint = status.isDetached ? theme::warning() : theme::purple();
            m_branchButton->setToolTip(status.tooltip() + QStringLiteral("\n点击切换分支"));
            m_branchButton->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "  color: %1; font-size: 10px; font-family: '%2';"
                "  padding: 3px 8px; border-radius: 8px;"
                "  background: %3; border: 1px solid %4;"
                "}")
                .arg(tint.name(), theme::monospaceFontFamily(),
                     Badge::hexWithAlpha(tint, 0.12), Badge::hexWithAlpha(tint, 0.22)));
            m_branchButton->ensurePolished();
            const QString branch = m_branchButton->fontMetrics().elidedText(
                status.branch, Qt::ElideRight, 180);
            m_branchButton->setText(QStringLiteral("⎇ %1 ▾").arg(branch).replace(
                QLatin1Char('&'), QStringLiteral("&&")));
            m_branchButton->show();
        } else {
            m_branchButton->hide();
            m_branchMenu->hide();
        }
        m_projectNameLabel->ensurePolished();
        const QFontMetrics metrics = m_projectNameLabel->fontMetrics();
        const int width = qMin(140, metrics.horizontalAdvance(project->name));
        m_projectNameLabel->setFixedWidth(width);
        m_projectNameLabel->setText(metrics.elidedText(project->name, Qt::ElideRight, width));
        m_projectNameLabel->setToolTip(project->name + QLatin1Char('\n') + project->path);
    } else {
        m_branchButton->hide();
        m_projectNameLabel->clear();
        m_projectNameLabel->setToolTip(QString());
        m_projectNameLabel->setFixedWidth(0);
    }

    // 外观按钮
    switch (theme::currentMode()) {
    case theme::ThemeMode::Light:
        m_appearanceButton->setText(QStringLiteral("☀"));
        break;
    case theme::ThemeMode::Dark:
        m_appearanceButton->setText(QStringLiteral("☾"));
        break;
    case theme::ThemeMode::System:
        m_appearanceButton->setText(QStringLiteral("◐"));
        break;
    }
    m_appearanceButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 12px; padding: 0;"
        " border: none; background: transparent; }")
        .arg(theme::textSecondary().name()));

    // 快捷按钮随运行状态切换
    m_actionButton->setEnabled(project && module);
    m_buildButton->setEnabled(project && module);
    if (module && project) {
        const bool running = m_store->isRunning(project->path, module->path);
        if (running) {
            m_actionButton->setText(QStringLiteral("■ 停止"));
            m_actionButton->setProperty("role", "danger");
            m_buildButton->hide();
        } else {
            m_actionButton->setText(QStringLiteral("▶ 启动服务"));
            m_actionButton->setProperty("role", "primary");
            m_buildButton->show();
        }
        repolish(m_actionButton);
    } else {
        m_actionButton->setText(QStringLiteral("▶ 启动服务"));
        m_actionButton->setProperty("role", "primary");
        m_buildButton->show();
        repolish(m_actionButton);
    }
}

void MainWindow::refreshStatusBar()
{
    m_statusMessage->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textSecondary().name()));
    m_statusProjectInfo->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    m_statusStateText->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    m_statusVersion->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;")
                                       .arg(Badge::hexWithAlpha(theme::textTertiary(), 0.85)));
    m_statusMessage->setText(m_store->statusMessage());

    const auto project = m_store->selectedProject();
    if (project) {
        m_statusProjectInfo->setText(
            QStringLiteral("%1 · %2 模块")
                .arg(buildToolDisplayName(project->tool))
                .arg(project->modules.size()));
        m_statusProjectInfo->show();
    } else {
        m_statusProjectInfo->hide();
    }

    const auto module = m_store->selectedModule();
    ProcessTask *task = nullptr;
    if (project && module)
        task = m_store->task(project->path, module->path);
    if (task) {
        m_statusDot->setTaskState(task->state());
        m_statusDot->show();
        const TaskState state = task->state();
        QString text;
        switch (state.kind) {
        case TaskState::Idle: text = QStringLiteral("空闲"); break;
        case TaskState::Starting: text = QStringLiteral("启动中"); break;
        case TaskState::Running: text = QStringLiteral("运行中"); break;
        case TaskState::Stopping: text = QStringLiteral("停止中"); break;
        case TaskState::Success: text = QStringLiteral("已完成"); break;
        case TaskState::Failed: text = QStringLiteral("失败"); break;
        }
        m_statusStateText->setText(text);
        m_statusStateText->show();
    } else {
        m_statusDot->hide();
        m_statusStateText->hide();
    }
}

void MainWindow::refreshTheme()
{
    refreshTitleBar();
    refreshStatusBar();
    m_modulePanel->refreshTheme();
    m_sidebar->refreshTheme();
    m_logConsole->refreshTheme();
    m_fileBrowser->refreshTheme();
    m_fileEditor->refreshTheme();
}

bool MainWindow::saveCurrentFile()
{
    return m_fileEditor->saveFile();
}

} // namespace jrunner
