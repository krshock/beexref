#include "hud.h"

#include "theme.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace ui::hud {
namespace {

// The toast stack constants.
constexpr int kTopMargin = 10;
constexpr int kSpacing = 8;
constexpr int kMaxVisible = 5;
constexpr int kSlideInOffset = 20;
constexpr int kAnimationMs = 150;

// The panel geometry.
constexpr int kShadowBlur = 6;
constexpr int kShadowOffset = 1;
constexpr int kShadowSteps = 4;
constexpr double kShadowPenWidth = 2.0;
constexpr int kBorderRadius = 8;

// The HUD stylesheet, applied per widget so
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
               "QWidget#OsdItem { background-color: rgba(0, 0, 0, 0.65); border-radius: 8px; }"
               "QLabel { background: transparent; border: none; color: %3; }"
               "QLabel#HUDTitle { color: %4; font-weight: bold; }"
               "QLabel#HUDMuted { color: %5; }"
               "QLabel#HUDDisplay { color: %4; font-size: 20pt; font-weight: 600; }"
               "QLabel#OsdTitle { color: %3; font-size: 22pt; font-weight: 600; }"
               "QLabel#OsdCaption { color: rgb(153, 153, 153); font-size: 14pt; }"
               "QLabel#OsdFooter { color: rgb(153, 153, 153); font-size: 12pt; }"
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
               "QPlainTextEdit#HUDNotes[dirty=\"true\"] { border: 1px solid %7; }"
               "QListView#HUDRecentFiles { background: transparent; border: none;"
               " color: %3; font-size: 10pt; outline: none; }"
               "QListView#HUDRecentFiles::item:hover { color: %7; }"
               "QListView#HUDRecentFiles::item:selected { background: %8; color: %3; }")
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

// The toasts of one host, stacked from the top and centred.
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
        return QPoint(anchoredPos(host_, toast->size(), Anchor::TopCenter, 0).x(), y);
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

QPoint anchoredPos(const QWidget *host, const QSize &size, Anchor anchor, int margin)
{
    const int hostWidth = host ? host->width() : 0;
    const int hostHeight = host ? host->height() : 0;
    int x = margin;
    if (anchor == Anchor::TopCenter || anchor == Anchor::BottomCenter)
        x = (hostWidth - size.width()) / 2;
    else if (anchor == Anchor::TopRight || anchor == Anchor::BottomRight)
        x = hostWidth - size.width() - margin;
    int y = margin;
    if (anchor == Anchor::BottomLeft || anchor == Anchor::BottomCenter
        || anchor == Anchor::BottomRight)
        y = hostHeight - size.height() - margin;
    return QPoint(qMax(0, x), qMax(0, y));
}

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
    // The app marks a dirty draft with a bullet.
    if (titleLabel_)
        titleLabel_->setText(dirty ? title_ + QStringLiteral(" •") : title_);
}

void HudPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (!shadow_ || !body_)
        return;
    // Soft shadow: concentric translucent rings around the glass body,
    // fading outward.
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

// --- on-screen displays -----------------------------------------------

namespace {

// The OSD geometry.
constexpr int kOsdMargin = 16;
constexpr int kOsdSpacing = 4;

// One OSD element: a main line with an optional smaller caption above
// it, drawn without a background and transparent to the mouse by
// construction, so clicks, the wheel and hover reach the canvas under it.
class OsdText : public QWidget
{
public:
    explicit OsdText(QWidget *parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("OsdItem"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
        setStyleSheet(hudStylesheet());
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(2);
        // The caption (author, source...) sits above the main line and
        // the footer (year, resolution...) below it.
        caption_ = new QLabel(this);
        caption_->setObjectName(QStringLiteral("OsdCaption"));
        layout->addWidget(caption_);
        title_ = new QLabel(this);
        title_->setObjectName(QStringLiteral("OsdTitle"));
        layout->addWidget(title_);
        footer_ = new QLabel(this);
        footer_->setObjectName(QStringLiteral("OsdFooter"));
        layout->addWidget(footer_);
        // The transparency attribute is repeated on the labels so the
        // contract is explicit at every level.
        for (QLabel *label : {title_, caption_, footer_}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
    }

    void setContent(const OsdContent &content)
    {
        // Empty lines are hidden: an element shows only what it has.
        const auto setLine = [](QLabel *label, const QString &text) {
            label->setText(text);
            label->setVisible(!text.isEmpty());
        };
        setLine(caption_, content.caption);
        setLine(title_, content.title);
        setLine(footer_, content.footer);
        adjustSize();
    }

    // Starts the cycle from zero: a running fade or hold is dropped, the
    // opacity goes back to 0 and the fade-in begins again. The cycle
    // counter makes the stale timers of an interrupted cycle harmless.
    void restart(const OsdTiming &timing)
    {
        const quint64 cycle = ++cycle_;
        stopAnimations();
        if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect()))
            effect->setOpacity(0.0);
        adjustSize();
        show();
        fadeIn(this, timing.fadeInMs);
        if (timing.holdMs < 0)
            return;
        QTimer::singleShot(timing.holdMs, this, [this, cycle, timing]() {
            if (cycle != cycle_)
                return;
            fadeOut(this, timing.fadeOutMs);
            QTimer::singleShot(timing.fadeOutMs, this, [this, cycle]() {
                if (cycle == cycle_)
                    hide();
            });
        });
    }

