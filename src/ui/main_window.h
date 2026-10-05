#pragma once

#include "doc/undo.h"
#include "doc/image_export.h"
#include "level_loader.h"
#include "scene.h"
#include "view.h"

#include <QColor>
#include <QMainWindow>
#include <QPoint>

#include <functional>
#include <memory>

class QAction;
class QCloseEvent;
class QMenu;
class QSplitter;
class QTimer;
class QToolButton;

namespace cache {
class SessionCache;
}

namespace ui {

class ActionRegistry;
class InputController;
class HudPreview;
class MetadataPanel;
class WelcomeOverlay;

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

    // Writes the document to path (the engine behind Save and Save As;
    // also used by tests and the smoke harness). createNew clears the
    // item ids first and appends the .beex extension if missing. On
    // success the path, clean state, recent files and LOD sources are
    // updated. Reports failures with a dialog and returns false.
    bool saveDocumentTo(const QString &path, bool createNew);

    // Writes the legacy .bee to path (the engine behind Export BeeRef
    // File; also used by tests). Reports failures with a dialog and
    // returns false. The document's path and clean state are untouched.
    bool exportBeeTo(const QString &path);

    // Renders the scene to path (the engine behind Export Scene; also
    // used by tests). PNG/JPEG ask for a size first; SVG exports at the
    // default size. Returns false on a cancelled dialog or a write
    // failure.
    bool exportSceneTo(const QString &path);

    // Exports every pixmap item into dir (the engine behind Export
    // Images; also used by tests). `resolve` answers file conflicts;
    // pass an empty function to overwrite silently. Returns false when
    // the user cancels a conflict prompt.
    bool exportImagesTo(
        const QString &dir,
        const std::function<std::optional<doc::ExportConflict>(const QString &)> &resolve = {});

    View *view() const { return view_; }
    MetadataPanel *metadataPanel() const { return metadataPanel_; }
    WelcomeOverlay *welcomeOverlay() const { return welcomeOverlay_; }
    Scene *scene() const { return scene_; }
    InputController *input() const { return input_; }
    // The action registry (menus, shortcuts, state groups); exposed so
    // tests can check the menu layout against it.
    ActionRegistry *actions() const { return actions_; }

    // Enables the periodic memory audit line (0 disables it).
    void startMemoryAudit(int seconds);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void openFileDialog();
    // Save (Ctrl+S) and Save As (Ctrl+Shift+S).
    void saveDocument();
    void saveDocumentAs();
    // File ▸ Export ▸ Export BeeRef File (.bee): the legacy upstream
    // format, for interchange with BeeRef only.
    void exportBee();
    // File ▸ Export ▸ Export Scene: the canvas as PNG/JPEG/SVG.
    void exportScene();
    // File ▸ Export ▸ Export Images: every image into a directory.
    void exportImages();
    void updateTitle();
    // Applies an undo/redo step and brings the scene in line.
    void applyHistoryStep(bool undo);
    // Refreshes the document state after a selection transformation.
    void afterSelectionAction();
    // The canvas context menu: the whole main menu as one popup (the Go
    // port's canvas menu), so every action stays reachable when the bars
    // are hidden.
    void showContextMenu(const QPoint &globalPos);
    // Refreshes the status bar's RAM/levels/items readout.
    void updateStatusBar();
    // The board-problem report: counts by kind, the full list in the
    // scrollable details area.
    void showDamageReport();
    // Change Opacity...: live preview in the dialog,
    // one undo step on OK, nothing on Cancel.
    void changeOpacity();
    // Builds the action registry and the menus of actions/menu_structure.
    void buildActions();
    void buildMenus();
    // Enables and checks every action for the current state.
    void updateActions();
    // The bottom-left plate: the selected image's name, author, year and
    // resolution for a moment; anything else clears it. A non-forced
    // update (after a metadata edit) only restarts the cycle when the
    // shown lines actually changed.
    void updateSelectionOsd(bool force = true);
    // Copies a sampled colour and says so (HUD toast).
    void copySampledColor(const QColor &color);
    // Opens the gamut wheel for the single selected image.
    void showColorGamut();
    // Settings menu: the settings dialog and the settings folder.
    void toggleHudPreview();
    // Applies a grayscale method: persists it, pushes it to the canvas
    // and refreshes the menu checkmarks.
    void setGrayscaleMethod(const QString &id);
    // Shows, hides and fills the empty-board overlay.
    void updateWelcomeOverlay();
    // The recent files from the settings, existing ones only.
    QStringList configuredRecentFiles() const;
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
    // Save/confirm_close_unsaved.
    bool confirmDiscardChanges(const QString &message);
    // Saves and replacements read the document: a draft still open in
    // the metadata panel becomes part of it first.
    void commitPendingMetadataDraft();
    // Items and Arrange menu handlers (Items/* settings are read at use
    // time).
    void normalizeSelection(int mode); // 0 height, 1 width, 2 size
    void arrangeSelection(int mode);   // 0 horizontal, 1 vertical, 2 square
    // Arrange default: the Items/arrange_default
    // setting picks between the three arrangements above.
    void arrangeSelectionDefault();

    Scene *scene_ = nullptr;
    View *view_ = nullptr;
    LevelLoader *loader_ = nullptr;
    InputController *input_ = nullptr;
    ActionRegistry *actions_ = nullptr;
    // The Items/grayscale_method value the canvas and the menu show.
    QString grayscaleMethod_;
    // The last content handed to the selection plate, so a metadata edit
    // that does not change it does not restart the animation.
    QString lastOsd_;
    QMenu *recentMenu_ = nullptr;
    int panelWidth_ = 320;
    QSplitter *splitter_ = nullptr;
    MetadataPanel *metadataPanel_ = nullptr;
    HudPreview *hudPreview_ = nullptr;
    WelcomeOverlay *welcomeOverlay_ = nullptr;
    QTimer *statusTimer_ = nullptr;
    QToolButton *damageBadge_ = nullptr;
    doc::UndoStack undoStack_;
    std::shared_ptr<cache::SessionCache> sessionCache_;
    std::shared_ptr<doc::Document> document_;
};

} // namespace ui
