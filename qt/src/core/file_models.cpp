#include "file_models.h"

#include "debug_log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>

namespace jrunner {

namespace {

/// 构建/依赖目录不展示,避免干扰
bool isSkippedDir(const QString &name)
{
    static const QSet<QString> skip = {
        QStringLiteral(".git"), QStringLiteral(".idea"), QStringLiteral(".gradle"),
        QStringLiteral(".mvn"), QStringLiteral(".settings"), QStringLiteral("build"),
        QStringLiteral("target"), QStringLiteral("node_modules"), QStringLiteral("out"),
        QStringLiteral("bin"), QStringLiteral("obj"), QStringLiteral("dist"),
        QStringLiteral(".next"), QStringLiteral(".cache"), QStringLiteral("logs"),
        QStringLiteral("log"), QStringLiteral(".DS_Store"), QStringLiteral("__pycache__"),
        QStringLiteral(".venv"), QStringLiteral("venv"),
    };
    return skip.contains(name);
}

bool isHiddenName(const QString &name)
{
    return name.startsWith(QLatin1Char('.'))
        && name != QLatin1String(".gitignore")
        && name != QLatin1String(".env")
        && name != QLatin1String(".editorconfig");
}

} // namespace

FileTreeModel::FileTreeModel(const QString &root, QObject *parent)
    : QObject(parent)
    , m_root(root)
{
    load(root);
    // 默认展开根
    m_expanded.insert(root);
}

QList<FileNode> FileTreeModel::readDirectory(const QString &dir)
{
    QDir d(dir);
    const QStringList names = d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

    QList<FileNode> nodes;
    for (const QString &name : names) {
        if (isHiddenName(name) || isSkippedDir(name))
            continue;
        const QString full = dir + QLatin1Char('/') + name;
        const QFileInfo info(full);
        if (!info.exists())
            continue;
        // 跳过符号链接,避免环路
        if (info.isSymLink())
            continue;
        FileNode node;
        node.path = full;
        node.name = name;
        node.isDirectory = info.isDir();
        nodes.append(node);
    }

    // 目录在前,同类按名称排序
    std::sort(nodes.begin(), nodes.end(), [](const FileNode &a, const FileNode &b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return nodes;
}

void FileTreeModel::load(const QString &dir)
{
    if (m_childrenMap.contains(dir))
        return;
    (void)QtConcurrent::run([this, dir]() {
        const QList<FileNode> nodes = readDirectory(dir);
        QMetaObject::invokeMethod(this, [this, dir, nodes]() {
            m_childrenMap.insert(dir, nodes);
            emit childrenLoaded(dir);
        }, Qt::QueuedConnection);
    });
}

void FileTreeModel::toggle(const FileNode &node)
{
    if (!node.isDirectory)
        return;
    if (m_expanded.contains(node.path)) {
        m_expanded.remove(node.path);
    } else {
        m_expanded.insert(node.path);
        load(node.path);
    }
}

void FileTreeModel::reload()
{
    m_childrenMap.clear();
    m_expanded = QSet<QString>{ m_root };
    load(m_root);
}

void FileTreeModel::reveal(const QString &path)
{
    QString dir = QFileInfo(path).absolutePath();
    QStringList toExpand;
    while (dir.size() >= m_root.size() && dir.startsWith(m_root)) {
        toExpand.append(dir);
        const QString parent = QFileInfo(dir).absolutePath();
        if (parent == dir)
            break;
        dir = parent;
    }
    for (const QString &d : toExpand) {
        m_expanded.insert(d);
        load(d);
    }
}

QList<QPair<FileNode, int>> FileTreeModel::visibleRows() const
{
    QList<QPair<FileNode, int>> out;
    std::function<void(const QString &, int)> walk = [&](const QString &dir, int depth) {
        if (depth >= 14)
            return;
        for (const FileNode &node : children(dir)) {
            out.append(qMakePair(node, depth));
            if (node.isDirectory && m_expanded.contains(node.path))
                walk(node.path, depth + 1);
        }
    };
    walk(m_root, 0);
    return out;
}

void FileTreeModel::updateSearch(const QString &query)
{
    const QString q = query.trimmed().toLower();
    m_searchGen += 1;
    const int gen = m_searchGen;

    if (q.isEmpty()) {
        m_searchResults.clear();
        emit searchFinished();
        return;
    }

    const QString root = m_root;
    (void)QtConcurrent::run([this, root, q, gen]() {
        QList<FileNode> results;
        int visited = 0;

        std::function<void(const QString &, int)> walk = [&](const QString &dir, int depth) {
            if (depth >= 10 || visited >= 4000 || results.size() >= 200)
                return;
            QDir d(dir);
            const QStringList names = d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
            for (const QString &name : names) {
                visited += 1;
                if (isHiddenName(name) || isSkippedDir(name))
                    continue;
                const QString full = dir + QLatin1Char('/') + name;
                const QFileInfo info(full);
                if (!info.exists())
                    continue;
                if (info.isSymLink())
                    continue;
                if (info.isDir()) {
                    walk(full, depth + 1);
                } else if (name.toLower().contains(q)) {
                    FileNode node;
                    node.path = full;
                    node.name = name;
                    node.isDirectory = false;
                    results.append(node);
                }
            }
        };
        walk(root, 0);

        QMetaObject::invokeMethod(this, [this, results, gen]() {
            if (gen != m_searchGen)
                return;
            m_searchResults = results;
            emit searchFinished();
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// EditorModel

EditorModel::EditorModel(QObject *parent)
    : QObject(parent)
{
}

bool EditorModel::isDirty() const
{
    return !m_currentPath.isEmpty() && !m_binary && m_text != m_savedText;
}

QString EditorModel::fileName() const
{
    return m_currentPath.isEmpty() ? QString() : QFileInfo(m_currentPath).fileName();
}

QString EditorModel::relativePath() const
{
    if (m_currentPath.isEmpty())
        return QString();
    const QStringList parts = m_currentPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // 取最后三段,够识别
    QStringList tail;
    for (int i = qMax(0, parts.size() - 3); i < parts.size(); ++i)
        tail.append(parts[i]);
    return tail.join(QLatin1Char('/'));
}

void EditorModel::open(const QString &path)
{
    // 保存当前草稿
    if (!m_currentPath.isEmpty() && !m_binary)
        m_drafts.insert(m_currentPath, m_text);

    m_errorMessage.clear();
    m_binary = false;
    m_currentPath = path;

    // 恢复草稿
    if (m_drafts.contains(path)) {
        m_text = m_drafts.value(path);
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            m_savedText = QString::fromUtf8(file.readAll());
        else
            m_savedText = m_text;
        emit documentChanged();
        return;
    }

    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        m_errorMessage = QStringLiteral("无法读取文件");
        m_text.clear();
        m_savedText.clear();
        emit documentChanged();
        return;
    }
    if (info.size() > 2000000) {
        m_errorMessage = QStringLiteral("文件过大(%1 KB),不支持编辑").arg(info.size() / 1024);
        m_text.clear();
        m_savedText.clear();
        emit documentChanged();
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_errorMessage = QStringLiteral("无法读取文件");
        m_text.clear();
        m_savedText.clear();
        emit documentChanged();
        return;
    }
    const QByteArray raw = file.readAll();
    if (raw.contains('\0')) {
        m_errorMessage = QStringLiteral("无法以 UTF-8 文本打开,可能是二进制文件");
        m_binary = true;
        m_text.clear();
        m_savedText.clear();
        emit documentChanged();
        return;
    }
    m_text = QString::fromUtf8(raw);
    m_savedText = m_text;
    emit documentChanged();
}

void EditorModel::setText(const QString &text)
{
    if (!m_currentPath.isEmpty() && !m_binary)
        m_drafts.insert(m_currentPath, text);
    m_text = text;
    emit documentChanged();
}

bool EditorModel::save()
{
    if (m_currentPath.isEmpty() || m_binary)
        return false;
    QFile file(m_currentPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_errorMessage = QStringLiteral("保存失败: 无法写入 %1").arg(m_currentPath);
        emit documentChanged();
        return false;
    }
    file.write(m_text.toUtf8());
    file.close();
    m_savedText = m_text;
    m_drafts.remove(m_currentPath);
    emit documentChanged();
    return true;
}

void EditorModel::close()
{
    if (!m_currentPath.isEmpty() && !m_binary)
        m_drafts.insert(m_currentPath, m_text);
    m_currentPath.clear();
    m_text.clear();
    m_savedText.clear();
    m_errorMessage.clear();
    m_binary = false;
    emit documentChanged();
}

} // namespace jrunner
