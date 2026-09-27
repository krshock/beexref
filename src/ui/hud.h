#pragma once

#include <QPoint>
#include <QSize>
#include <QString>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace ui::hud {

// Shows a short-lived notification at the top of the host, stacked with
// any others: the toast stack (top margin 10, spacing 8, up
// to five visible, three seconds each), sliding in and fading out.
void toast(QWidget *host, const QString &text, int timeoutMs = 3000);

// Where a HUD element sits inside its host.
enum class Anchor {
    TopLeft,
    TopCenter,
    TopRight,
};

// The top-left position for an element of this size in host, honouring
// the anchor and the margin; the element never leaves the host.
QPoint anchoredPos(const QWidget *host, const QSize &size, Anchor anchor, int margin);

// The HUD panel: a translucent glass panel with a painted
// soft shadow. Content goes into body()/bodyLayout().
class HudPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HudPanel(QWidget *parent = nullptr, bool shadow = true);

    QWidget *body() const { return body_; }
    QVBoxLayout *bodyLayout() const { return bodyLayout_; }
    // A small muted heading above the content; ' •' marks a dirty draft.
    void setTitle(const QString &title);
    QString title() const;
    void setTitleDirty(bool dirty);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QWidget *body_ = nullptr;
    QVBoxLayout *bodyLayout_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QString title_;
    bool shadow_ = true;
};

// The fade helpers: animate a widget's opacity in or out.
void fadeIn(QWidget *widget, int durationMs = 150);
void fadeOut(QWidget *widget, int durationMs = 150);

} // namespace ui::hud
