#pragma once

#include "level_loader.h"
#include "scene.h"
#include "view.h"

#include <QMainWindow>

#include <memory>

namespace ui {

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

private:
    void openFileDialog();
    void updateTitle();

    Scene *scene_ = nullptr;
    View *view_ = nullptr;
    LevelLoader *loader_ = nullptr;
    std::shared_ptr<doc::Document> document_;
};

} // namespace ui
