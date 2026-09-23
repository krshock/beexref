#include "hud.h"

#include "theme.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace ui::hud {
namespace {

// The reference's toast stack constants.
constexpr int kTopMargin = 10;
constexpr int kSpacing = 8;
constexpr int kMaxVisible = 5;
constexpr int kSlideInOffset = 20;
constexpr int kAnimationMs = 150;

// The reference's panel geometry (hud/widgets.py).
constexpr int kShadowBlur = 6;
constexpr int kShadowOffset = 1;
constexpr int kShadowSteps = 4;
constexpr double kShadowPenWidth = 2.0;
constexpr int kBorderRadius = 8;

// The reference's HUD stylesheet (hud/style.py), applied per widget so
// it never leaks into the rest of the application.
QString hudStylesheet()
{
    const auto rgba = [](const QColor &color) {
        return QStringLiteral("rgba(%1, %2, %3, %4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(QString::number(color.alphaF(), 'f', 2));
    };
    const QString background = rgba(theme::hudBackground);
    const QString border = rgba(theme::hudBorder);
    const QString surface = rgba(theme::hudSurface);
    const QString hover = rgba(theme::hudHover);
    const QString pressed = rgba(theme::hudPressed);
    const QString foreground = rgba(theme::hudForeground);
    const QString muted = rgba(theme::hudMuted);
    const QString emphasis = rgba(theme::hudEmphasis);
    const QString selection = rgba(theme::hudSelection);
    const QString focus = rgba(theme::hudFocus);

    return QStringLiteral(
               "QWidget#HUDPanel, QWidget#HUDToast {"
               " background-color: %1; border: 1px solid %2; border-radius: 8px; }"
               "QLabel { background: transparent; border: none; color: %3; }"
               "QLabel#HUDTitle { color: %4; font-weight: bold; }"
               "QLabel#HUDMuted { color: %5; }"
               "QLabel#HUDDisplay { color: %4; font-size: 20pt; font-weight: 600; }"
               "QLabel#HUDSubtitle { color: %3; font-size: 11pt; }"
               "QLabel#HUDHint { color: %5; font-size: 10pt; }"
               "QPushButton#HUDButton { background-color: %6; color: %3;"
               " border: 1px solid %2; border-radius: 6px; padding: 4px 12px; }"
               "QPushButton#HUDButton[welcome=\"true\"] { font-size: 10pt; padding: 6px 14px; }"
               "QPushButton#HUDButton[primary=\"true\"] { color: %7; border: 1px solid %7; }"
               "QPushButton#HUDButton[primary=\"true\"]:hover { background-color: %8; }"
               "QPushButton#HUDButton[primary=\"true\"]:disabled { color: %5;"
               " border: 1px solid %2; }"
               "QPushButton#HUDButton:hover { background-color: %8; }"
               "QPushButton#HUDButton:pressed { background-color: %9; }"
               "QPushButton#HUDButton:disabled { color: %5; background-color: %9; }"
               "QPushButton#HUDButton[dirty=\"true\"] { color: %7; border: 1px solid %7; }"
               "QLineEdit#HUDLineEdit { background-color: %6; color: %3;"
               " border: 1px solid %2; border-radius: 6px; padding: 4px 8px;"
               " selection-background-color: %10; }"
               "QLineEdit#HUDLineEdit:focus { border: 1px solid %10; }"
               "QLineEdit#HUDLineEdit[dirty=\"true\"] { border: 1px solid %7; }"
               "QPlainTextEdit#HUDNotes { background-color: %6; color: %3;"
               " border: 1px solid %2; border-radius: 6px; padding: 4px 8px;"
               " selection-background-color: %10; }"
               "QPlainTextEdit#HUDNotes:focus { border: 1px solid %10; }"
               "QPlainTextEdit#HUDNotes[dirty=\"true\"] { border: 1px solid %7; }")
        .arg(background, border, foreground, emphasis, muted, surface, selection, hover,
             pressed, focus);
}

// A toast: the panel with one label, sliding in from below its slot.
class Toast : public HudPanel
{
public:
    Toast(QWidget *host, const QString &text)
        : HudPanel(host, /*shadow=*/false)
    {
        setObjectName(QStringLiteral("HUDToast"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *label = new QLabel(text, body());
        label->setObjectName(QStringLiteral("HUDLabel"));
        bodyLayout()->addWidget(label);
        adjustSize();
    }
};

// The toasts of one host, stacked from the top and centred, like the
// reference's ToastManager.
class Stack : public QObject
{
public:
    explicit Stack(QWidget *host)
        : QObject(host)
        , host_(host)
    {
        host->installEventFilter(this);
    }

    void add(const QString &text, int timeoutMs)
    {
        auto *toast = new Toast(host_, text);
        toasts_.append(toast);
        while (toasts_.size() > kMaxVisible) {
            HudPanel *oldest = toasts_.takeFirst();
            oldest->deleteLater();
        }

        relayout(/*exclude=*/toast);
        const QPoint final = slotPos(toast);
        fadeIn(toast);
        toast->move(final + QPoint(0, kSlideInOffset));
        toast->show();
        auto *slide = new QPropertyAnimation(toast, "pos", toast);
        slide->setDuration(kAnimationMs);
        slide->setStartValue(toast->pos());
        slide->setEndValue(final);
        slide->setEasingCurve(QEasingCurve::OutCubic);
        slide->start(QPropertyAnimation::KeepWhenStopped);

        QTimer::singleShot(timeoutMs, toast, [this, toast]() {
            toasts_.removeAll(toast);
            fadeOut(toast);
            QTimer::singleShot(kAnimationMs, toast, [toast]() { toast->deleteLater(); });
            relayout();
        });
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == host_ && event->type() == QEvent::Resize)
            relayout();
        return QObject::eventFilter(watched, event);
    }

private:
    void relayout(HudPanel *exclude = nullptr)
    {
        for (HudPanel *toast : std::as_const(toasts_)) {
            if (toast != exclude)
                moveToast(toast, slotPos(toast));
        }
    }

    QPoint slotPos(const HudPanel *toast) const
    {
        int y = kTopMargin;
        for (HudPanel *other : toasts_) {
            if (other == toast)
                break;
            y += other->height() + kSpacing;
        }
        const int x = (host_->width() - toast->width()) / 2;
        return QPoint(qMax(0, x), y);
    }

    void moveToast(HudPanel *toast, const QPoint &pos)
    {
        if (toast->pos() == pos)
            return;
        auto *animation = new QPropertyAnimation(toast, "pos", toast);
        animation->setDuration(kAnimationMs);
        animation->setStartValue(toast->pos());
        animation->setEndValue(pos);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        animation->start(QPropertyAnimation::KeepWhenStopped);
    }

    QWidget *host_ = nullptr;
    QList<HudPanel *> toasts_;
};

} // namespace

// --- HudPanel ---------------------------------------------------------

HudPanel::HudPanel(QWidget *parent, bool shadow)
    : QWidget(parent)
    , shadow_(shadow)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    auto *outer = new QVBoxLayout(this);
    if (shadow) {
        // Reserve exactly the painted shadow's extent.
        const double halfPen = kShadowPenWidth / 2.0;
        outer->setContentsMargins(static_cast<int>(std::ceil(kShadowBlur + halfPen)),
                                  static_cast<int>(std::ceil(kShadowBlur - kShadowOffset
                                                             + halfPen)),
                                  static_cast<int>(std::ceil(kShadowBlur + halfPen)),
                                  static_cast<int>(std::ceil(kShadowBlur + kShadowOffset
                                                             + halfPen)));
    } else {
        outer->setContentsMargins(0, 0, 0, 0);
    }

    body_ = new QWidget(this);
    body_->setObjectName(QStringLiteral("HUDPanel"));
    body_->setStyleSheet(hudStylesheet());
    bodyLayout_ = new QVBoxLayout(body_);
    bodyLayout_->setContentsMargins(12, 8, 12, 8);
    outer->addWidget(body_);
}

void HudPanel::setTitle(const QString &title)
{
    title_ = title;
    if (!titleLabel_) {
        titleLabel_ = new QLabel(body_);
        titleLabel_->setObjectName(QStringLiteral("HUDTitle"));
        bodyLayout_->insertWidget(0, titleLabel_);
    }
    titleLabel_->setText(title_);
    titleLabel_->setVisible(!title_.isEmpty());
}

QString HudPanel::title() const
{
    return title_;
}

void HudPanel::setTitleDirty(bool dirty)
{
    // The reference marks a dirty draft with a bullet.
    if (titleLabel_)
        titleLabel_->setText(dirty ? title_ + QStringLiteral(" •") : title_);
}

void HudPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (!shadow_ || !body_)
        return;
    // Soft shadow: concentric translucent rings around the glass body,
    // fading outward, like the reference.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF rect(body_->geometry());
    for (int step = 0; step < kShadowSteps; ++step) {
        const double grow = kShadowBlur * (step + 1) / double(kShadowSteps);
        const int alpha = qMax(70 - step * 12, 0);
        QPen pen(QColor(0, 0, 0, alpha));
        pen.setWidthF(kShadowPenWidth);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect.adjusted(-grow, -grow + kShadowOffset, grow,
                                              grow + kShadowOffset),
                                kBorderRadius + grow, kBorderRadius + grow);
    }
}

// --- animation --------------------------------------------------------

void fadeIn(QWidget *widget, int durationMs)
{
    if (!widget)
        return;
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
        effect->setOpacity(0.0);
    }
    auto *animation = new QPropertyAnimation(effect, "opacity", widget);
    animation->setDuration(durationMs);
    animation->setStartValue(effect->opacity());
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QPropertyAnimation::KeepWhenStopped);
}

void fadeOut(QWidget *widget, int durationMs)
{
    if (!widget)
        return;
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
    }
    auto *animation = new QPropertyAnimation(effect, "opacity", widget);
    animation->setDuration(durationMs);
    animation->setStartValue(effect->opacity());
    animation->setEndValue(0.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QPropertyAnimation::KeepWhenStopped);
}

void toast(QWidget *host, const QString &text, int timeoutMs)
{
    if (!host)
        return;
    Stack *stack =
        dynamic_cast<Stack *>(host->findChild<QObject *>(QStringLiteral("hudToastStack")));
    if (!stack) {
        stack = new Stack(host);
        stack->setObjectName(QStringLiteral("hudToastStack"));
    }
    stack->add(text, timeoutMs);
}

} // namespace ui::hud
