#include "file_browser.h"

#include "code_editor.h"
#include "theme.h"
#include "widgets.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

namespace jrunner {

// ---------------------------------------------------------------------------
// FileBrowserPanel

FileBrowserPanel::FileBrowserPanel(AppStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("jrMiddlePanel"));
    setMinimumWidth(260);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(12, 9, 12, 9);
    searchRow->setSpacing(8);
    auto *icon = new QLabel(QStringLiteral("⌕"), this);
    icon->setStyleSheet(
        QStringLiteral("color: %1; font-size: 13px;").arg(theme::textTertiary().name()));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索文件"));
    m_searchEdit->setFrame(false);
    m_searchEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; font-size: 12px; }"));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString q = text.trimmed();
        if (q.isEmpty()) {
            m_inSearch = false;
            rebuildRows();
        } else {
            applySearch(q);
        }
    });
    m_refreshButton = makeIconButton(QStringLiteral("view-refresh"), QStyle::SP_BrowserReload,
                                     QStringLiteral("刷新"), this);
    m_refreshButton->setProperty("role", "flat");
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        if (m_model) {
            m_model->reload();
            rebuildRows();
        }
    });
    searchRow->addWidget(icon);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_refreshButton);
    layout->addLayout(searchRow);

    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::border().name(QColor::HexArgb)));
    layout->addWidget(divider);

    m_list = new QListWidget(this);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        const bool isDir = item->data(Qt::UserRole + 1).toBool();
        if (isDir && !m_inSearch) {
            FileNode node;
            node.path = path;
            node.name = QFileInfo(path).fileName();
            node.isDirectory = true;
            m_model->toggle(node);
            rebuildRows();
        } else if (!isDir) {
            emit fileOpenRequested(path);
        }
    });
    layout->addWidget(m_list, 1);
}

void FileBrowserPanel::setProject(const QString &rootPath)
{
    if (m_model && m_model->root() == rootPath)
        return;
    delete m_model;
    m_model = new FileTreeModel(rootPath, this);
    connect(m_model, &FileTreeModel::childrenLoaded, this, [this]() {
        if (!m_inSearch)
            rebuildRows();
    });
    connect(m_model, &FileTreeModel::searchFinished, this, [this]() {
        if (m_inSearch)
            rebuildSearchResults();
    });
    m_inSearch = false;
    m_searchEdit->clear();
    rebuildRows();
}

namespace {

/// 扩展名 → 字形与颜色(对应 FileNode.icon 扩展)
struct FileGlyph {
    QString glyph;
    QColor color;
};

FileGlyph glyphForNode(const FileNode &node)
{
    const QString ext = node.ext();
    if (node.isDirectory)
        return { QStringLiteral("▸"), theme::textSecondary() };
    if (ext == QLatin1String("kt") || ext == QLatin1String("kts"))
        return { QStringLiteral("K"), theme::purple() };
    if (ext == QLatin1String("java"))
        return { QStringLiteral("J"), QColor(0xd0, 0x7a, 0x2e) };
    if (ext == QLatin1String("yaml") || ext == QLatin1String("yml"))
        return { QStringLiteral("Y"), theme::primary() };
    if (ext == QLatin1String("xml"))
        return { QStringLiteral("X"), QColor(0xd0, 0x7a, 0x2e) };
    if (ext == QLatin1String("properties") || ext == QLatin1String("env")
        || ext == QLatin1String("conf") || ext == QLatin1String("ini"))
        return { QStringLiteral("P"), theme::textSecondary() };
    if (ext == QLatin1String("json"))
        return { QStringLiteral("{}"), QColor(0xc9, 0xa2, 0x28) };
    if (ext == QLatin1String("gradle"))
        return { QStringLiteral("G"), theme::success() };
    if (ext == QLatin1String("sql"))
        return { QStringLiteral("S"), theme::primary() };
    if (ext == QLatin1String("md"))
        return { QStringLiteral("M"), theme::primary() };
    if (ext == QLatin1String("sh") || ext == QLatin1String("bash"))
        return { QStringLiteral("$"), theme::success() };
    if (ext == QLatin1String("jar"))
        return { QStringLiteral("📦"), QColor(0x8d, 0x6e, 0x63) };
    if (ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")
        || ext == QLatin1String("gif") || ext == QLatin1String("svg"))
        return { QStringLiteral("🖼"), theme::textTertiary() };
    return { QStringLiteral("·"), theme::textTertiary() };
}

} // namespace

