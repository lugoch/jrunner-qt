#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace jrunner {

// 【Git 状态】对应 macOS 版 GitInfo.swift。
// 读分支走 .git/HEAD 直读(极快),查改动才起 git 进程,放后台。

struct GitStatus {
    /// 分支名;detached HEAD 时为短 SHA;空字符串表示不是 git 仓库
    QString branch;
    bool isDetached = false;
    bool isDirty = false;
    int changedCount = 0;

    static GitStatus none() { return GitStatus{}; }
    bool isRepo() const { return !branch.isEmpty(); }

    QString tooltip() const
    {
        if (!isRepo())
            return QStringLiteral("不是 Git 仓库");
        QString s = isDetached ? QStringLiteral("游离 HEAD:%1").arg(branch)
                               : QStringLiteral("当前分支:%1").arg(branch);
        if (isDirty)
            s += QStringLiteral("\n%1 个文件有未提交修改").arg(changedCount);
        return s;
    }

    bool operator==(const GitStatus &) const = default;
};

/// 不依赖 git 进程的读取(分支/引用)
namespace GitReader {

/// 向上查找仓库根目录(项目可能只是仓库里的一个子目录)
QString repoRoot(const QString &path);

/// 解析出真正的 .git 目录(兼容 worktree / submodule 的 .git 文件)
QString gitDir(const QString &repoPath);

/// 读 .git/HEAD;失败返回空 branch
GitStatus readHead(const QString &repoPath);

/// 工作区是否有改动(忽略未跟踪文件,避免 build/ 目录干扰);同步调用 git
QPair<bool, int> readWorkingTree(const QString &repoPath);

/// 列出本地分支(.git/refs/heads/** + packed-refs,不起 git 进程)
QStringList localBranches(const QString &repoPath);

/// 切换分支;失败时返回 git 的原始报错
QPair<bool, QString> checkout(const QString &repoPath, const QString &branch);

} // namespace GitReader

/// 按项目路径缓存 Git 状态(QObject 版 GitMonitor,信号驱动)
class GitMonitor : public QObject {
    Q_OBJECT

public:
    explicit GitMonitor(QObject *parent = nullptr);

    GitStatus status(const QString &path) const;
    QStringList branches(const QString &path) const;
    bool isCheckingOut() const { return m_checkingOut; }

    /// 刷新某个项目的 Git 状态;force=false 时 2 秒内不重复起 git 进程
    void refresh(const QString &path, bool force = false);
    /// 单独重读分支列表(菜单里点「刷新」时用)
    void loadBranches(const QString &path);
    /// 批量刷新(只读分支,不查改动,开销极小)
    void refreshBranches(const QStringList &paths);
    /// 切换分支;成功后更新状态缓存,再发出 checkoutSucceeded
    void checkoutBranch(const QString &path, const QString &branch);

signals:
    void statusUpdated(const QString &path);
    void branchesUpdated(const QString &path);
    void checkingOutChanged();
    void checkoutSucceeded(const QString &path, const QString &branch);
    void checkoutFailed(const QString &title, const QString &message);

private:
    void finish(const QString &path, const GitStatus &st);

    QHash<QString, GitStatus> m_statuses;
    QHash<QString, QStringList> m_branches;
    bool m_checkingOut = false;
    QString m_checkoutPath;
    // checkout 前启动的读取不能覆盖切换后的状态。
    QHash<QString, quint64> m_revisions;
    QSet<QString> m_inFlight;
    QHash<QString, qint64> m_lastRunMs;
};

} // namespace jrunner
