#include "theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>

namespace jrunner {
namespace theme {

namespace {

bool g_dark = false;
ThemeMode g_mode = ThemeMode::System;

/// 明/暗双色板(取自 macOS 版 DesignSystem.swift 的 sRGB 值)
QColor dynamic(const QColor &light, const QColor &dark)
{
    return g_dark ? dark : light;
}

bool systemPrefersDark()
{
    if (const auto *hints = QGuiApplication::styleHints())
        return hints->colorScheme() == Qt::ColorScheme::Dark;
    const QPalette pal = QGuiApplication::palette();
    return pal.color(QPalette::Window).lightness() < 128;
}

QPalette buildPalette(bool dark)
{
    QPalette pal;
    const QColor window = dark ? bg() : bg();
    const QColor base = dark ? QColor(0x17, 0x1a, 0x20) : QColor(0xff, 0xff, 0xff);
    const QColor text = dark ? textPrimary() : textPrimary();
    const QColor textDisabled = dark ? QColor(0x6a, 0x6f, 0x78) : QColor(0xa0, 0xa3, 0xa8);
    const QColor button = dark ? card() : QColor(0xfa, 0xfa, 0xfb);
    const QColor highlight = dark ? QColor(0x24, 0x47, 0x77) : QColor(0xcf, 0xe2, 0xf9);
    const QColor highlightText = text;
    const QColor link = primary();

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, dark ? QColor(0x1a, 0x1e, 0x24) : QColor(0xf5, 0xf6, 0xf8));
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightText);
    pal.setColor(QPalette::Link, link);
    pal.setColor(QPalette::ToolTipBase, dark ? QColor(0x24, 0x27, 0x2e) : QColor(0xff, 0xff, 0xff));
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::PlaceholderText, textDisabled);
    pal.setColor(QPalette::Disabled, QPalette::Text, textDisabled);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled);
    pal.setColor(QPalette::PlaceholderText, textDisabled);
    return pal;
}

} // namespace

QString themeModeLabel(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::System: return QStringLiteral("跟随系统");
    case ThemeMode::Light: return QStringLiteral("浅色");
    case ThemeMode::Dark: return QStringLiteral("深色");
    }
    return QStringLiteral("跟随系统");
}

QString themeModeKey(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::System: return QStringLiteral("system");
    case ThemeMode::Light: return QStringLiteral("light");
    case ThemeMode::Dark: return QStringLiteral("dark");
    }
    return QStringLiteral("system");
}

ThemeMode themeModeFromKey(const QString &key)
{
    if (key == QLatin1String("light"))
        return ThemeMode::Light;
    if (key == QLatin1String("dark"))
        return ThemeMode::Dark;
    return ThemeMode::System;
}

ThemeMode storedThemeMode()
{
    QSettings settings;
    return themeModeFromKey(settings.value(QStringLiteral("appearance/mode")).toString());
}

void storeThemeMode(ThemeMode mode)
{
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/mode"), themeModeKey(mode));
}

void applyTheme(ThemeMode mode)
{
    g_mode = mode;
    g_dark = mode == ThemeMode::Dark ? true : (mode == ThemeMode::Light ? false : systemPrefersDark());

    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        app->setPalette(buildPalette(g_dark));
        app->setStyleSheet(appStyleSheet());
    }
}

bool isDark()
{
    return g_dark;
}

ThemeMode currentMode()
{
    return g_mode;
}

QColor primary()
{
    return dynamic(QColor(0x17, 0x73, 0xd1), QColor(0x4a, 0x99, 0xfa));
}

QColor primaryLight()
{
    return dynamic(QColor(0xed, 0xf5, 0xff), QColor(0x24, 0x3a, 0x5c));
}

QColor success()
{
    return dynamic(QColor(0x0f, 0x9e, 0x59), QColor(0x47, 0xcc, 0x84));
}

QColor warning()
{
    return dynamic(QColor(0xd9, 0x80, 0x0f), QColor(0xfa, 0xb3, 0x42));
}

QColor danger()
{
    return dynamic(QColor(0xcc, 0x30, 0x30), QColor(0xf5, 0x66, 0x61));
}

