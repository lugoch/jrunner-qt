#pragma once

#include <QLabel>
#include <QString>
#include <QStyle>
#include <QTimer>
#include <QWidget>

#include "core/git_info.h"
#include "core/process_task.h"

class QPushButton;

namespace jrunner {

QPushButton *makeIconButton(const QString &iconName, QStyle::StandardPixmap fallback,
                            const QString &label, QWidget *parent);

// 【通用组件】对应 macOS 版 DesignSystem.swift 中的可复用部件。

/// 徽章标签:胶囊底 + 语义色
class Badge : public QLabel {
    Q_OBJECT
public:
    explicit Badge(const QString &text, const QColor &color, bool filled = false,
                   QWidget *parent = nullptr);
    void setBadgeColor(const QColor &color);
    static QString hexWithAlpha(const QColor &color, double alpha);

private:
    void refresh();
    QColor m_color;
    bool m_filled = false;
};

/// 状态指示灯(运行中带呼吸光环)
class StatusDot : public QWidget {
    Q_OBJECT
public:
    explicit StatusDot(QWidget *parent = nullptr);
    void setTaskState(const TaskState &state);
    QSize sizeHint() const override { return { 22, 16 }; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void restartTimer();

    TaskState m_state;
    QTimer m_timer;
    qreal m_phase = 0;       // 0..1 呼吸相位
};

/// 空状态提示(图标 + 标题 + 副标题)
class EmptyStateView : public QWidget {
    Q_OBJECT
public:
    explicit EmptyStateView(const QString &glyph, const QString &title,
                            const QString &subtitle, QWidget *parent = nullptr);
};

/// 通用工厂:等宽字体标签
QLabel *makeMonoLabel(const QString &text, QWidget *parent);

/// 重新应用动态属性样式(改 role/tone 后调用)
void repolish(QWidget *widget);

} // namespace jrunner
