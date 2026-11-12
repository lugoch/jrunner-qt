#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class QTextDocument;

namespace jrunner {

// 【语法高亮】对应 macOS 版 CodeEditor.swift 中的 SyntaxLanguage / SyntaxTheme /
// SyntaxHighlighter:10 种语言的正则规则 + 明暗双主题配色。

enum class SyntaxLanguage {
    Yaml,
    Properties,
    Java,
    Kotlin,
    Xml,
    Json,
    Sql,
    Markdown,
    Shell,
    Plain,
};

SyntaxLanguage syntaxLanguageFromExtension(const QString &ext);

/// 高亮配色(随外观切换)
struct SyntaxTheme {
    QColor keyword;
    QColor string;
    QColor comment;
    QColor number;
    QColor type;
    QColor annotation;
    QColor key;
    QColor punctuation;

    static SyntaxTheme forAppearance(bool dark);
};

class EditorSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit EditorSyntaxHighlighter(QTextDocument *document);
    void setLanguage(SyntaxLanguage language);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<Rule> buildRules(SyntaxLanguage language) const;
    bool hasMultilineComment(SyntaxLanguage language) const;

    SyntaxLanguage m_language = SyntaxLanguage::Plain;
    QList<Rule> m_rules;
    enum class CommentMarker {
        None,
        CStyle,      // /* ... */(java/kotlin/sql)
        Xml,         // <!-- ... -->
    };
    CommentMarker m_commentMarker = CommentMarker::None;
    SyntaxTheme m_theme;
};

} // namespace jrunner
