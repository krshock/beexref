#pragma once

#include "doc/undo.h"
#include "level_loader.h"
#include "scene.h"
#include "view.h"

#include <QMainWindow>

#include <memory>

namespace ui {

class InputController;

// Application window: the canvas plus the minimal actions the canvas
// phase needs. The full action registry and HUD arrive later.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens a board, replacing the current document. Reports failures
    // with a dialog and returns false.
    bool openBoard(const QString &path);

    View *view() const { return view_; }
    Scene *scene() const { return scene_; }
    InputController *input() const { return input_; }

private:
    void openFileDialog();
    void updateTitle();
    // Applies an undo/redo step and brings the scene in line.
    void applyHistoryStep(bool undo);

    Scene *scene_ = nullptr;
    View *view_ = nullptr;
    LevelLoader *loader_ = nullptr;
    InputController *input_ = nullptr;
    doc::UndoStack undoStack_;
    std::shared_ptr<doc::Document> document_;
};

} // namespace ui
