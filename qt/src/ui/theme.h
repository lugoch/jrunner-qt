#pragma once

#include <QColor>
#include <QString>

#include "core/process_task.h"

class QGuiApplication;

namespace jrunner {
namespace theme {

// 【主题系统】对应 macOS 版 DesignSystem.swift / Appearance.swift。
// 语义色 token(明/暗双色板) + 生成式 QSS + 显式 QPalette,运行时切换。

/// 外观模式:深色 / 浅色 / 跟随系统
enum class ThemeMode {
    System,
    Light,
    Dark,
};

QString themeModeLabel(ThemeMode mode);
ThemeMode themeModeFromKey(const QString &key);
QString themeModeKey(ThemeMode mode);
ThemeMode storedThemeMode();
void storeThemeMode(ThemeMode mode);

/// 应用主题:设置调色板与全局样式表,并记录当前模式
void applyTheme(ThemeMode mode);

/// 当前生效是否为深色(System 时取系统实际外观)
bool isDark();

/// 当前生效的主题模式
ThemeMode currentMode();

// ---- 语义色 token -------------------------------------------------------

QColor primary();
QColor primaryLight();
QColor success();
QColor warning();
QColor danger();
QColor purple();
QColor bg();
QColor card();
QColor border();
QColor textPrimary();
QColor textSecondary();
QColor textTertiary();

// ---- 日志级别配色 -------------------------------------------------------

QColor logLevelColor(LogLevel level);
QString logLevelLabel(LogLevel level);

// ---- 任务状态配色 -------------------------------------------------------

QColor taskStateColor(const TaskState &state);
QString taskStateLabel(const TaskState &state);

// ---- 样式表 -------------------------------------------------------------

/// 全局应用样式表(按钮 role / 标签 tone / 输入控件 / 列表 / 滚动条)
QString appStyleSheet();

/// 等宽字体族(日志/代码/路径)
QString monospaceFontFamily();

} // namespace theme
} // namespace jrunner
