#pragma once

#include <QPlainTextEdit>

#include "syntax_highlighter.h"

class QWidget;

namespace jrunner {

// 【代码编辑器】对应 macOS 版 CodeEditor.swift:QPlainTextEdit + 行号栏 + 语法高亮。
// 等宽字体、无换行替换、撤销可用;Ctrl+F 由编辑面板提供查找栏。

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    void setLanguage(SyntaxLanguage language);
    SyntaxLanguage language() const { return m_language; }
    int lineCount() const;

    void refreshTheme();

    // 行号栏绘制(由 LineNumberArea 调用)
    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth() const;

signals:
    void contentEdited();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();

private:
    QWidget *lineNumberArea;
    EditorSyntaxHighlighter *m_highlighter = nullptr;
    SyntaxLanguage m_language = SyntaxLanguage::Plain;
};

} // namespace jrunner
