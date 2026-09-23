#include "hud.h"

#include "theme.h"

#include <QEvent>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace ui::hud {
namespace {

// The reference's toast stack constants.
constexpr int kTopMargin = 10;
constexpr int kSpacing = 8;
constexpr int kMaxVisible = 5;

class Toast : public QWidget
{
public:
    Toast(QWidget *host, const QString &text)
        : QWidget(host)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setObjectName(QStringLiteral("HUDToast"));
        // The reference's glass panel styling.
        setStyleSheet(QStringLiteral("QWidget#HUDToast {"
                                     " background-color: rgba(%1,%2,%3,%4);"
                                     " border: 1px solid rgba(%5,%6,%7,%8);"
                                     " border-radius: 8px; }")
                          .arg(theme::hudBackground.red())
                          .arg(theme::hudBackground.green())
                          .arg(theme::hudBackground.blue())
                          .arg(theme::hudBackground.alpha())
                          .arg(theme::hudBorder.red())
                          .arg(theme::hudBorder.green())
                          .arg(theme::hudBorder.blue())
                          .arg(theme::hudBorder.alpha()));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 8, 12, 8);
        auto *label = new QLabel(text, this);
        label->setStyleSheet(QStringLiteral("color: rgb(%1,%2,%3); background: transparent;")
                                 .arg(theme::hudForeground.red())
                                 .arg(theme::hudForeground.green())
                                 .arg(theme::hudForeground.blue()));
        layout->addWidget(label);
        adjustSize();
    }
};

// The toasts of one host, stacked from the top and centred, exactly like
// the reference's ToastManager (without the fade animation for now).
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
            Toast *oldest = toasts_.takeFirst();
            oldest->deleteLater();
        }
        relayout();
        toast->show();
        QTimer::singleShot(timeoutMs, toast, [this, toast]() {
            toasts_.removeAll(toast);
            toast->deleteLater();
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
    void relayout()
    {
        int y = kTopMargin;
        for (Toast *toast : toasts_) {
            const int x = (host_->width() - toast->width()) / 2;
            toast->move(qMax(0, x), y);
            y += toast->height() + kSpacing;
        }
    }

    QWidget *host_ = nullptr;
    QList<Toast *> toasts_;
};

} // namespace

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
