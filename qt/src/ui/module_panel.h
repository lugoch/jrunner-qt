#pragma once

#include <QPoint>
#include <QSet>
#include <QString>
#include <QWidget>

#include <functional>

#include "app/app_store.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace jrunner {

// 【模块面板】对应 macOS 版 ModuleDetailPanel.swift:项目头部(重扫/打开)、
// 模块与 profile 选择、运行/打包/停止按钮、模块列表(分组/多选批量)、模块配置对话框。

class ModulePanel : public QWidget {
    Q_OBJECT

public:
    explicit ModulePanel(AppStore *store, QWidget *parent = nullptr);
    void refreshTheme();

private:
    void rebuild();
    void rebuildHeader();
    void rebuildModuleList();
    QWidget *buildModuleRow(const Project &project, const Module &module, bool selected, int indent = 0);
    QWidget *buildGroupHeader(const Project &project, const ModuleGroup &group);
    void showModuleContextMenu(Project project, Module module, QPoint globalPos);
    void toggleModuleChecked(const QString &modulePath);
    QStringList checkedModulePaths(const Project &project) const;
    void mutateGroups(const Project &project, const std::function<void(QList<ModuleGroup> &)> &mutator);
    void renameGroup(const Project &project, const QString &oldName);
    void showModuleConfig(const Project &project, const Module &module);

    AppStore *m_store;
    QWidget *m_header = nullptr;
    QScrollArea *m_listScroll = nullptr;
    QWidget *m_listContainer = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QComboBox *m_modulePicker = nullptr;
    QComboBox *m_profilePicker = nullptr;
    QPushButton *m_actionButton = nullptr;
    QPushButton *m_buildButton = nullptr;
    QPushButton *m_configButton = nullptr;
    bool m_rebuilding = false;
    QSet<QString> m_checkedModules;      // 勾选待批量操作的模块路径
    QSet<QString> m_collapsedGroups;     // 折叠的分组名
};

} // namespace jrunner