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
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// The top-left position for an element of this size in host, honouring
// the anchor and the margin; the element never leaves the host.
QPoint anchoredPos(const QWidget *host, const QSize &size, Anchor anchor, int margin);

// The timings of one OSD element: fade in, hold at full opacity (-1
// stays until the element is replaced or cleared) and fade out.
struct OsdTiming
{
    int fadeInMs = 150;
    int holdMs = -1;
    int fadeOutMs = 150;
};

// The lines of one on-screen display element: the main title, an
// optional caption above it (an author, a source...) and an optional
// footer below it (a year, a resolution...). Empty lines are hidden, so
// an element shows only the lines it has. The caption may carry simple
// rich text (Qt's auto-detection); callers escape the text they build it
// from.
struct OsdContent
{
    QString title;
    QString caption;
    QString footer;
};

// Shows or updates one on-screen display element in a corner of host.
// The id identifies it: setting it again replaces the content and
// restarts the timing cycle from the start. Elements are transparent to
// the mouse -- clicks, the wheel and hover reach whatever is under them
// -- and never take focus. Elements sharing an anchor stack away from it.
void osdSet(QWidget *host, const QString &id, Anchor anchor, const OsdContent &content,
            const OsdTiming &timing = {});
// Cancels an element's cycle and hides it at once.
void osdClear(QWidget *host, const QString &id);

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
