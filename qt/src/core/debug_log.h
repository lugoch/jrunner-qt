#pragma once

#include <QString>

namespace jrunner {

// 【核心日志】分类调试日志:写入 stderr,便于命令行排查问题。
// 环境变量 JRUNNER_DEBUG=1 时启用。
void debugLog(const QString &category, const QString &message);

bool debugLogEnabled();

} // namespace jrunner
