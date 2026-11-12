#include "code_editor.h"

#include "theme.h"

#include <QKeyEvent>
#include <QPainter>
#include <QTextBlock>

namespace jrunner {

namespace {

/// 行号栏(对应 LineNumberRuler:背景稍作区分 + 右侧分隔线)
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor *editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor *m_editor;
};

} // namespace

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    // 纯文本编辑:关闭一切自动替换与拼写
    setPlainText(QString());
    QFont font(theme::monospaceFontFamily());
    font.setPointSize(11);
    font.setFixedPitch(true);
    setFont(font);
    setTabStopDistance(4 * fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setUndoRedoEnabled(true);

    lineNumberArea = new LineNumberArea(this);
    m_highlighter = new EditorSyntaxHighlighter(document());

    connect(this, &QPlainTextEdit::blockCountChanged, this,
            &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::textChanged, this, &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

void CodeEditor::setLanguage(SyntaxLanguage language)
{
    m_language = language;
    m_highlighter->setLanguage(language);
}

void CodeEditor::refreshTheme()
{
    m_highlighter->rehighlight();
    highlightCurrentLine();
}

int CodeEditor::lineCount() const
{
    return document()->blockCount();
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    qint64 max = qMax<qint64>(1, document()->blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    const int space = 13 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy) {
        lineNumberArea->scroll(0, dy);
    } else {
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
    }
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    // Tab 输入 4 空格,避免制表符混入配置文件
    if (event->key() == Qt::Key_Tab && !textCursor().hasSelection()) {
        insertPlainText(QStringLiteral("    "));
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(theme::primaryLight());
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    selections.append(selection);
    setExtraSelections(selections);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), theme::isDark() ? QColor(0x20, 0x22, 0x26) : QColor(0xf2, 0xf3, 0xf5));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setPen(theme::textTertiary());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const QString number = QString::number(blockNumber + 1);
            painter.drawText(0, top, lineNumberArea->width() - 8,
                             fontMetrics().height(), Qt::AlignRight, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

} // namespace jrunner
