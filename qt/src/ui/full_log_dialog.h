#pragma once

#include <QDialog>
#include <QPointer>
#include <QSyntaxHighlighter>

#include "core/process_task.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextDocument;

namespace jrunner {

// 【完整日志窗口】从磁盘文件加载任务全量历史,支持全文搜索与纯文本导出。
// 实时控制台仍只显示尾部窗口;本窗口是独立的非模态快照视图(刷新按钮重载)。

/// 按日志级别惰性着色(仅绘制可见块),避免逐行 HTML 的开销
class LogLineHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit LogLineHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;
};

class FullLogDialog : public QDialog {
    Q_OBJECT
public:
    explicit FullLogDialog(ProcessTask *task, QWidget *parent = nullptr);

private:
    void loadBrowse();
    void runSearch();
    void exportToFile();
    void updateInfo();

    QPointer<ProcessTask> m_task;
    QString m_logPath;

    QPlainTextEdit *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_infoLabel = nullptr;
    QLabel *m_bannerLabel = nullptr;
    QPushButton *m_exportButton = nullptr;
    LogLineHighlighter *m_highlighter = nullptr;
    bool m_searchMode = false;
};

} // namespace jrunner