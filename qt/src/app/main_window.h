#pragma once

#include <QMainWindow>

#include "app/app_store.h"

class QComboBox;
class QLabel;
class QMenu;
class QMenuBar;
class QPushButton;
class QSplitter;
class QTimer;

namespace jrunner {

class FileBrowserPanel;
class FileEditorPanel;
class LogConsole;
class ModulePanel;
class ProjectSidebar;
class StatusDot;

// 【主窗口】对应 macOS 版 MainView.swift:标题栏(logo/分支切换/外观/工作区切换/
// 快捷按钮)、三栏分割布局、状态栏、菜单与快捷键。

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(AppStore *store, QWidget *parent = nullptr);

    bool saveCurrentFile();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildTitleBar();
    QWidget *buildStatusBar();
    void buildMenus();
    void refreshBranchSwitcherMenu();
    void showBranchSwitcherMenu();
    void showSettings();
    void toggleSelectedModule();
    void setWorkspace(int workspace);      // 0=模块,1=文件
    void refreshTitleBar();
    void refreshStatusBar();
    void refreshTheme();
    void updateMiddleWidth();
    void toggleMaximize();
    void updateMaximizeIcon();
    Qt::Edges resizeEdgesAt(const QPoint &pos) const;

    AppStore *m_store;

    // 标题栏(自绘,融合菜单栏与窗口控制按钮)
    QWidget *m_titleBar = nullptr;
    QMenuBar *m_menuBar = nullptr;
    QPushButton *m_minButton = nullptr;
    QPushButton *m_maxButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QComboBox *m_workspacePicker = nullptr;
    QPushButton *m_appearanceButton = nullptr;
    QPushButton *m_branchButton = nullptr;
    QMenu *m_branchMenu = nullptr;
    QString m_branchMenuPath;
    QLabel *m_projectNameLabel = nullptr;
    QLabel *m_titleNameLabel = nullptr;
    QLabel *m_titleSubtitleLabel = nullptr;
    QPushButton *m_actionButton = nullptr;
    QPushButton *m_buildButton = nullptr;

    // 三栏
    QSplitter *m_splitter = nullptr;
    int m_workspace = 0;
    QList<int> m_workspaceSizes[2];
    ProjectSidebar *m_sidebar = nullptr;
    ModulePanel *m_modulePanel = nullptr;
    LogConsole *m_logConsole = nullptr;
    FileBrowserPanel *m_fileBrowser = nullptr;
    FileEditorPanel *m_fileEditor = nullptr;

    // 状态栏
    QLabel *m_statusMessage = nullptr;
    QLabel *m_statusProjectInfo = nullptr;
    StatusDot *m_statusDot = nullptr;
    QLabel *m_statusStateText = nullptr;
    QLabel *m_statusVersion = nullptr;
};

} // namespace jrunner
