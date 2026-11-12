# JRunner Qt (jrunner-qt)

轻量级 Java / Kotlin 服务启动器 —— 为「只想把服务跑起来、看看日志、改改配置」的场景而生，不用打开 2 GB 的 IDE。

本项目是 macOS 版 [JRunner](https://github.com/lq0910/jrunner)（Swift）的 Linux 原生移植：**Qt 6 / C++20 / CMake**，**原生 Wayland**（不依赖 X11），功能与 macOS 版对等，并针对 Linux 桌面做了深度适配。

## 致敬

JRunner Qt 站在原项目 [lq0910/jrunner](https://github.com/lq0910/jrunner) 的肩膀上 —— 一款优雅的原生 macOS Java 服务启动器。本项目的产品形态、三栏动线、交互细节与中文文案均向其看齐；感谢原作者的开源与启发。本仓库为独立的 Qt/C++ 移植实现，同样以 MIT 许可发布。

```
┌────────────────────────────────────────────────────────────────────────┐
│  JRunner   操作 帮助   [模块▾]  ⎇ develop   ◐  [启动服务] [打包]  — □ ✕ │  ← 自绘标题栏(融合菜单栏)
├──────────────┬───────────────────────────────┬─────────────────────────┤
│  搜索项目或模块 │  mall-backend                 │  控制台    [含调试▾]     │
│              │  /home/you/…/mall-backend     │  [过滤日志]              │
│  ◧ shop-admin │  ┌─────────────────────────┐  │                         │
│    Java·JDK17 │  │ mall-common          ▾  │  │  实时日志(级别着色/过滤)  │
│  ⬡ mall-backend│  └─────────────────────────┘  │                         │
│    Java·JDK21 │  [启动] [打包] [⚙]             │  失败根因摘要 / 端口识别   │
│              │  模块 (13)        管理分组      │                         │
│              │  常用 [4]   [全部启动][全部停止] │                         │
│              │   ☐ ● order-service    ▶ ▤   │                         │
│              │   ☐ ● user-service     ▶ ▤   │                         │
│  [＋ 添加项目] │                               │                         │
└──────────────┴───────────────────────────────┴─────────────────────────┘
```

## 目录

- [致敬](#致敬)
- [项目说明](#项目说明)
- [功能手册](#功能手册)
  - [主窗口与标题栏](#主窗口与标题栏)
  - [项目管理](#项目管理)
  - [模块启动 / 停止 / 打包](#模块启动--停止--打包)
  - [分组管理](#分组管理)
  - [控制台与完整日志](#控制台与完整日志)
  - [文件工作区](#文件工作区)
  - [Git 集成](#git-集成)
  - [外观](#外观)
  - [MCP 控制（外部 agent 启停服务）](#mcp-控制外部-agent-启停服务)
  - [快捷键](#快捷键)
- [Linux 适配要点](#linux-适配要点)
- [构建与运行](#构建与运行)
- [打包与安装（Arch / pacman）](#打包与安装arch--pacman)
- [配置与数据目录](#配置与数据目录)
- [测试](#测试)
- [目录结构](#目录结构)
- [开发记录摘要](#开发记录摘要)

## 项目说明

JRunner 把「扫描项目 → 选模块 → 启动 → 看日志 → 改配置 → 打包」这条日常动线做成了一个三栏原生窗口：

- **左栏**：项目列表（搜索、运行状态点、Git 分支徽章、重命名、右键菜单）；
- **中栏**：模块工作区（分组、profile、JDK 覆盖、环境变量、启动/打包）或文件工作区（文件树 + 语法高亮编辑器）；
- **右栏**：控制台（实时日志、过滤、失败根因、端口识别）与独立完整日志窗口。

技术纪律：

- `core/` 为纯逻辑层，不依赖 Qt GUI，可独立单测；
- 单文件 < 750 行；UI 文案与 macOS 版保持中文一致；
- 原生 Wayland：自绘标题栏（CSD）+ `startSystemMove/Resize`，KDE/GNOME/niri 等合成器均可运行；
- 56 个自动化回归测试（28 核心 + 28 UI，UI 测试在 Wayland 会话中真实渲染窗口）。

## 功能手册

### 主窗口与标题栏

- **自绘标题栏**：系统标题栏隐藏，菜单栏（操作 / 帮助）融入标题栏；右侧依次为工作区切换（模块 / 文件）、Git 分支切换、主题切换、启动服务、打包、最小化 / 最大化 / 关闭。
- **Wayland 窗口操作**：拖动标题栏移动窗口（`startSystemMove`），双击标题栏切换最大化；窗口四边 5px 边缘可拖拽缩放（`startSystemResize`）。
- **工作区切换**：标题栏「模块 / 文件」下拉切换中栏内容；状态栏显示构建工具、模块数与版本号。

### 项目管理

- **自动扫描**：首次启动递归扫描常用目录（`~/Projects`、`~/IdeaProjects`、`~/workspace` 等，可在设置中修改），之后从缓存秒恢复；「重新扫描」更新列表。
- **识别能力**：Gradle / Maven 项目、模块、Spring Boot 启动类、jib 插件、JDK 需求、`application-*` profiles。**Maven 聚合工程递归展开**（`packaging=pom` 的 `<modules>` 一直展开到叶子模块，含多层嵌套）。
- **添加 / 移除**：左下角「＋ 添加项目」输入或选择绝对路径；右键项目可「在文件管理器中打开 / 重新识别该项目 / 重命名项目 / 移除项目」。
- **重命名**：自定义显示名持久化；留空恢复目录名。重名项目自动附加父目录名区分。
- **搜索**：左上搜索框同时匹配项目名、路径与模块名。
- **状态一目了然**：运行中项目显示绿色状态点；行尾显示当前 Git 分支；副标题显示语言 · JDK 需求 · 模块数。项目名 / 模块名均可选中复制。

### 模块启动 / 停止 / 打包

- **一键启动 / 停止 / 打包**：`bootRun` / `spring-boot:run` / `jib`；双击模块行等价于启动/停止。
- **Profile 选择**：中栏顶部下拉快速切换 Spring profile（自动发现 `application-*`）。
- **模块配置（⚙ 按钮，按模块持久化）**：Spring Profile、JDK 版本覆盖、额外环境变量（`KEY=VALUE` 每行一条）、额外构建参数。
- **JDK 自动发现与去重**：`$JAVA_HOME`、`/usr/lib/jvm/*`、`~/.jdks`、sdkman、asdf、alternatives 反解；按规范化路径去重，设置中可查看/重置列表。
- **进程管理**：停止时 SIGTERM → 5s → SIGKILL；客户端退出后按端口清理残留 java 进程（纯 `/proc` 实现，不依赖 lsof/ss）。
- **登录 shell 环境**：启动命令通过 `$SHELL -lc env` 取回登录环境变量（Nacos / 数据库地址等 GUI 启动不继承的变量）。

### 分组管理

- 「管理分组」可新建 / 编辑 / 删除分组（如「常用」），勾选成员模块；
- 分组标题行提供「全部启动 / 全部停止」，一次拉起整套服务；
- 未分组模块单独列出；分组按项目持久化到 `config.json`。

### 控制台与完整日志

**实时控制台（右栏）**：

- 级别着色（错误/警告/信息/调试/输出）、级别过滤（含调试开关）、文本过滤、自动滚动；
- 失败根因摘要（`Caused by` / `APPLICATION FAILED` / `What went wrong`）、端口自动识别；
- 「清空」按钮（垃圾桶图标）清空当前窗口与磁盘日志。

**完整日志窗口（控制台右上「打开完整日志」）**：

- 日志**全量落盘**为 JSON Lines（`~/.config/jrunner/logs/`），实时窗口只保留有界尾窗（默认 20000 行，可配置），二者分离保证性能；
- 独立非模态窗口浏览完整历史：文本搜索（回车执行）、级别过滤、语法高亮、**导出…** 完整文件；
- 浏览默认加载最近 20 万行、搜索最多显示 5000 条匹配，超出时横幅提示导出；
- 磁盘日志保留 **7 天**，启动时自动清理过期文件；任务结束后状态栏提示「… 已结束 / 失败」。

### 文件工作区

- 标题栏切换到「文件」：项目文件树 + 代码编辑器；
- 语法高亮：yaml / properties / java / kotlin / xml / json / sql / md / shell；
- 行号、当前行高亮、`Ctrl+F` 查找、`Ctrl+S` 保存、未保存草稿保留。

### Git 集成

- 侧边栏分支徽章（悬停显示完整状态 tooltip）；
- 标题栏分支下拉直接切换本地分支（直接读 `.git/HEAD` / `refs/heads`，极快），切换后自动重新识别项目。

### 外观

- 深色 / 浅色 / 跟随系统，运行时即时切换（标题栏 ◐ 按钮或设置）；
- 语义色板 + 全局 QSS，切换时所有内联样式同步刷新。

### MCP 控制（外部 agent 启停服务）

设置中勾选「启用 MCP 控制服务」后，应用监听本地 Unix socket `jrunner-control`；随包安装的 `jrunner-mcp` 是 stdio MCP 桥接器，把 MCP 调用转发到正在运行的 JRunner。

**安装到 qodercli（user 作用域）**：

```bash
qodercli mcp add-json jrunner '{"command":"/usr/bin/jrunner-mcp"}' --scope user
# 会话内执行 /mcp reload 热加载
```

**工具列表**：

| 工具 | 参数 | 说明 |
| --- | --- | --- |
| `list_projects` | — | 列出所有项目及其模块与运行状态 |
| `list_modules` | `project` | 列出指定项目的模块与运行状态 |
| `start_module` | `project`, `module` | 启动指定模块 |
| `stop_module` | `project`, `module` | 停止指定模块 |
| `build_module` | `project`, `module` | 打包指定模块 |
| `module_status` | `project`, `module` | 查询状态、端口与最近日志 |
| `stop_all` | — | 停止所有正在运行的模块 |

`project` 可用项目路径或显示名，`module` 可用模块名或相对路径。示例（对 agent 说）：「启动 mall-backend 常用分组下的所有模块」「关闭已经启动的服务」。

> 安全提示：MCP 服务默认**关闭**；开启后任何能访问该 Unix socket 的本机进程都可启停模块，按需启用。

### 快捷键

| 快捷键 | 动作 |
| --- | --- |
| `Ctrl+R` | 重新扫描项目 |
| `Ctrl+Shift+R` | 启动 / 停止服务 |
| `Ctrl+B` | 打包 |
| `Ctrl+.` | 停止 |
| `Ctrl+S` | 保存文件 |
| `Ctrl+F` | 查找 |
| `Ctrl+,` | 设置 |

## Linux 适配要点

- **JDK 发现**：`$JAVA_HOME`、`/usr/lib/jvm/*`、`~/.jdks`（IntelliJ）、`~/.sdkman/candidates/java`、`~/.asdf/installs/java`、`/etc/alternatives/java` 反解（版本读 `release` 文件，规范化路径去重）。
- **端口清理**：纯 `/proc` 实现（`/proc/net/tcp{,6}` → inode → `/proc/*/fd` → `comm == java`）。
- **Gradle 兜底**：`./gradlew` → PATH → `~/.gradle/wrapper/dists` 缓存版本匹配（优先项目声明版本）。
- **桌面集成**：`xdg-open` 打开目录、标准 `QFileDialog`、`.desktop` + hicolor 图标随包安装。
- **Wayland**：标准 xdg-shell + 自绘 CSD；不依赖 X11 / XWayland。

## 构建与运行

依赖：CMake ≥ 3.20、C++20 编译器、Qt 6（Core / Gui / Widgets / Concurrent / Network；测试另需 Test）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build                 # 核心逻辑回归测试
./build/qt/jrunner                     # 运行（Wayland 会话）
```

调试与诊断：

- `JRUNNER_DEBUG=1`：分类调试日志输出到 stderr；
- `JRUNNER_SMOKE_SHOT=/path/shot.png`：启动 2.5s 后窗口截图并退出（冒烟测试）；
- 崩溃线索写入 `~/.config/jrunner/crash.log`。

## 打包与安装（Arch / pacman）

仓库自带 `packaging/arch/PKGBUILD`。源码 tarball 与构建产物不入库（可再生），一键打包：

```bash
scripts/make-arch-package.sh           # 生成 tarball + updpkgsums + makepkg -f
sudo pacman -U packaging/arch/jrunner-0.1.0-1-x86_64.pkg.tar.zst
```

安装内容：`/usr/bin/jrunner`、`/usr/bin/jrunner-mcp`、`.desktop` 入口与 hicolor 图标。
依赖 `qt6-base`、`qt6-wayland`；可选 `jdk-openjdk` / `maven` / `gradle` / `git`。

## 配置与数据目录

| 路径 | 内容 |
| --- | --- |
| `~/.config/jrunner/config.json` | 模块配置（profile/JDK/env/参数）、分组、项目显示名、MCP 开关、日志尾窗行数 |
| `~/.config/jrunner/logs/` | 完整日志（JSON Lines，保留 7 天） |
| `~/.config/jrunner/crash.log` | 崩溃线索 |
| QSettings（`jrunner/jrunner`） | 外观模式、扫描目录等偏好 |

## 测试

```bash
QT_QPA_PLATFORM=wayland ./build/qt/tests/jrunner-core-tests   # 28 项:扫描/JDK/日志落盘/配置/MCP 协议
QT_QPA_PLATFORM=wayland ./build/qt/tests/jrunner-ui-tests     # 28 项:真实窗口交互回归
```

UI 测试在 Wayland 会话中真实渲染窗口，覆盖：快速点击切换、完整日志浏览、无边框标题栏窗口控制、MCP 控制与 stdio 桥接往返、暗色刷新、重命名与可选中名称、**样式表激活下项目名不裁切**等回归。

## 目录结构

```
├── CMakeLists.txt            # 顶层构建(版本注入、Qt6、可选 WaylandClient)
├── VERSION                   # 0.1.0
├── qt/
│   ├── CMakeLists.txt        # jrunner / jrunner-mcp / 测试目标
│   ├── data/                 # .desktop.in + hicolor 图标
│   ├── src/
│   │   ├── main.cpp          # 入口:崩溃日志、主题、冒烟截图
│   │   ├── app/              # main_window(三栏/自绘标题栏)、app_store(中央状态)、mcp_control_server
│   │   ├── core/             # 纯逻辑:scanner/task_planner/process_task(磁盘日志)/
│   │   │                     # jdk_resolver/git_info/app_config/shell_env/file_models
│   │   ├── mcp/              # jrunner-mcp stdio 桥接器
│   │   └── ui/               # theme/widgets/sidebar/module_panel/log_console/
│   │                         # full_log_dialog/file_browser/code_editor/settings_dialog
│   └── tests/                # core_logic_tests / ui_interaction_tests
├── packaging/arch/           # PKGBUILD(pacman 包)
└── scripts/                  # make-arch-package.sh
```

## 开发记录摘要

仓库以总结性提交初始化（完整里程碑如下）：

1. **feat: Qt6 原生 Wayland 移植与核心引擎** —— 项目扫描（含 Maven 聚合递归展开）、JDK 发现去重、进程任务与端口清理、三栏 UI、文件工作区、Git 集成、主题系统。
2. **feat: 日志体系与完整日志窗口** —— 全量 JSONL 落盘 + 有界实时尾窗分离；独立完整日志窗口（搜索/级别过滤/导出，7 天保留）。
3. **feat: 自绘标题栏** —— 隐藏系统标题栏，菜单栏融合，最小化/最大化/关闭内置，Wayland 原生拖拽/缩放。
4. **feat: MCP 控制面** —— 配置开关 + `jrunner-control` Unix socket + `jrunner-mcp` stdio 桥接，外部 agent 可启停模块。
5. **fix: 扫描与显示回归** —— 嵌套 Maven 子模块识别、JDK 列表去重、项目重命名与名称可选中、清空按钮换垃圾桶图标、快速点击切换失效。
6. **fix: 侧边栏裁切根因** —— 全局样式表激活时 `QStyleSheetStyle` 按错误度量压缩自带 `QFont` 的标签（名称 18px 被压到 14px，中文顶部裁切）；以真实字体度量固定标签高度修复，并加入触发条件下的回归测试。
7. **build: Arch pacman 打包** —— PKGBUILD + 一键打包脚本，`pacman -U` 安装。
8. **docs: 项目说明与功能手册** —— 本 README。
9. **chore: 项目更名 jrunner-qt** —— README 致敬原项目 [lq0910/jrunner](https://github.com/lq0910/jrunner) 并附链接；示例全部使用虚构项目名，不暴露真实业务项目。