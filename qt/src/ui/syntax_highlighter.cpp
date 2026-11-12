#include "syntax_highlighter.h"

#include "theme.h"

namespace jrunner {

SyntaxLanguage syntaxLanguageFromExtension(const QString &ext)
{
    const QString e = ext.toLower();
    if (e == QLatin1String("yml") || e == QLatin1String("yaml"))
        return SyntaxLanguage::Yaml;
    if (e == QLatin1String("properties") || e == QLatin1String("conf")
        || e == QLatin1String("env") || e == QLatin1String("ini"))
        return SyntaxLanguage::Properties;
    if (e == QLatin1String("java"))
        return SyntaxLanguage::Java;
    if (e == QLatin1String("kt") || e == QLatin1String("kts") || e == QLatin1String("gradle"))
        return SyntaxLanguage::Kotlin;
    if (e == QLatin1String("xml") || e == QLatin1String("html") || e == QLatin1String("htm")
        || e == QLatin1String("plist"))
        return SyntaxLanguage::Xml;
    if (e == QLatin1String("json"))
        return SyntaxLanguage::Json;
    if (e == QLatin1String("sql"))
        return SyntaxLanguage::Sql;
    if (e == QLatin1String("md") || e == QLatin1String("markdown"))
        return SyntaxLanguage::Markdown;
    if (e == QLatin1String("sh") || e == QLatin1String("bash") || e == QLatin1String("zsh"))
        return SyntaxLanguage::Shell;
    return SyntaxLanguage::Plain;
}

SyntaxTheme SyntaxTheme::forAppearance(bool dark)
{
    SyntaxTheme t;
    if (dark) {
        t.keyword = QColor(0xfa, 0x78, 0xa3);      // 粉
        t.string = QColor(0x9e, 0xde, 0x8f);       // 绿
        t.comment = QColor(0x78, 0x82, 0x8f);      // 灰
        t.number = QColor(0xde, 0xbd, 0x85);       // 沙
        t.type = QColor(0x73, 0xcc, 0xf5);         // 青
        t.annotation = QColor(0xd1, 0xa3, 0xfe);   // 紫
        t.key = QColor(0x82, 0xd1, 0xf5);
        t.punctuation = QColor(0x9e, 0xa6, 0xb0);
    } else {
        t.keyword = QColor(0xb8, 0x1a, 0x6b);
        t.string = QColor(0x21, 0x73, 0x29);
        t.comment = QColor(0x85, 0x8c, 0x94);
        t.number = QColor(0x99, 0x59, 0x0d);
        t.type = QColor(0x1a, 0x59, 0x9e);
        t.annotation = QColor(0x7a, 0x40, 0xb3);
        t.key = QColor(0x17, 0x52, 0x94);
        t.punctuation = QColor(0x59, 0x5e, 0x66);
    }
    return t;
}

// ---------------------------------------------------------------------------

EditorSyntaxHighlighter::EditorSyntaxHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    m_theme = SyntaxTheme::forAppearance(theme::isDark());
}

namespace {

QTextCharFormat tokenFormat(const QColor &color)
{
    QTextCharFormat format;
    format.setForeground(QBrush(color));
    return format;
}

} // namespace

