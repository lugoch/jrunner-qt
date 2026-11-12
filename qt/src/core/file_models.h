#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace jrunner {

// 【文件模型】对应 macOS 版 FileModels.swift:文件树(懒加载)与文本编辑器状态。

struct FileNode {
    QString path;
    QString name;
    bool isDirectory = false;

    QString ext() const
    {
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        if (dot < 0 || dot == name.size() - 1)
            return QString();
        return name.mid(dot + 1).toLower();
    }

    bool operator==(const FileNode &) const = default;
};

/// 文件树模型(QObject 版,信号驱动,后台读目录)
class FileTreeModel : public QObject {
    Q_OBJECT

public:
    explicit FileTreeModel(const QString &root, QObject *parent = nullptr);

    QString root() const { return m_root; }
    QList<FileNode> children(const QString &dir) const { return m_childrenMap.value(dir); }
    bool isExpanded(const QString &path) const { return m_expanded.contains(path); }
    QList<FileNode> searchResults() const { return m_searchResults; }

    void load(const QString &dir);
    void toggle(const FileNode &node);
    void reload();
    /// 展开到指定文件所在目录
    void reveal(const QString &path);
    /// 扁平化当前可见行(依赖展开状态)
    QList<QPair<FileNode, int>> visibleRows() const;
    void updateSearch(const QString &query);

signals:
    void childrenLoaded(const QString &dir);
    void searchFinished();

private:
    static QList<FileNode> readDirectory(const QString &dir);

    QString m_root;
    QHash<QString, QList<FileNode>> m_childrenMap;
    QSet<QString> m_expanded;
    QList<FileNode> m_searchResults;
    int m_searchGen = 0;
};

/// 文本编辑器状态(必须在 UI 线程使用)
class EditorModel : public QObject {
    Q_OBJECT

public:
    explicit EditorModel(QObject *parent = nullptr);

    QString currentPath() const { return m_currentPath; }
    QString text() const { return m_text; }
    bool isBinary() const { return m_binary; }
    QString errorMessage() const { return m_errorMessage; }

    bool isDirty() const;
    QString fileName() const;
    /// 取最后三段路径,够识别
    QString relativePath() const;

    void open(const QString &path);
    /// 编辑时同步文本
    void setText(const QString &text);
    bool save();
    void close();

signals:
    void documentChanged();

private:
    QString m_currentPath;
    QString m_text;
    QString m_savedText;
    QString m_errorMessage;
    bool m_binary = false;
    /// 未保存的草稿,切换文件时不丢修改
    QHash<QString, QString> m_drafts;
};

} // namespace jrunner
