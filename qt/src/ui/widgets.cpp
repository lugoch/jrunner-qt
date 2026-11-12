#include "widgets.h"

#include "theme.h"

#include <QEvent>
#include <QIcon>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QVBoxLayout>

namespace jrunner {

QPushButton *makeIconButton(const QString &iconName, QStyle::StandardPixmap fallback,
                            const QString &label, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setProperty("iconOnly", true);
    button->setIcon(QIcon::fromTheme(iconName, button->style()->standardIcon(fallback)));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(30, 30);
    button->setToolTip(label);
    button->setAccessibleName(label);
    return button;
}

QString Badge::hexWithAlpha(const QColor &color, double alpha)
{
    QColor c = color;
    c.setAlphaF(alpha);
    return c.name(QColor::HexArgb);
}

Badge::Badge(const QString &text, const QColor &color, bool filled, QWidget *parent)
    : QLabel(text, parent)
    , m_color(color)
    , m_filled(filled)
{
    setObjectName(QStringLiteral("jrBadge"));
    refresh();
}

void Badge::setBadgeColor(const QColor &color)
{
    m_color = color;
    refresh();
}

void Badge::refresh()
{
    setStyleSheet(QStringLiteral("QLabel#jrBadge {"
                                 "  color: %1;"
                                 "  background: %2;"
                                 "  border: 1px solid %3;"
                                 "}")
                      .arg(m_filled ? QStringLiteral("#ffffff") : m_color.name(QColor::HexArgb),
                           hexWithAlpha(m_color, m_filled ? 0.9 : 0.13),
                           hexWithAlpha(m_color, m_filled ? 0.9 : 0.25)));
}

// ---------------------------------------------------------------------------
// StatusDot

StatusDot::StatusDot(QWidget *parent)
    : QWidget(parent)
{
    m_timer.setInterval(50);     // 20 fps,与 macOS 版 TimelineView 频率一致
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        m_phase += 0.05;
        if (m_phase >= 1.0)
            m_phase -= 1.0;
        update();
    });
}

void StatusDot::setTaskState(const TaskState &state)
{
    if (m_state.kind == state.kind)
        return;
    m_state = state;
    restartTimer();
    update();
}

void StatusDot::restartTimer()
{
    const bool active = m_state.kind == TaskState::Running || m_state.kind == TaskState::Starting;
    if (active && !m_timer.isActive())
        m_timer.start();
    else if (!active && m_timer.isActive())
        m_timer.stop();
    m_phase = 0;
}

void StatusDot::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor dotColor = theme::taskStateColor(m_state);
    const QPointF center = rect().center();
    const double baseR = 4.0;

    const bool active = m_state.kind == TaskState::Running || m_state.kind == TaskState::Starting;
    if (active) {
        // 呼吸光环:1.4s 周期向外扩散淡出
        const double t = m_phase;
        const double r = baseR + 6.5 * t;
        QColor halo = dotColor;
        halo.setAlphaF(0.40 * (1.0 - t));
        QPen pen(halo, 1.6);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(center, qreal(r), qreal(r));
    }

    p.setPen(Qt::NoPen);
    p.setBrush(dotColor);
    p.drawEllipse(center, baseR, baseR);
}

// ---------------------------------------------------------------------------
// EmptyStateView

EmptyStateView::EmptyStateView(const QString &glyph, const QString &title,
                               const QString &subtitle, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(10);

    auto *iconLabel = new QLabel(glyph, this);
    iconLabel->setAlignment(Qt::AlignCenter);
    QFont iconFont = iconLabel->font();
    iconFont.setPointSize(22);
    iconFont.setBold(false);
    iconLabel->setFont(iconFont);
    iconLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textTertiary().name()));

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(11);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textSecondary().name()));

    auto *subLabel = new QLabel(subtitle, this);
    subLabel->setAlignment(Qt::AlignCenter);
    subLabel->setWordWrap(true);
    subLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textTertiary().name()));

    layout->addStretch(1);
    layout->addWidget(iconLabel);
    layout->addWidget(titleLabel);
    layout->addWidget(subLabel);
    layout->addStretch(1);
}

// ---------------------------------------------------------------------------

QLabel *makeMonoLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont f = label->font();
    f.setFamily(theme::monospaceFontFamily());
    label->setFont(f);
    return label;
}

void repolish(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

} // namespace jrunner