QList<EditorSyntaxHighlighter::Rule> EditorSyntaxHighlighter::buildRules(SyntaxLanguage language) const
{
    const SyntaxTheme &t = m_theme;
    QList<Rule> rules;
    auto add = [&rules](const QString &pattern, const QColor &color,
                        QRegularExpression::PatternOptions options = {}) {
        Rule rule;
        rule.pattern = QRegularExpression(pattern, options);
        rule.format = tokenFormat(color);
        rules.append(rule);
    };

    switch (language) {
    case SyntaxLanguage::Yaml:
        add(QStringLiteral("^[ \\t]*#.*$"), t.comment);
        add(QStringLiteral("^[ \\t]*(?:-[ \\t]*)?([A-Za-z_][\\w.\\-]*)(?=[ \\t]*:)"), t.key);
        add(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'"), t.string);
        add(QStringLiteral("\\$\\{[^}]+\\}"), t.annotation);
        add(QStringLiteral("(?<![\\w.-])-?\\d+(?:\\.\\d+)?(?![\\w.-])"), t.number);
        add(QStringLiteral("\\b(?:true|false|null|yes|no|on|off)\\b"), t.keyword,
            QRegularExpression::CaseInsensitiveOption);
        break;
    case SyntaxLanguage::Properties:
        add(QStringLiteral("^[ \\t]*[#!].*$"), t.comment);
        add(QStringLiteral("^[ \\t]*([^=:\\s]+)[ \\t]*[=:]"), t.key);
        add(QStringLiteral("\\$\\{[^}]+\\}"), t.annotation);
        add(QStringLiteral("(?<![\\w.-])\\d+(?:\\.\\d+)?(?![\\w.-])"), t.number);
        add(QStringLiteral("\\b(?:true|false)\\b"), t.keyword);
        break;
    case SyntaxLanguage::Java:
    case SyntaxLanguage::Kotlin:
        add(QStringLiteral("//.*$"), t.comment);
        add(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\""), t.string);
        add(QStringLiteral("@[A-Za-z_]\\w*"), t.annotation);
        add(QStringLiteral("\\b(?:public|private|protected|internal|class|interface|object|enum|"
                           "fun|val|var|return|if|else|when|for|while|try|catch|finally|throw|new|"
                           "package|import|extends|implements|static|final|abstract|override|open|"
                           "data|suspend|inline|const|lateinit|companion|init|constructor|this|"
                           "super|null|true|false|void|int|long|double|float|boolean|char|byte|"
                           "short|String|Int|Long|Double|Float|Boolean|Any|Unit|List|Map|Set|"
                           "MutableList|MutableMap|Repository|Service|Controller|Component|"
                           "Configuration)\\b"),
            t.keyword);
        add(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*\\b"), t.type);
        add(QStringLiteral("(?<![\\w.])\\d+(?:\\.\\d+)?[fFlLdD]?(?![\\w.])"), t.number);
        break;
    case SyntaxLanguage::Xml:
        add(QStringLiteral("[A-Za-z_][\\w.:\\-]*(?==)"), t.key);
        add(QStringLiteral("\"[^\"\\n]*\""), t.string);
        add(QStringLiteral("</?[A-Za-z_][\\w.:\\-]*"), t.keyword);
        add(QStringLiteral("<\\?.*\\?>"), t.annotation);
        break;
    case SyntaxLanguage::Json:
        add(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"(?=\\s*:)"), t.key);
        add(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\""), t.string);
        add(QStringLiteral("(?<![\\w.])-?\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?(?![\\w.])"), t.number);
        add(QStringLiteral("\\b(?:true|false|null)\\b"), t.keyword);
        break;
    case SyntaxLanguage::Sql:
        add(QStringLiteral("--.*$"), t.comment);
        add(QStringLiteral("'(?:[^'\\\\]|\\\\.)*'"), t.string);
        add(QStringLiteral("\\b(?:SELECT|FROM|WHERE|INSERT|INTO|VALUES|UPDATE|SET|DELETE|CREATE|"
                           "TABLE|INDEX|ALTER|DROP|JOIN|LEFT|RIGHT|INNER|OUTER|ON|GROUP|BY|ORDER|"
                           "HAVING|LIMIT|OFFSET|AS|AND|OR|NOT|NULL|IS|IN|EXISTS|DISTINCT|COUNT|"
                           "SUM|AVG|MAX|MIN|CASE|WHEN|THEN|ELSE|END|PRIMARY|KEY|FOREIGN|"
                           "REFERENCES|DEFAULT|UNIQUE)\\b"),
            t.keyword, QRegularExpression::CaseInsensitiveOption);
        add(QStringLiteral("(?<![\\w.])\\d+(?:\\.\\d+)?(?![\\w.])"), t.number);
        break;
    case SyntaxLanguage::Markdown:
        add(QStringLiteral("^#{1,6}[ \\t].*$"), t.keyword);
        add(QStringLiteral("`[^`\\n]+`"), t.string);
        add(QStringLiteral("^[ \\t]*[-*+][ \\t]"), t.annotation);
        add(QStringLiteral("\\[[^\\]]*\\]\\([^)]*\\)"), t.type);
        add(QStringLiteral("^>.*$"), t.comment);
        break;
    case SyntaxLanguage::Shell:
        add(QStringLiteral("^[ \\t]*#.*$"), t.comment);
        add(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'[^']*'"), t.string);
        add(QStringLiteral("\\$\\{?\\w+\\}?"), t.annotation);
        add(QStringLiteral("\\b(?:if|then|else|fi|for|do|done|while|case|esac|function|return|"
                           "export|local|echo|cd|exit)\\b"),
            t.keyword);
        break;
    case SyntaxLanguage::Plain:
        break;
    }
    return rules;
}

