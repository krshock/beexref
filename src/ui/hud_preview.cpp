#include "hud_preview.h"

#include "theme.h"
#include "view.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace ui {
namespace {

// One semantic token swatch of the preview.
QFrame *swatch(const QColor &color, const QString &tooltip, QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("HUDSwatch"));
    frame->setFixedSize(18, 18);
    frame->setToolTip(tooltip);
    frame->setStyleSheet(QStringLiteral("QFrame#HUDSwatch { background-color: rgb(%1,%2,%3);"
                                        " border-radius: 4px; }")
                             .arg(color.red())
                             .arg(color.green())
                             .arg(color.blue()));
    return frame;
}

QLabel *muted(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("HUDMuted"));
    return label;
}

} // namespace

HudPreview::HudPreview(View *view)
    : HudPanel(view)
    , view_(view)
{
    setObjectName(QStringLiteral("HUDPreview"));
    setTitle(QStringLiteral("HUD Preview"));

    bodyLayout()->addWidget(muted(
        QStringLiteral("Grayscale chrome, tone-step states, muted semantic accents."), body()));

    auto *buttons = new QHBoxLayout();
    auto *normal = new QPushButton(QStringLiteral("Button"), body());
    normal->setObjectName(QStringLiteral("HUDButton"));
    normal->setAutoDefault(false);
    auto *disabled = new QPushButton(QStringLiteral("Disabled"), body());
    disabled->setObjectName(QStringLiteral("HUDButton"));
    disabled->setEnabled(false);
    auto *toastButton = new QPushButton(QStringLiteral("Show toast"), body());
    toastButton->setObjectName(QStringLiteral("HUDButton"));
    toastButton->setAutoDefault(false);
    connect(toastButton, &QPushButton::clicked, this, [this]() {
        hud::toast(view_, QStringLiteral("HUD toast — the new look"));
    });
    buttons->addWidget(normal);
    buttons->addWidget(disabled);
    buttons->addWidget(toastButton);
    buttons->addStretch();
    bodyLayout()->addLayout(buttons);

    bodyLayout()->addWidget(new QLabel(QStringLiteral("Semantic tokens:"), body()));
    auto *tokens = new QHBoxLayout();
    tokens->setSpacing(4);
    tokens->addWidget(swatch(theme::hudSelection, QStringLiteral("HUD:Selection"), body()));
    tokens->addWidget(swatch(theme::hudFocus, QStringLiteral("HUD:Focus"), body()));
    tokens->addWidget(swatch(theme::hudDanger, QStringLiteral("HUD:Danger"), body()));
    tokens->addWidget(swatch(theme::hudWarning, QStringLiteral("HUD:Warning"), body()));
    tokens->addWidget(swatch(theme::hudSuccess, QStringLiteral("HUD:Success"), body()));
    tokens->addWidget(swatch(theme::hudInfo, QStringLiteral("HUD:Info"), body()));
    tokens->addStretch();
    bodyLayout()->addLayout(tokens);

    if (view_)
        view_->installEventFilter(this);
    adjustSize();
    moveToTopRight();
}

void HudPreview::moveToTopRight(int margin)
{
    if (!view_)
        return;
    adjustSize();
    move(hud::anchoredPos(view_, size(), hud::Anchor::TopRight, margin));
}

bool HudPreview::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == view_ && event->type() == QEvent::Resize && !isHidden())
        moveToTopRight();
    return hud::HudPanel::eventFilter(watched, event);
}

void HudPreview::showEvent(QShowEvent *event)
{
    hud::HudPanel::showEvent(event);
    hud::fadeIn(this);
}

} // namespace ui