QString FileBrowserPanel::fileGlyph(const FileNode &node, QColor *color) const
{
    const FileGlyph g = glyphForNode(node);
    if (color)
        *color = g.color;
    return g.glyph;
}

void FileBrowserPanel::applySearch(const QString &query)
{
    m_inSearch = true;
    if (m_model)
        m_model->updateSearch(query);
}

void FileBrowserPanel::refreshTheme()
{
    rebuildRows();
}

void FileBrowserPanel::rebuildRows()
{
    m_list->clear();
    if (!m_model)
        return;

    const auto rows = m_model->visibleRows();
    for (const auto &entry : rows) {
        const FileNode &node = entry.first;
        const int depth = entry.second;

        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(7);

        QColor color;
        QString glyph = fileGlyph(node, &color);
        const bool expanded = node.isDirectory && m_model->isExpanded(node.path);
        if (expanded)
            glyph = QStringLiteral("▾");

        auto *iconLabel = new QLabel(glyph, row);
        iconLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 10px; font-weight: 600; border: none;")
                .arg(color.name()));
        iconLabel->setFixedWidth(14);
        auto *nameLabel = new QLabel(node.name, row);
        nameLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px; border: none;")
                .arg(theme::textPrimary().name()));
        layout->addWidget(iconLabel);
        layout->addWidget(nameLabel, 1);

        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, node.path);
        item->setData(Qt::UserRole + 1, node.isDirectory);
        m_list->addItem(item);
        m_list->setItemWidget(item, row);
        item->setSizeHint(QSize(0, 24));
        // 深度缩进
        item->setTextAlignment(Qt::AlignLeft);
        row->setContentsMargins(depth * 14, 0, 0, 0);
    }
}

void FileBrowserPanel::rebuildSearchResults()
{
    m_list->clear();
    if (!m_model)
        return;

    const QList<FileNode> results = m_model->searchResults();
    if (results.isEmpty()) {
        auto *empty = new EmptyStateView(QStringLiteral("⌕"), QStringLiteral("无匹配文件"),
                                         QStringLiteral("换个关键词试试"), this);
        auto *item = new QListWidgetItem(m_list);
        item->setFlags(Qt::NoItemFlags);
        m_list->addItem(item);
        m_list->setItemWidget(item, empty);
        m_list->item(0)->setSizeHint(empty->sizeHint());
        return;
    }

    const QString root = m_model->root();
    for (const FileNode &node : results) {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(7);
        QColor color;
        const QString glyph = fileGlyph(node, &color);
        auto *iconLabel = new QLabel(glyph, row);
        iconLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 10px; font-weight: 600; border: none;")
                .arg(color.name()));
        iconLabel->setFixedWidth(14);
        auto *nameLabel = new QLabel(node.name, row);
        nameLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px; border: none;")
                .arg(theme::textPrimary().name()));
        // 相对目录副标题
        QString relDir;
        if (node.path.startsWith(root + QLatin1Char('/')))
            relDir = node.path.mid(root.size() + 1);
        relDir = QFileInfo(relDir).path();
        layout->addWidget(iconLabel);
        layout->addWidget(nameLabel);
        if (!relDir.isEmpty() && relDir != QLatin1String(".")) {
            auto *dirLabel = new QLabel(relDir, row);
            dirLabel->setStyleSheet(
                QStringLiteral("color: %1; font-size: 9px; border: none;")
                    .arg(theme::textTertiary().name()));
            layout->addWidget(dirLabel, 1);
        } else {
            layout->addStretch(1);
        }

        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, node.path);
        item->setData(Qt::UserRole + 1, false);
        m_list->addItem(item);
        m_list->setItemWidget(item, row);
        item->setSizeHint(QSize(0, 24));
    }
}