    void cancel()
    {
        ++cycle_;
        stopAnimations();
        hide();
    }

protected:
    // A QWidget subclass must paint its stylesheet background itself (the
    // #OsdItem glass pill); the labels are styled by Qt.
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QStyleOption option;
        option.initFrom(this);
        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    }

private:
    void stopAnimations()
    {
        for (QPropertyAnimation *animation : findChildren<QPropertyAnimation *>())
            animation->stop();
    }

    QLabel *title_ = nullptr;
    QLabel *caption_ = nullptr;
    QLabel *footer_ = nullptr;
    quint64 cycle_ = 0;
};

// The OSD elements of one host: a transparent column per anchor, so
// elements sharing a corner stack away from it, repositioned on every
// host resize (the toast stack's pattern).
class OsdManager : public QObject
{
public:
    explicit OsdManager(QWidget *host)
        : QObject(host)
        , host_(host)
    {
        setObjectName(QStringLiteral("hudOsdManager"));
        host->installEventFilter(this);
    }

    void set(const QString &id, Anchor anchor, const OsdContent &content,
             const OsdTiming &timing)
    {
        OsdText *item = items_.value(id);
        if (!item) {
            item = new OsdText(host_);
            items_.insert(id, item);
        }
        QWidget *column = columnFor(anchor);
        if (item->parentWidget() != column) {
            // The item must live in the column's layout, or the column
            // measures 0 and the anchor maths put the element outside the
            // host (the stacking also comes from that layout).
            if (QWidget *oldParent = item->parentWidget()) {
                if (QLayout *oldLayout = oldParent->layout())
                    oldLayout->removeWidget(item);
            }
            static_cast<QVBoxLayout *>(column->layout())->addWidget(item);
        }
        item->setContent(content);
        item->restart(timing);
        relayout();
        // The stylesheet font is applied at polish time, so the sizes the
        // first relayout sees may still be zero (the element would hang
        // below its anchor, clipped). Lay out again once the event loop
        // has polished the new widgets.
        QTimer::singleShot(0, this, [this]() { relayout(); });
    }

    void clear(const QString &id)
    {
        OsdText *item = items_.value(id);
        if (!item)
            return;
        item->cancel();
        relayout();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == host_ && event->type() == QEvent::Resize)
            relayout();
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget *columnFor(Anchor anchor)
    {
        QWidget *&column = columns_[int(anchor)];
        if (!column) {
            column = new QWidget(host_);
            column->setAttribute(Qt::WA_TransparentForMouseEvents);
            column->setAttribute(Qt::WA_TranslucentBackground);
            auto *layout = new QVBoxLayout(column);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(kOsdSpacing);
            // A new child of an already visible host needs an explicit
            // show; the items inside show themselves when they appear.
            column->show();
        }
        return column;
    }

    void relayout()
    {
        for (auto it = columns_.constBegin(); it != columns_.constEnd(); ++it) {
            QWidget *column = it.value();
            if (!column)
                continue;
            column->ensurePolished();
            column->adjustSize();
            column->move(anchoredPos(host_, column->size(), Anchor(it.key()), kOsdMargin));
        }
    }

    QWidget *host_ = nullptr;
    QHash<QString, OsdText *> items_;
    QHash<int, QWidget *> columns_;
};

OsdManager *osdManager(const QWidget *host)
{
    return host
        ? dynamic_cast<OsdManager *>(
              host->findChild<QObject *>(QStringLiteral("hudOsdManager")))
        : nullptr;
}

} // namespace

void osdSet(QWidget *host, const QString &id, Anchor anchor, const OsdContent &content,
            const OsdTiming &timing)
{
    if (!host)
        return;
    OsdManager *manager = osdManager(host);
    if (!manager)
        manager = new OsdManager(host);
    manager->set(id, anchor, content, timing);
}

void osdClear(QWidget *host, const QString &id)
{
    if (OsdManager *manager = osdManager(host))
        manager->clear(id);
}

} // namespace ui::hud
