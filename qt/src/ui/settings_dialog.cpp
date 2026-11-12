#include "settings_dialog.h"

#include "theme.h"
#include "widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace jrunner {

SettingsDialog::SettingsDialog(AppStore *store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumSize(560, 620);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);

    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(16);

    // ---- 外观 ----
    auto *appearanceTitle = new QLabel(QStringLiteral("外观"), content);
    QFont sectionFont = appearanceTitle->font();
    sectionFont.setPointSize(10);
    sectionFont.setBold(true);
    appearanceTitle->setFont(sectionFont);
    appearanceTitle->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::textSecondary().name()));

    m_appearancePicker = new QComboBox(content);
    m_appearancePicker->addItem(QStringLiteral("◐ 跟随系统"),
                                theme::themeModeKey(theme::ThemeMode::System));
    m_appearancePicker->addItem(QStringLiteral("☀ 浅色"),
                                theme::themeModeKey(theme::ThemeMode::Light));
    m_appearancePicker->addItem(QStringLiteral("☾ 深色"),
                                theme::themeModeKey(theme::ThemeMode::Dark));
    const int appearanceIdx = m_appearancePicker->findData(theme::themeModeKey(theme::currentMode()));
    if (appearanceIdx >= 0)
        m_appearancePicker->setCurrentIndex(appearanceIdx);
    connect(m_appearancePicker, &QComboBox::currentIndexChanged, this, [this]() {
        const auto mode = theme::themeModeFromKey(m_appearancePicker->currentData().toString());
        theme::applyTheme(mode);
        theme::storeThemeMode(mode);
    });

    auto *appearanceHint = new QLabel(
        QStringLiteral("选「跟随系统」时,外观随桌面环境的浅色 / 深色自动切换"), content);
    appearanceHint->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));

    contentLayout->addWidget(appearanceTitle);
    contentLayout->addWidget(m_appearancePicker);
    contentLayout->addWidget(appearanceHint);

    // ---- 扫描目录 ----
    auto *rootsTitle = new QLabel(QStringLiteral("扫描目录"), content);
    rootsTitle->setFont(sectionFont);
    rootsTitle->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::textSecondary().name()));
    auto *rootsHint = new QLabel(
        QStringLiteral("每行一个目录。首次启动自动发现项目，之后从缓存恢复；点击重新扫描更新列表。"), content);
    rootsHint->setWordWrap(true);
    rootsHint->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));

    m_rootsEdit = new QPlainTextEdit(content);
    QFont mono(theme::monospaceFontFamily());
    mono.setPointSize(10);
    m_rootsEdit->setFont(mono);
    m_rootsEdit->setPlainText(store->scanRoots().join(QLatin1Char('\n')));
    m_rootsEdit->setFixedHeight(110);

    contentLayout->addWidget(rootsTitle);
    contentLayout->addWidget(rootsHint);
    contentLayout->addWidget(m_rootsEdit);

    // ---- MCP 控制 ----
    auto *mcpTitle = new QLabel(QStringLiteral("MCP 控制"), content);
    mcpTitle->setFont(sectionFont);
    mcpTitle->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textSecondary().name()));
    m_mcpCheck = new QCheckBox(QStringLiteral("启用 MCP 控制服务(允许外部 agent 启停模块)"), content);
    m_mcpCheck->setChecked(store->mcpEnabled());
    auto *mcpHint = new QLabel(
        QStringLiteral("开启后在本机暴露控制接口,配合 jrunner-mcp(stdio)供其他 agent 调用;"
                       "仅当前用户可连接,关闭后立即停止监听。"), content);
    mcpHint->setWordWrap(true);
    mcpHint->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
    contentLayout->addWidget(mcpTitle);
    contentLayout->addWidget(m_mcpCheck);
    contentLayout->addWidget(mcpHint);

    // ---- JDK 列表 ----
    m_jdkCountLabel = new QLabel(content);
    m_jdkCountLabel->setFont(sectionFont);
    m_jdkCountLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::textSecondary().name()));

    m_jdkListContainer = new QWidget(content);
    auto *jdkLayout = new QVBoxLayout(m_jdkListContainer);
    jdkLayout->setContentsMargins(0, 0, 0, 0);
    jdkLayout->setSpacing(3);

    contentLayout->addWidget(m_jdkCountLabel);
    contentLayout->addWidget(m_jdkListContainer);
    contentLayout->addStretch(1);

    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    // ---- 底部按钮 ----
    auto *buttons = new QHBoxLayout();
    auto *resetButton = new QPushButton(QStringLiteral("重置 JDK 列表"), this);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        m_store->refreshJdks();
    });
    buttons->addWidget(resetButton);
    buttons->addStretch(1);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    auto *saveButton = new QPushButton(QStringLiteral("保存并重新扫描"), this);
    saveButton->setProperty("role", "primary");
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        QStringList roots;
        const QStringList lines = m_rootsEdit->toPlainText().split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString t = line.trimmed();
            if (!t.isEmpty())
                roots.append(t);
        }
        m_store->setMcpEnabled(m_mcpCheck->isChecked());
        m_store->setScanRoots(roots);
        m_store->scanProjects();
        accept();
    });
    buttons->addWidget(cancelButton);
    buttons->addWidget(saveButton);
    layout->addLayout(buttons);

    connect(m_store, &AppStore::jdksChanged, this, [this]() { rebuildJdkList(); });
    rebuildJdkList();
}

void SettingsDialog::rebuildJdkList()
{
    // 清空旧列表
    if (auto *layout = m_jdkListContainer->layout()) {
        while (layout->count() > 0) {
            QLayoutItem *item = layout->takeAt(0);
            delete item->widget();
            delete item;
        }
    }

    const JDKList jdks = m_store->jdks();
    m_jdkCountLabel->setText(QStringLiteral("已安装的 JDK (%1)").arg(jdks.size()));

    for (const JDK &jdk : jdks) {
        auto *row = new QWidget(m_jdkListContainer);
        row->setStyleSheet(
            QStringLiteral("QWidget { background: %1; border-radius: 5px; }").arg(theme::bg().name()));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 4, 8, 4);
        rowLayout->setSpacing(8);

        auto *mark = new QLabel(jdk.isCurrent ? QStringLiteral("✓") : QStringLiteral("○"), row);
        mark->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                                .arg(jdk.isCurrent ? theme::success().name()
                                                   : theme::textTertiary().name()));
        auto *version = new QLabel(QStringLiteral("JDK %1").arg(jdk.fullVersion), row);
        version->setStyleSheet(
            QStringLiteral("color: %1; font-size: 11px; font-weight: 600;")
                .arg(theme::textPrimary().name()));
        auto *vendor = new QLabel(jdk.vendor, row);
        vendor->setStyleSheet(
            QStringLiteral("color: %1; font-size: 10px;").arg(theme::textTertiary().name()));
        rowLayout->addWidget(mark);
        rowLayout->addWidget(version);
        rowLayout->addWidget(vendor);
        rowLayout->addStretch(1);
        if (jdk.isCurrent)
            rowLayout->addWidget(new Badge(QStringLiteral("默认"), theme::success(), true, row));

        m_jdkListContainer->layout()->addWidget(row);
    }
}

} // namespace jrunner