QColor purple()
{
    return dynamic(QColor(0x82, 0x4f, 0xc9), QColor(0xbd, 0x91, 0xfa));
}

QColor bg()
{
    return dynamic(QColor(0xf6, 0xf7, 0xf9), QColor(0x1b, 0x1c, 0x1f));
}

QColor card()
{
    return dynamic(QColor(0xff, 0xff, 0xff), QColor(0x26, 0x28, 0x2b));
}

QColor border()
{
    return dynamic(QColor(0xde, 0xe0, 0xe6), QColor(0x41, 0x44, 0x49));
}

QColor textPrimary()
{
    return dynamic(QColor(0x1c, 0x1f, 0x21), QColor(0xed, 0xf0, 0xf2));
}

QColor textSecondary()
{
    return dynamic(QColor(0x6b, 0x73, 0x7d), QColor(0xb0, 0xb5, 0xbd));
}

QColor textTertiary()
{
    return dynamic(QColor(0x9e, 0xa6, 0xae), QColor(0x85, 0x8c, 0x96));
}

QColor logLevelColor(LogLevel level)
{
    switch (level) {
    case LogLevel::Error: return danger();
    case LogLevel::Warn: return warning();
    case LogLevel::Info: return textSecondary();
    case LogLevel::Debug: return textTertiary();
    case LogLevel::Plain: return textPrimary();
    }
    return textPrimary();
}

QString logLevelLabel(LogLevel level)
{
    switch (level) {
    case LogLevel::Error: return QStringLiteral("错误");
    case LogLevel::Warn: return QStringLiteral("警告");
    case LogLevel::Info: return QStringLiteral("信息");
    case LogLevel::Debug: return QStringLiteral("调试");
    case LogLevel::Plain: return QStringLiteral("输出");
    }
    return QStringLiteral("输出");
}

QColor taskStateColor(const TaskState &state)
{
    switch (state.kind) {
    case TaskState::Idle: return textTertiary();
    case TaskState::Starting:
    case TaskState::Running: return success();
    case TaskState::Stopping: return warning();
    case TaskState::Success: return textTertiary();   // 已结束/已停止 = 非运行,灰色
    case TaskState::Failed: return danger();
    }
    return textTertiary();
}

QString taskStateLabel(const TaskState &state)
{
    switch (state.kind) {
    case TaskState::Idle: return QStringLiteral("未运行");
    case TaskState::Starting: return QStringLiteral("启动中");
    case TaskState::Running: return QStringLiteral("运行中");
    case TaskState::Stopping: return QStringLiteral("停止中");
    case TaskState::Success: return QStringLiteral("已结束");
    case TaskState::Failed: return QStringLiteral("失败:%1").arg(state.failureMessage);
    }
    return QString();
}

QString monospaceFontFamily()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

