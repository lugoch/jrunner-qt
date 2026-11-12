#pragma once

#include <QWidget>

#include "app/app_store.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;

namespace jrunner {

// 【项目侧边栏】对应 macOS 版 ProjectSidebar.swift:搜索、项目列表、右键菜单、添加项目。

class ProjectSidebar : public QWidget {
    Q_OBJECT

public:
    explicit ProjectSidebar(AppStore *store, QWidget *parent = nullptr);
    void refreshTheme();

signals:
    void openSettingsRequested();

private slots:
    void rebuildList();
    void refreshSpinState();

private:
    void showContextMenu(const QPoint &pos);
    QWidget *buildRowWidget(const Project &project, bool selected, bool running, bool showParent);

    AppStore *m_store;
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_spinTimer = nullptr;
    int m_spinFrame = 0;
    static const char *kSpinFrames[];
};

} // namespace jrunner
