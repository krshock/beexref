#pragma once

#include "doc/undo.h"
#include "level_loader.h"
#include "scene.h"
#include "view.h"

#include <QMainWindow>

#include <memory>

class QAction;

namespace cache {
class SessionCache;
}

namespace ui {

class InputController;

// Application window: the canvas plus the minimal actions the canvas
// phase needs. The full action registry and HUD arrive later.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // cacheDisabled is the --no-cache override.
    explicit MainWindow(bool cacheDisabled = false, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens a board, replacing the current document. Reports failures
    // with a dialog and returns false.
    bool openBoard(const QString &path);

    View *view() const { return view_; }
    Scene *scene() const { return scene_; }
    InputController *input() const { return input_; }

    // Enables the periodic memory audit line (0 disables it).
    void startMemoryAudit(int seconds);

private:
    void openFileDialog();
    void updateTitle();
    // Applies an undo/redo step and brings the scene in line.
    void applyHistoryStep(bool undo);
    // Refreshes the document state after a selection transformation.
    void afterSelectionAction();
    // The reference's Change Opacity...: live preview in the dialog,
    // one undo step on OK, nothing on Cancel.
    void changeOpacity();
    // Keeps the Grayscale action's enabled and checked state in step
    // with the selection.
    void updateSelectionActions();

    Scene *scene_ = nullptr;
    View *view_ = nullptr;
    LevelLoader *loader_ = nullptr;
    InputController *input_ = nullptr;
    QAction *grayscaleAction_ = nullptr;
    doc::UndoStack undoStack_;
    std::shared_ptr<cache::SessionCache> sessionCache_;
    std::shared_ptr<doc::Document> document_;
};

} // namespace ui