// ---------------------------------------------------------------------------
// FileEditorPanel

FileEditorPanel::FileEditorPanel(AppStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("jrRightPanel"));
    setMinimumWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 工具栏
    m_toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(m_toolbar);
    toolbarLayout->setContentsMargins(12, 7, 12, 7);
    toolbarLayout->setSpacing(8);

    m_dirtyDot = new QLabel(QStringLiteral("●"), m_toolbar);
    m_dirtyDot->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::warning().name()));
    m_dirtyDot->hide();
    m_fileName = new QLabel(m_toolbar);
    m_fileName->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-weight: 600;")
                                  .arg(theme::textPrimary().name()));
    m_relativePath = makeMonoLabel(QString(), m_toolbar);
    m_relativePath->setStyleSheet(
        QStringLiteral("color: %1; font-size: 9px;").arg(theme::textTertiary().name()));
    m_lineCount = new QLabel(m_toolbar);
    m_lineCount->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    toolbarLayout->addWidget(m_dirtyDot);
    toolbarLayout->addWidget(m_fileName);
    toolbarLayout->addWidget(m_relativePath);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(m_lineCount);

    m_saveButton = new QPushButton(QStringLiteral("保存"), m_toolbar);
    m_saveButton->setProperty("role", "primary");
    m_saveButton->setToolTip(QStringLiteral("保存 (Ctrl+S)"));
    connect(m_saveButton, &QPushButton::clicked, this, [this]() { saveFile(); });
    m_closeButton = makeIconButton(QStringLiteral("window-close"), QStyle::SP_DialogCloseButton,
                                   QStringLiteral("关闭文件"), m_toolbar);
    m_closeButton->setProperty("role", "flat");
    connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        m_store->editor()->close();
        refreshDocument();
    });
    toolbarLayout->addWidget(m_saveButton);
    toolbarLayout->addWidget(m_closeButton);
    layout->addWidget(m_toolbar);

    // 查找栏(默认隐藏)
    m_findBar = new QWidget(this);
    auto *findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(12, 4, 12, 4);
    findLayout->setSpacing(6);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(QStringLiteral("查找"));
    m_findEdit->setFixedWidth(200);
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() { findNext(); });
    auto *prevButton = makeIconButton(QStringLiteral("go-up"), QStyle::SP_ArrowUp,
                                      QStringLiteral("上一个匹配"), m_findBar);
    prevButton->setProperty("role", "flat");
    connect(prevButton, &QPushButton::clicked, this, [this]() { findPrevious(); });
    auto *nextButton = makeIconButton(QStringLiteral("go-down"), QStyle::SP_ArrowDown,
                                      QStringLiteral("下一个匹配"), m_findBar);
    nextButton->setProperty("role", "flat");
    connect(nextButton, &QPushButton::clicked, this, [this]() { findNext(); });
    auto *hideButton = makeIconButton(QStringLiteral("window-close"), QStyle::SP_DialogCloseButton,
                                      QStringLiteral("关闭查找"), m_findBar);
    hideButton->setProperty("role", "flat");
    connect(hideButton, &QPushButton::clicked, this, [this]() { m_findBar->hide(); });
    findLayout->addWidget(m_findEdit);
    findLayout->addWidget(prevButton);
    findLayout->addWidget(nextButton);
    findLayout->addStretch(1);
    findLayout->addWidget(hideButton);
    m_findBar->hide();
    layout->addWidget(m_findBar);

    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::border().name(QColor::HexArgb)));
    layout->addWidget(divider);

    // 错误条
    m_errorStrip = new QWidget(this);
    m_errorStrip->setStyleSheet(
        QStringLiteral("QWidget { background: %1; }")
            .arg(Badge::hexWithAlpha(theme::warning(), 0.10)));
    auto *errorLayout = new QHBoxLayout(m_errorStrip);
    errorLayout->setContentsMargins(12, 6, 12, 6);
    m_errorLabel = new QLabel(m_errorStrip);
    m_errorLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::warning().name()));
    errorLayout->addWidget(m_errorLabel);
    m_errorStrip->hide();
    layout->addWidget(m_errorStrip);

    // 编辑器
    m_editor = new CodeEditor(this);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() {
        m_store->editor()->setText(m_editor->toPlainText());
        refreshDocument();
    });
    auto *editorWrap = new QWidget(this);
    auto *wrapLayout = new QVBoxLayout(editorWrap);
    wrapLayout->setContentsMargins(0, 0, 0, 0);
    wrapLayout->addWidget(m_editor);
    layout->addWidget(editorWrap, 1);

    // 空状态遮罩(未打开文件时)
    auto *empty = new EmptyStateView(
        QStringLiteral("✎"),
        QStringLiteral("未打开文件"),
        QStringLiteral("从中间的文件树选择一个文件进行编辑\n支持 yaml / properties / java / kotlin 等语法高亮"),
        this);
    layout->addWidget(empty, 1);
    empty->raise();

    connect(m_store->editor(), &EditorModel::documentChanged, this, [this, empty]() {
        refreshDocument();
        empty->setVisible(m_store->editor()->currentPath().isEmpty());
    });
    QTimer::singleShot(0, this, [this, empty]() {
        empty->setVisible(m_store->editor()->currentPath().isEmpty());
    });
}

