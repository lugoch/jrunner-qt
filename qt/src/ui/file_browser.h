#pragma once

#include <QWidget>

#include "app/app_store.h"
#include "core/file_models.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QVBoxLayout;

namespace jrunner {

class CodeEditor;

// 【文件工作区】对应 macOS 版 FileBrowser.swift:
// FileBrowserPanel = 文件树(树/搜索双模式);FileEditorPanel = 编辑器 + 查找栏。

/// 文件树面板(中栏)
class FileBrowserPanel : public QWidget {
    Q_OBJECT

public:
    explicit FileBrowserPanel(AppStore *store, QWidget *parent = nullptr);
    void setProject(const QString &rootPath);
    void refreshTheme();

signals:
    void fileOpenRequested(const QString &path);

private:
    void rebuildRows();
    void rebuildSearchResults();
    void applySearch(const QString &query);
    QString fileGlyph(const FileNode &node, QColor *color) const;

    AppStore *m_store;
    FileTreeModel *m_model = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QListWidget *m_list = nullptr;
    bool m_inSearch = false;
};

/// 编辑器面板(右栏):工具栏 + CodeEditor + 查找栏
class FileEditorPanel : public QWidget {
    Q_OBJECT

public:
    explicit FileEditorPanel(AppStore *store, QWidget *parent = nullptr);
    void refreshDocument();
    void refreshTheme();

    bool saveFile();      // Ctrl+S
    void showFindBar();   // Ctrl+F
    void findNext();
    void findPrevious();

private:
    AppStore *m_store;
    QWidget *m_toolbar = nullptr;
    QLabel *m_dirtyDot = nullptr;
    QLabel *m_fileName = nullptr;
    QLabel *m_relativePath = nullptr;
    QLabel *m_lineCount = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    CodeEditor *m_editor = nullptr;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QWidget *m_errorStrip = nullptr;
    QLabel *m_errorLabel = nullptr;
};

} // namespace jrunner