QString appStyleSheet()
{
    const QColor cBg = bg(), cCard = card(), cBorder = border();
    const QColor cText = textPrimary(), cText2 = textSecondary(), cText3 = textTertiary();
    const QColor cPrimary = primary(), cDanger = danger(), cSuccess = success();
    const QString mono = monospaceFontFamily();

    return QStringLiteral(R"(
/* ---- 基础 ---- */
QMainWindow, QDialog { background: %1; }
QWidget#jrSidePanel, QWidget#jrMiddlePanel, QWidget#jrRightPanel { background: %2; }
QLabel { color: %4; }
QLabel[tone="secondary"] { color: %5; }
QLabel[tone="tertiary"] { color: %6; }
QLabel[tone="error"] { color: %8; }
QLabel[tone="success"] { color: %9; }
QLabel[tone="primary"] { color: %7; }

/* ---- 按钮 ---- */
QPushButton {
    background: %2; color: %4;
    border: 1px solid %3; border-radius: 6px;
    padding: 5px 12px; font-size: 12.5px;
}
QPushButton:hover { border-color: %7; color: %7; }
QPushButton:pressed { background: %10; }
QPushButton:disabled { color: %6; border-color: %3; }
QPushButton[role="primary"] {
    background: %7; color: #ffffff; border: none; font-weight: 600;
}
QPushButton[role="primary"]:hover { background: %11; }
QPushButton[role="primary"]:pressed { background: %11; }
QPushButton[role="primary"]:disabled { background: %3; color: #ffffff; }
QPushButton[role="success"] {
    background: %9; color: #ffffff; border: none; font-weight: 600;
}
QPushButton[role="success"]:hover { background: %9; }
QPushButton[role="danger"] {
    background: transparent; color: %8; border: 1px solid %8;
}
QPushButton[role="flat"] { background: transparent; border: none; color: %5; }
QPushButton[role="flat"]:hover { color: %7; background: %10; }
QPushButton[iconOnly="true"] { padding: 2px; }
QPushButton[iconOnly="true"]:checked { background: %10; border: 1px solid %7; }

/* ---- 输入 ---- */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox {
    background: %2; color: %4;
    border: 1px solid %3; border-radius: 6px;
    padding: 4px 8px; selection-background-color: %7;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: %7; }
QLineEdit[mono="true"] { font-family: "%12"; }

/* ---- 列表 / 树 ---- */
QListWidget, QTreeView, QTreeView::item {
    background: transparent; border: none; color: %4; outline: none;
}
QListWidget::item, QTreeView::item { padding: 3px 4px; border-radius: 4px; }
QListWidget::item:hover, QTreeView::item:hover { background: %10; }
QListWidget::item:selected, QTreeView::item:selected { background: %7; color: #ffffff; }
QTreeView::branch { background: transparent; }
QHeaderView::section {
    background: %2; color: %5; border: none;
    border-bottom: 1px solid %3; padding: 4px 8px; font-size: 11px;
}

/* ---- 菜单 / 下拉 ---- */
QMenu { background: %2; border: 1px solid %3; border-radius: 8px; padding: 4px; }
QMenu::item { color: %4; padding: 5px 24px 5px 12px; border-radius: 5px; }
QMenu::item:selected { background: %10; color: %4; }
QMenu::item:disabled { color: %6; }
QMenu::separator { height: 1px; background: %3; margin: 4px 8px; }
QComboBox {
    background: %2; color: %4; border: 1px solid %3;
    border-radius: 6px; padding: 3px 18px 3px 6px;
}
QComboBox:hover { border-color: %7; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background: %2; color: %4; border: 1px solid %3;
    selection-background-color: %10; selection-color: %4;
}

/* ---- 滚动条 ---- */
QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }
QScrollBar::handle:vertical {
    background: %3; border-radius: 4px; min-height: 28px;
}
QScrollBar::handle:vertical:hover { background: %6; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 2px; }
QScrollBar::handle:horizontal { background: %3; border-radius: 4px; min-width: 28px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- 分隔线 / 工具栏 ---- */
QFrame[line="true"] { color: %3; }
QToolBar, QMenuBar { background: %1; color: %4; }
QMenuBar::item { padding: 4px 8px; border-radius: 4px; }
QMenuBar::item:selected { background: %10; }
QStatusBar { background: %1; color: %5; border-top: 1px solid %3; }
QStatusBar::item { border: none; }
QSplitter::handle { background: %3; }
QSplitter::handle:hover { background: %7; }
QToolTip { background: %2; color: %4; border: 1px solid %3; padding: 4px 8px; }

/* ---- 日志 / 徽章 ---- */
QPlainTextEdit#jrLogView {
    font-family: "%12"; font-size: 12px;
    background: %1; border: none; selection-background-color: %7;
}
QLabel#jrBadge {
    font-size: 10px; font-weight: 600;
    padding: 2px 8px; border-radius: 8px;
}
QLabel[jrCard="true"] {
    background: %2; border: 1px solid %3; border-radius: 10px;
}
)")
        .arg(cBg.name(), cCard.name(), cBorder.name(), cText.name(),
             cText2.name(), cText3.name(), cPrimary.name(),
             cDanger.name(QColor::HexArgb), cSuccess.name())
        .arg(primaryLight().name(),                      // %10 hover 底色
             cPrimary.darker(110).name(),                // %11 primary hover
             mono);                                      // %12 等宽字体
}

} // namespace theme
} // namespace jrunner