bool EditorSyntaxHighlighter::hasMultilineComment(SyntaxLanguage language) const
{
    switch (language) {
    case SyntaxLanguage::Java:
    case SyntaxLanguage::Kotlin:
    case SyntaxLanguage::Sql:
    case SyntaxLanguage::Xml:
        return true;
    default:
        return false;
    }
}

void EditorSyntaxHighlighter::setLanguage(SyntaxLanguage language)
{
    if (m_language == language)
        return;
    m_language = language;
    switch (language) {
    case SyntaxLanguage::Java:
    case SyntaxLanguage::Kotlin:
    case SyntaxLanguage::Sql:
        m_commentMarker = CommentMarker::CStyle;
        break;
    case SyntaxLanguage::Xml:
        m_commentMarker = CommentMarker::Xml;
        break;
    default:
        m_commentMarker = CommentMarker::None;
        break;
    }
    m_rules = buildRules(language);
    rehighlight();
}

namespace {
constexpr int kInCommentState = 1;
constexpr int kNormalState = 0;
constexpr int kMaxHighlightChars = 400000;    // 超大文件不上色,避免卡顿
} // namespace

void EditorSyntaxHighlighter::highlightBlock(const QString &text)
{
    // 超大文件跳过着色
    if (document()->characterCount() > kMaxHighlightChars) {
        setCurrentBlockState(kNormalState);
        return;
    }

    // 多行注释状态机(/* */ 与 <!-- -->)
    if (m_commentMarker != CommentMarker::None) {
        const QString open = m_commentMarker == CommentMarker::Xml
            ? QStringLiteral("<!--")
            : QStringLiteral("/*");
        const QString close = m_commentMarker == CommentMarker::Xml
            ? QStringLiteral("-->")
            : QStringLiteral("*/");
        int pos = 0;
        int start = 0;
        bool inComment = previousBlockState() == kInCommentState;
        if (!inComment) {
            start = text.indexOf(open);
            pos = start < 0 ? text.size() : start + open.size();
        }
        while (start >= 0 && pos <= text.size()) {
            const int end = text.indexOf(close, pos);
            if (end < 0) {
                // 注释延续到块尾
                setFormat(inComment ? 0 : start, text.size() - (inComment ? 0 : start),
                          tokenFormat(m_theme.comment));
                setCurrentBlockState(kInCommentState);
                return;
            }
            const int from = inComment ? 0 : start;
            setFormat(from, end + close.size() - from, tokenFormat(m_theme.comment));
            inComment = false;
            const int next = text.indexOf(open, end + close.size());
            if (next < 0)
                break;
            start = next;
            pos = next + open.size();
        }
        setCurrentBlockState(kNormalState);
    }

    // 单行规则(后应用的规则覆盖先应用的,与 macOS 版优先级一致)
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }
}

} // namespace jrunner