void FileEditorPanel::refreshTheme()
{
    m_dirtyDot->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;").arg(theme::warning().name()));
    m_fileName->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-weight: 600;")
                                  .arg(theme::textPrimary().name()));
    m_relativePath->setStyleSheet(
        QStringLiteral("color: %1; font-size: 9px;").arg(theme::textTertiary().name()));
    m_lineCount->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    m_editor->refreshTheme();
}

void FileEditorPanel::refreshDocument()
{
    EditorModel *editor = m_store->editor();
    const bool hasFile = !editor->currentPath().isEmpty();
    m_fileName->setText(editor->fileName());
    m_relativePath->setText(editor->relativePath());
    const bool dirty = editor->isDirty();
    m_dirtyDot->setVisible(hasFile && dirty);
    m_lineCount->setText(hasFile ? QStringLiteral("%1 行").arg(m_editor->lineCount())
                                 : QString());
    m_saveButton->setEnabled(hasFile && dirty);
    m_closeButton->setEnabled(hasFile);

    // 错误条
    if (!editor->errorMessage().isEmpty()) {
        m_errorLabel->setText(editor->errorMessage());
        m_errorStrip->show();
    } else {
        m_errorStrip->hide();
    }

    // 同步编辑器文本(外部打开/切换文件)
    if (m_editor->toPlainText() != editor->text()) {
        m_editor->setPlainText(editor->text());
    }
    // 语言跟随扩展名
    const SyntaxLanguage lang = syntaxLanguageFromExtension(
        QFileInfo(editor->currentPath()).suffix());
    if (lang != m_editor->language())
        m_editor->setLanguage(lang);
}

bool FileEditorPanel::saveFile()
{
    EditorModel *editor = m_store->editor();
    if (editor->isDirty()) {
        editor->setText(m_editor->toPlainText());
        const bool ok = editor->save();
        if (ok)
            m_store->refreshGit(true);
        refreshDocument();
        return ok;
    }
    return false;
}

void FileEditorPanel::showFindBar()
{
    m_findBar->show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void FileEditorPanel::findNext()
{
    const QString text = m_findEdit->text();
    if (text.isEmpty())
        return;
    if (!m_editor->find(text)) {
        // 回绕查找
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        m_editor->setTextCursor(cursor);
        m_editor->find(text);
    }
}

void FileEditorPanel::findPrevious()
{
    const QString text = m_findEdit->text();
    if (text.isEmpty())
        return;
    if (!m_editor->find(text, QTextDocument::FindBackward)) {
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_editor->setTextCursor(cursor);
        m_editor->find(text, QTextDocument::FindBackward);
    }
}

} // namespace jrunner
