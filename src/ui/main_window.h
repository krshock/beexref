#pragma once

#include "doc/undo.h"
#include "level_loader.h"
#include "scene.h"
#include "view.h"

#include <QColor>
#include <QMainWindow>

#include <memory>

class QAction;
class QMenu;
class QSplitter;

namespace cache {
class SessionCache;
}

namespace ui {

class ActionRegistry;
class InputController;
class HudPreview;
class MetadataPanel;

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
    MetadataPanel *metadataPanel() const { return metadataPanel_; }
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
    // Builds the action registry and the menus of actions/menu_structure.
    void buildActions();
    void buildMenus();
    // Enables and checks every action for the current state.
    void updateActions();
    // Copies a sampled colour and says so (the reference's HUD toast).
    void copySampledColor(const QColor &color);
    // Opens the gamut wheel for the single selected image.
    void showColorGamut();
    // Settings menu: the settings dialog and the settings folder.
    void toggleHudPreview();
    void openHelp();
    void showAbout();
    void openDebugLog();
    void openSettingsDialog();
    void openControlsDialog();
    void openSettingsDir();
    // Re-applies the stored shortcut overrides to every action.
    void applyShortcuts();
    // The "Menu: Action" label of an action, for the controls editor.
    QString actionLabel(const QString &id) const;
    void applySettingChanged(const QString &key);
    // Applies Items/image_allocation_limit to QImageReader.
    void applyAllocationLimit();
    // File menu: new, open, recent, insert.
    void newScene();
    void insertImages();
    void rebuildRecentMenu();
    // Edit menu: selection and z-order.
    void selectAll();
    void deselectAll();
    void deleteSelection();
    void raiseSelectionToTop();
    void lowerSelectionToBottom();
    void pasteAtPointer();
    // Asks before dropping unsaved changes, honouring
    // Save/confirm_close_unsaved like the reference.
    bool confirmDiscardChanges(const QString &message);
    // Items and Arrange menu handlers (Items/* settings are read at use
    // time, like the reference's valueOrDefault calls).
    void normalizeSelection(int mode); // 0 height, 1 width, 2 size
    void arrangeSelection(int mode);   // 0 horizontal, 1 vertical, 2 square
    // The reference's arrange_default: the Items/arrange_default
    // setting picks between the three arrangements above.
    void arrangeSelectionDefault();

    Scene *scene_ = nullptr;
    View *view_ = nullptr;
    LevelLoader *loader_ = nullptr;
    InputController *input_ = nullptr;
    ActionRegistry *actions_ = nullptr;
    QMenu *recentMenu_ = nullptr;
    int panelWidth_ = 320;
    QSplitter *splitter_ = nullptr;
    MetadataPanel *metadataPanel_ = nullptr;
    HudPreview *hudPreview_ = nullptr;
    doc::UndoStack undoStack_;
    std::shared_ptr<cache::SessionCache> sessionCache_;
    std::shared_ptr<doc::Document> document_;
};

} // namespace ui
