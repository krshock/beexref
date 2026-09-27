#pragma once

#include <QString>

#include <memory>
#include <utility>
#include <vector>

class QKeyEvent;
class QMouseEvent;

namespace ui {

class View;

// One interactive mode of the canvas (the active modes).
// Tools live as long as the view and carry their own per-session state.
// The view forwards mouse and key events to the active tool; returning
// true means the event was consumed. A tool ends itself through cancel(),
// and cancelModes() cancels whatever is active, so a new mode is wired
// in one place instead of every call site.
class Tool
{
public:
    virtual ~Tool() = default;

    virtual QString id() const = 0;
    // True while the tool owns the interaction.
    virtual bool active() const = 0;
    // Ends the mode without applying anything.
    virtual void cancel() = 0;

    virtual bool mousePress(QMouseEvent *event)
    {
        Q_UNUSED(event);
        return false;
    }
    virtual bool mouseMove(QMouseEvent *event)
    {
        Q_UNUSED(event);
        return false;
    }
    virtual bool mouseRelease(QMouseEvent *event)
    {
        Q_UNUSED(event);
        return false;
    }
    virtual bool keyPress(QKeyEvent *event)
    {
        Q_UNUSED(event);
        return false;
    }
};

// Owns the view's tools and dispatches to whichever is active. The tools
// decide when they are active themselves, so activation needs no
// bookkeeping here; a handful of tools makes the linear scan cheap.
class ToolController
{
public:
    explicit ToolController(View *view);

    // Takes ownership; returns a borrowed pointer for the view's API.
    template <typename T, typename... Args>
    T *add(Args &&...args)
    {
        auto tool = std::make_unique<T>(std::forward<Args>(args)...);
        T *borrowed = tool.get();
        tools_.push_back(std::move(tool));
        return borrowed;
    }

    Tool *active() const;
    QString activeId() const;
    void cancel();

    bool mousePress(QMouseEvent *event);
    bool mouseMove(QMouseEvent *event);
    bool mouseRelease(QMouseEvent *event);
    bool keyPress(QKeyEvent *event);

private:
    View *view_ = nullptr;
    std::vector<std::unique_ptr<Tool>> tools_;
};

} // namespace ui
