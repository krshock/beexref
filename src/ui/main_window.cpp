#include "main_window.h"

#include "actions.h"
#include "cache/session_cache.h"
#include "color_gamut.h"
#include "color_tools.h"
#include "hud.h"
#include "hud_preview.h"
#include "info_dialogs.h"
#include "metadata_panel.h"
#include "constants.h"
#include "input_controller.h"
#include "layout_ops.h"
#include "levels.h"
#include "lod_manager.h"
#include "logging.h"
#include "opacity_dialog.h"
#include "rendering.h"
#include "selection_ops.h"
#include "welcome_overlay.h"
#include "controls.h"
#include "controls_dialog.h"
#include "settings_dialog.h"
#include "settings.h"
#include "util/format.h"
#include "util/memory.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QImageReader>
#include <QPlainTextEdit>
#include <QUrl>
#include <QMenu>
#include <QSplitter>
#include <QClipboard>
#include <QDesktopServices>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QTimer>
#include <QToolTip>

namespace ui {
namespace {

// LOD settings from the INI, with the FIELDS defaults.
LodSettings loadLodSettings()
{
    settings::File file(settings::iniPath());
    file.load();
    LodSettings lod;
    lod.method = settings::valueOrDefault(file, QStringLiteral("Items/lod_method")).toString();
    lod.fractions =
        settings::valueOrDefault(file, QStringLiteral("Items/lod_fractions")).toString();
    lod.budgetMB = settings::valueOrDefault(file, QStringLiteral("Items/lod_ram_budget_mb")).toInt();
    lod.primaryBudgetMB =
        settings::valueOrDefault(file, QStringLiteral("Items/lod_primary_budget_mb")).toInt();
    lod.ramCacheMB =
        settings::valueOrDefault(file, QStringLiteral("Items/lod_ram_cache_mb")).toInt();
    lod.decodeThreads =
        settings::valueOrDefault(file, QStringLiteral("Items/lod_decode_threads")).toInt();
    lod.cacheSettlePercent =
        settings::valueOrDefault(file, QStringLiteral("Items/lod_cache_settle_percent")).toInt();
    lod.quality = settings::valueOrDefault(file, QStringLiteral("Items/lod_quality")).toString();
    return normalized(lod);
}

} // namespace

MainWindow::MainWindow(bool cacheDisabled, QWidget *parent)
    : QMainWindow(parent)
{
    scene_ = new Scene(this);
    view_ = new View(this);
    loader_ = new LevelLoader(this);
    input_ = new InputController(scene_, &undoStack_, this);
    view_->setBoardScene(scene_);
    view_->setLevelLoader(loader_);
    view_->setLodSettings(loadLodSettings());
    view_->setUndoStack(&undoStack_);

    // Session disk cache (decoded levels, detached payloads). The
    // Go ports' Items/disk_cache wins, then the Python key, then the
    // default; --no-cache forces it off for this run.
    {
        settings::File file(settings::iniPath());
        file.load();
        bool enabled = file.contains(QStringLiteral("Items"), QStringLiteral("disk_cache"))
            ? file.boolValue(QStringLiteral("Items"), QStringLiteral("disk_cache"), true)
            : file.boolValue(QStringLiteral("Items"), QStringLiteral("undo_cache"), true);
        if (cacheDisabled)
            enabled = false;
        if (enabled) {
            cache::SessionCache::sweepStale(settings::cacheDir());
            sessionCache_ = cache::SessionCache::create(settings::cacheDir());
            logging::info(QStringLiteral("Session cache"),
                          {{QStringLiteral("path"), sessionCache_->path()},
                           {QStringLiteral("available"), sessionCache_->isAvailable()}});
        } else {
            logging::info(QStringLiteral("Session cache: disabled"));
        }
        view_->lodManager()->setLevelCache(sessionCache_);
        view_->lodManager()->setSettings(view_->lodManager()->settings());
        input_->setSessionCache(sessionCache_);
    }
    view_->setMimeFilter([this](const QMimeData &data) { return input_->acceptsMimeData(data); });
    connect(view_, &View::mimeDropped, this, [this](const QMimeData *data, const QPointF &pos) {
        input_->insertMimeData(*data, pos, view_->transform().m11());
    });
    connect(view_, &View::documentModified, this, [this]() {
        updateTitle();
        updateActions();
    });
    connect(input_, &InputController::message, this, [](const QString &text) {
        logging::info(text);
    });
    connect(input_, &InputController::itemsInserted, this, [this]() {
        updateActions();
        view_->lodManager()->logAudit(QStringLiteral("insert"));
        view_->lodManager()->evaluateNow();
        updateTitle();
    });
    // The canvas and the metadata panel share a splitter: the panel is
    // a real right-hand side panel, hidden until asked for, with a
    // draggable divider (the Go port's layout).
    metadataPanel_ = new MetadataPanel(scene_, &undoStack_, this);
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->addWidget(view_);
    splitter_->addWidget(metadataPanel_);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 0);
    metadataPanel_->hide();
    setCentralWidget(splitter_);
    connect(splitter_, &QSplitter::splitterMoved, this, [this]() {
        const QList<int> sizes = splitter_->sizes();
        if (sizes.size() == 2 && metadataPanel_->isVisible() && sizes.at(1) > 0)
            panelWidth_ = sizes.at(1);
    });
    connect(metadataPanel_, &MetadataPanel::visibilityChanged, this, [this]() {
        if (!metadataPanel_->isVisible())
            return;
        // Restore the last width the user had, if the panel fits.
        if (splitter_->width() <= 0)
            return;
        const int wanted = qMin(metadataPanel_->preferredWidth(),
                                qMax(MetadataPanel::kMinWidth, splitter_->width() / 2));
        splitter_->setSizes({splitter_->width() - wanted, wanted});
    });
    connect(metadataPanel_, &MetadataPanel::modified, this, [this]() {
        updateActions();
        updateTitle();
    });
    connect(scene_, &Scene::itemViewAboutToBeRemoved, this,
            [this](SceneItem *view) { metadataPanel_->forgetItem(view); });

    // The empty-board overlay, a child of the canvas.
    welcomeOverlay_ = new WelcomeOverlay(view_);
    welcomeOverlay_->setMimeFilter(
        [this](const QMimeData &data) { return input_->acceptsMimeData(data); });
    connect(welcomeOverlay_, &WelcomeOverlay::openFileRequested, this,
            &MainWindow::openFileDialog);
    connect(welcomeOverlay_, &WelcomeOverlay::insertImagesRequested, this,
            &MainWindow::insertImages);
    connect(welcomeOverlay_, &WelcomeOverlay::undoRequested, this,
            [this]() { applyHistoryStep(true); });
    connect(welcomeOverlay_, &WelcomeOverlay::recentFileActivated, this,
            [this](const QString &path) { openBoard(path); });
    connect(welcomeOverlay_, &WelcomeOverlay::mimeDropped, this,
            [this](const QMimeData *data, const QPointF &pos) {
                input_->insertMimeData(*data, view_->mapToScene(pos.toPoint()),
                                       view_->transform().m11());
            });

    // A window always has a document: a new unsaved board until a file
    // is opened, so paste, drops and undo/redo work from the start.
    document_ = std::make_shared<doc::Document>(doc::Document::create());
    undoStack_.setDocument(document_.get());
    scene_->setDocument(document_);

    // The action registry and the menus of the reference's
    // actions/actions.py and actions/menu_structure.py.
    actions_ = new ActionRegistry(this);
    buildActions();
    buildMenus();
    // Menu-less actions (Shift+I for the metadata panel, for example)
    // only get live shortcuts once they are in the window's action list;
    // actions inside menus are registered by the menu itself.
    for (const QString &id : actions_->ids())
        addAction(actions_->action(id));
    applyShortcuts();

    connect(view_, &View::colorSampled, this, &MainWindow::copySampledColor);
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this]() {
        updateActions();
        metadataPanel_->refresh();
    });
    undoStack_.addChangedCallback([this]() {
        updateActions();
        updateWelcomeOverlay();
    });
    connect(scene_, &Scene::itemsChanged, this, [this]() {
        updateActions();
        updateWelcomeOverlay();
    });
    updateActions();

    setMinimumSize(400, 300);
    resize(500, 300);
    updateWelcomeOverlay();
    updateTitle();

    // The title shows live RAM usage and, for saved boards, the file
    // size, as the reference does.
    auto *titleTimer = new QTimer(this);
    connect(titleTimer, &QTimer::timeout, this, &MainWindow::updateTitle);
    titleTimer->start(2000);

    // Decode allocation limit, as the reference applies on startup (the
    // environment variable wins, matching the Python app).
    applyAllocationLimit();
}

MainWindow::~MainWindow()
{
    // Order matters: stop workers before the document (and its board
    // connection and temp copy) goes away; the cache file goes last,
    // after the workers that read and write it.
    view_->setLevelLoader(nullptr);
    loader_->shutdown();
    scene_->setDocument(nullptr);
    undoStack_.setDocument(nullptr);
    document_.reset();
    sessionCache_.reset();
}

bool MainWindow::openBoard(const QString &path)
{
    auto opened = doc::Document::open(path, settings::cacheDir());
    if (!opened) {
        logging::error(QStringLiteral("Cannot open board"),
                       {{QStringLiteral("file"), path},
                        {QStringLiteral("error"), opened.error().toString()}});
        QMessageBox::warning(this, QStringLiteral("Cannot open board"),
                             opened.error().toString());
        return false;
    }

    document_ = std::make_shared<doc::Document>(std::move(opened.take()));
    undoStack_.setDocument(document_.get());
    undoStack_.clear();
    scene_->setDocument(document_);
    view_->setLodSettings(loadLodSettings());
    view_->fitScene();
    view_->lodManager()->logAudit(QStringLiteral("open"));
    metadataPanel_->closePanel();

    // Record it for Open Recent, like the reference's loading callback.
    {
        settings::File file(settings::iniPath());
        file.load();
        file.updateRecentFiles(path);
    }
    rebuildRecentMenu();
    updateActions();
    updateTitle();
    logging::info(QStringLiteral("Board opened"),
                  {{QStringLiteral("file"), path},
                   {QStringLiteral("items"), document_->items().size()}});
    return true;
}

void MainWindow::openFileDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Board"), settings::configDir(),
        QStringLiteral("BeeXRef board (*.beex *.bee)"));
    if (!path.isEmpty())
        openBoard(path);
}

void MainWindow::saveDocument()
{
    view_->cancelCrop();
    view_->cancelSampleColor();
    const QString path = document_ ? document_->path() : QString();
    // A legacy .bee file is import-only; a document without a file must
    // be saved as one (the reference's on_action_save).
    if (path.isEmpty() || path.endsWith(QStringLiteral(".bee"), Qt::CaseInsensitive))
        saveDocumentAs();
    else
        saveDocumentTo(path, false);
}

void MainWindow::saveDocumentAs()
{
    view_->cancelCrop();
    view_->cancelSampleColor();
    const QString path = document_ ? document_->path() : QString();
    const QString startDir =
        path.isEmpty() ? settings::configDir() : QFileInfo(path).absolutePath();
    const QString filename = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save file"), startDir,
        QStringLiteral("BeeXRef File (*.beex)"));
    if (!filename.isEmpty())
        saveDocumentTo(filename, true);
}

void MainWindow::exportBee()
{
    view_->cancelCrop();
    view_->cancelSampleColor();
    const QString path = document_ ? document_->path() : QString();
    const QString startDir =
        path.isEmpty() ? settings::configDir() : QFileInfo(path).absolutePath();
    const QString filename = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export BeeRef File"), startDir,
        QStringLiteral("BeeRef File (*.bee)"));
    if (filename.isEmpty())
        return;
    exportBeeTo(filename);
}

bool MainWindow::exportBeeTo(const QString &path)
{
    if (!document_)
        return false;

    QString filename = path;
    if (!filename.endsWith(QStringLiteral(".bee"), Qt::CaseInsensitive))
        filename += QStringLiteral(".bee");

    QProgressDialog progress(QStringLiteral("Exporting %1").arg(filename), QString(), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    const board::Progress report = [&progress](int done, int total) {
        if (total <= 0)
            return;
        progress.setMaximum(total);
        progress.setValue(done);
        QCoreApplication::processEvents();
    };

    const board::Status status = document_->exportBee(filename, report);
    progress.close();
    if (!status) {
        logging::error(QStringLiteral("Cannot export file"),
                       {{QStringLiteral("file"), filename},
                        {QStringLiteral("error"), status.error().toString()}});
        QMessageBox::warning(this, QStringLiteral("Problem exporting file"),
                             status.error().toString());
        return false;
    }
    logging::info(QStringLiteral("File exported"),
                  {{QStringLiteral("file"), filename},
                   {QStringLiteral("items"), document_->items().size()}});
    return true;
}

bool MainWindow::saveDocumentTo(const QString &path, bool createNew)
{    if (!document_)
        return false;

    // Native saves always use .beex; .bee is import only.
    QString filename = path;
    if (!filename.endsWith(QStringLiteral(".beex"), Qt::CaseInsensitive))
        filename += QStringLiteral(".beex");

    // Appears only if writing takes longer than the minimum duration.
    QProgressDialog progress(QStringLiteral("Saving %1").arg(filename), QString(), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    const board::Progress report = [&progress](int done, int total) {
        if (total <= 0)
            return;
        progress.setMaximum(total);
        progress.setValue(done);
        QCoreApplication::processEvents();
    };

    const board::Status status = document_->save(filename, true, report, createNew);
    progress.close();
    if (!status) {
        logging::error(QStringLiteral("Cannot save file"),
                       {{QStringLiteral("file"), filename},
                        {QStringLiteral("error"), status.error().toString()}});
        QMessageBox::warning(this, QStringLiteral("Problem saving file"), status.error().toString());
        return false;
    }

    document_->setPath(filename);
    undoStack_.setClean();
    document_->setModified(false);
    // The bytes now live in the file; free the in-RAM copies. LOD
    // levels stay in RAM.
    document_->adoptFileSources();
    view_->lodManager()->evaluateNow();

    settings::File file(settings::iniPath());
    file.load();
    file.updateRecentFiles(filename);
    rebuildRecentMenu();
    updateActions();
    updateTitle();
    logging::info(QStringLiteral("File saved"),
                  {{QStringLiteral("file"), filename},
                   {QStringLiteral("items"), document_->items().size()}});
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmDiscardChanges(
            QStringLiteral("There are unsaved changes. Are you sure you want to quit?"))) {
        event->ignore();
        return;
    }
    event->accept();
}

void MainWindow::startMemoryAudit(int seconds)
{
    if (seconds <= 0)
        return;
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        view_->lodManager()->logAudit(QStringLiteral("periodic"));
    });
    timer->start(seconds * 1000);
}

void MainWindow::applyHistoryStep(bool undo)
{
    // The reference cancels active modes (crop, sampling) before an
    // undo or redo touches the items they edit.
    view_->cancelCrop();
    view_->cancelSampleColor();
    const bool changed = undo ? undoStack_.undo() : undoStack_.redo();
    if (!changed)
        return;
    scene_->syncDocument();
    if (document_)
        document_->setModified(!undoStack_.isClean());
    view_->lodManager()->evaluateNow();
    updateActions();
    updateTitle();
}

void MainWindow::afterSelectionAction()
{
    // Scene-wide edits cancel an active crop, like the reference's
    // cancel_active_modes().
    view_->cancelCrop();
    view_->cancelSampleColor();
    if (document_)
        document_->setModified(true);
    view_->refreshSceneRect();
    view_->lodManager()->evaluateNow();
    updateActions();
    updateTitle();
}

void MainWindow::openSettingsDialog()
{
    auto *dialog = new SettingsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    connect(dialog, &SettingsDialog::settingChanged, this, &MainWindow::applySettingChanged);
    connect(dialog, &SettingsDialog::settingsRestored, this, [this]() {
        applyAllocationLimit();
        view_->setLodSettings(loadLodSettings());
        view_->lodManager()->evaluateNow();
    });
    dialog->show();
}

void MainWindow::toggleHudPreview()
{
    if (!hudPreview_) {
        hudPreview_ = new HudPreview(view_);
        hudPreview_->hide();
    }
    if (hudPreview_->isVisible()) {
        hudPreview_->hide();
        return;
    }
    hudPreview_->moveToTopRight();
    hudPreview_->show();
}

void MainWindow::openHelp()
{
    auto *dialog = new HelpDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, QStringLiteral("About %1").arg(QString::fromLatin1(constants::AppName)),
        QStringLiteral("<h2>%1 %2</h2><p>%3</p><p>%4</p>")
            .arg(QString::fromLatin1(constants::AppName),
                 QString::fromLatin1(constants::Version),
                 QString::fromLatin1(constants::AppNameFull),
                 QString::fromUtf8(constants::Copyright)));
}

void MainWindow::openDebugLog()
{
    auto *dialog = new DebugLogDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::openControlsDialog()
{
    auto *dialog =
        new ControlsDialog(this, actions_, [this](const QString &id) { return actionLabel(id); });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ControlsDialog::controlsChanged, this, [this]() {
        // Both take effect at once: the bindings for new events, the
        // shortcuts on the actions.
        view_->setBindings(controls::Bindings::load());
        applyShortcuts();
    });
    dialog->show();
}

void MainWindow::applyShortcuts()
{
    if (!actions_)
        return;
    const controls::Store store;
    for (const QString &id : actions_->ids()) {
        const QStringList defaults = actions_->defaultShortcuts(id);
        actions_->setShortcuts(id, store.actionShortcuts(id, defaults));
    }
}

QString MainWindow::actionLabel(const QString &id) const
{
    QAction *action = actions_ ? actions_->action(id) : nullptr;
    if (!action)
        return id;

    // The reference shows the menu path joined with the action text.
    QStringList path;
    QList<QMenu *> menus = menuBar()->findChildren<QMenu *>();
    for (QMenu *menu : menus) {
        if (!menu->actions().contains(action))
            continue;
        // Walk upwards to the menu bar for the full path.
        QWidget *owner = menu->parentWidget();
        while (auto *parentMenu = qobject_cast<QMenu *>(owner)) {
            path.prepend(QString(parentMenu->title()).remove(QLatin1Char('&')));
            owner = parentMenu->parentWidget();
        }
        path.prepend(QString(menu->title()).remove(QLatin1Char('&')));
        break;
    }

    QString text = action->text();
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QStringLiteral("...")))
        text.chop(3);
    path.append(text);
    return path.join(QStringLiteral(": "));
}

void MainWindow::openSettingsDir()
{
    const QString dir = QFileInfo(settings::iniPath()).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::applySettingChanged(const QString &key)
{
    if (key == QLatin1String("Items/image_allocation_limit")) {
        applyAllocationLimit();
        return;
    }
    if (key.startsWith(QLatin1String("Items/lod_"))) {
        // LOD settings take effect at once, like the reference's
        // lod_changed event.
        view_->setLodSettings(loadLodSettings());
        view_->lodManager()->evaluateNow();
        return;
    }
    // arrange_gap and arrange_default are read when used; the storage
    // format and cache settings apply on the next save or run.
}

void MainWindow::applyAllocationLimit()
{
    settings::File file(settings::iniPath());
    file.load();
    int limit =
        settings::valueOrDefault(file, QStringLiteral("Items/image_allocation_limit")).toInt();
    if (qEnvironmentVariableIsSet("QT_IMAGEIO_MAXALLOC")) {
        bool ok = false;
        const int fromEnv = qEnvironmentVariableIntValue("QT_IMAGEIO_MAXALLOC", &ok);
        if (ok)
            limit = fromEnv;
    }
    QImageReader::setAllocationLimit(limit);
}

bool MainWindow::confirmDiscardChanges(const QString &message)
{
    if (!document_ || !document_->isModified())
        return true;
    settings::File file(settings::iniPath());
    file.load();
    if (!file.boolValue(QStringLiteral("Save"), QStringLiteral("confirm_close_unsaved"), true))
        return true;
    return QMessageBox::question(this, QStringLiteral("Discard unsaved changes?"), message,
                                 QMessageBox::Yes | QMessageBox::Cancel)
        == QMessageBox::Yes;
}

void MainWindow::newScene()
{
    if (!confirmDiscardChanges(
            QStringLiteral("There are unsaved changes. Are you sure you want to open a new "
                           "scene?")))
        return;

    view_->cancelCrop();
    view_->cancelSampleColor();
    document_ = std::make_shared<doc::Document>(doc::Document::create());
    undoStack_.setDocument(document_.get());
    undoStack_.clear();
    scene_->setDocument(document_);
    // The reference resets the view transform for a new scene.
    view_->setTransform(QTransform());
    view_->refreshSceneRect();
    view_->lodManager()->evaluateNow();
    metadataPanel_->closePanel();
    updateActions();
    updateTitle();
}

void MainWindow::selectAll()
{
    // Active modes first, like the reference's select_all_items().
    view_->cancelCrop();
    view_->cancelSampleColor();
    for (QGraphicsItem *item : scene_->items())
        item->setSelected(true);
    updateActions();
}

void MainWindow::deselectAll()
{
    view_->cancelCrop();
    view_->cancelSampleColor();
    scene_->clearSelection();
    updateActions();
}

void MainWindow::deleteSelection()
{
    view_->cancelCrop();
    view_->cancelSampleColor();
    input_->removeSelection();
    updateActions();
}

void MainWindow::raiseSelectionToTop()
{
    selection::raiseToTop(*scene_, undoStack_);
    afterSelectionAction();
}

void MainWindow::lowerSelectionToBottom()
{
    selection::lowerToBottom(*scene_, undoStack_);
    afterSelectionAction();
}

void MainWindow::pasteAtPointer()
{
    QPoint position = view_->viewport()->mapFromGlobal(QCursor::pos());
    if (!view_->viewport()->rect().contains(position))
        position = view_->viewport()->rect().center();
    input_->paste(view_->mapToScene(position), view_->transform().m11());
    updateActions();
}

void MainWindow::insertImages()
{
    view_->cancelCrop();
    view_->cancelSampleColor();

    QStringList patterns;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    for (const QByteArray &format : formats)
        patterns << QStringLiteral("*.") + QString::fromLatin1(format).toLower();
    const QString filter =
        QStringLiteral("Images (%1)").arg(patterns.join(QLatin1Char(' ')));

    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Select one or more images to open"), QString(), filter);
    if (files.isEmpty())
        return;

    QMimeData mime;
    QList<QUrl> urls;
    urls.reserve(files.size());
    for (const QString &file : files)
        urls.append(QUrl::fromLocalFile(file));
    mime.setUrls(urls);

    scene_->clearSelection();
    input_->insertMimeData(mime, view_->mapToScene(view_->viewport()->rect().center()),
                           view_->transform().m11());
    updateActions();
}

QStringList MainWindow::configuredRecentFiles() const
{
    settings::File file(settings::iniPath());
    file.load();
    return file.recentFiles(true);
}

void MainWindow::updateWelcomeOverlay()
{
    if (!welcomeOverlay_)
        return;
    const bool empty = !document_ || document_->items().isEmpty();
    if (!empty) {
        welcomeOverlay_->hideOverlay();
        return;
    }

    const QString path = document_ ? document_->path() : QString();
    if (path.isEmpty()) {
        welcomeOverlay_->setMode(WelcomeOverlay::Mode::Start);
        welcomeOverlay_->setRecentFiles(configuredRecentFiles());
    } else {
        welcomeOverlay_->setMode(WelcomeOverlay::Mode::Empty);
        welcomeOverlay_->setBoardName(QFileInfo(path).fileName());
        welcomeOverlay_->setRecentFiles({});
    }
    welcomeOverlay_->setUndoState(undoStack_.canUndo(), undoStack_.undoText());
    welcomeOverlay_->resize(view_->size());
    welcomeOverlay_->showOverlay();
}

void MainWindow::rebuildRecentMenu()
{
    if (!recentMenu_)
        return;
    recentMenu_->clear();
    const QStringList files = configuredRecentFiles();
    for (const QString &path : files) {
        const QString name = QFileInfo(path).fileName();
        QAction *action = recentMenu_->addAction(name.isEmpty() ? path : name);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path]() { openBoard(path); });
    }
    recentMenu_->setEnabled(!files.isEmpty());
}

void MainWindow::copySampledColor(const QColor &color)
{
    const QString hex = colors::hex(color);
    QApplication::clipboard()->setText(hex);
    // A later paste should not restore the items copied before.
    input_->clearInternalClipboard();
    // The reference's notification toast.
    hud::toast(view_, QStringLiteral("Copied color to clipboard: %1").arg(hex));
    logging::info(QStringLiteral("Sampled color"), {{QStringLiteral("color"), hex}});
}

void MainWindow::showColorGamut()
{
    if (!scene_)
        return;
    const QVector<SceneItem *> images = selection::imageSelection(*scene_);
    if (images.size() != 1)
        return;
    SceneItem *view = images.first();
    auto *dialog = new GamutDialog(this, view->item(), view->level());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::normalizeSelection(int mode)
{
    layout::Normalize normalizeMode = layout::Normalize::Height;
    if (mode == 1)
        normalizeMode = layout::Normalize::Width;
    else if (mode == 2)
        normalizeMode = layout::Normalize::Size;
    layout::normalize(*scene_, undoStack_, normalizeMode);
    afterSelectionAction();
}

void MainWindow::arrangeSelection(int mode)
{
    layout::Arrange arrangeMode = layout::Arrange::Horizontal;
    if (mode == 1)
        arrangeMode = layout::Arrange::Vertical;
    else if (mode == 2)
        arrangeMode = layout::Arrange::Square;

    // Settings are read at use time, like the reference.
    settings::File file(settings::iniPath());
    file.load();
    bool ok = false;
    const int gap =
        file.value(QStringLiteral("Items"), QStringLiteral("arrange_gap"), QStringLiteral("0"))
            .toInt(&ok);
    layout::arrange(*scene_, undoStack_, arrangeMode, ok ? qBound(0, gap, 200) : 0);
    afterSelectionAction();
}

void MainWindow::arrangeSelectionDefault()
{
    settings::File file(settings::iniPath());
    file.load();
    const QString configured =
        file.value(QStringLiteral("Items"), QStringLiteral("arrange_default"),
                   QStringLiteral("optimal"));
    bool ok = false;
    const int gap =
        file.value(QStringLiteral("Items"), QStringLiteral("arrange_gap"), QStringLiteral("0"))
            .toInt(&ok);
    layout::arrange(*scene_, undoStack_, layout::arrangeModeFromSetting(configured),
                    ok ? qBound(0, gap, 200) : 0);
    afterSelectionAction();
}

void MainWindow::changeOpacity()
{
    const QVector<SceneItem *> images = selection::imageSelection(*scene_);
    if (images.isEmpty())
        return;

    QVector<doc::ChangeItemCommand::State> before;
    before.reserve(images.size());
    for (SceneItem *view : images)
        before.append(doc::ChangeItemCommand::State::capture(*view->item()));

    OpacityDialog dialog(this, qRound(images.first()->item()->opacity() * 100.0));
    connect(&dialog, &OpacityDialog::percentChanged, this, [this](int percent) {
        selection::applyOpacity(*scene_, percent / 100.0);
    });

    const bool accepted = dialog.exec() == QDialog::Accepted;
    const int percent = dialog.percent();

    // Undo the live preview, then commit the chosen value as one step,
    // so undo returns to the opacity from before the dialog.
    for (qsizetype i = 0; i < images.size(); ++i)
        before.at(i).apply(*images.at(i)->item());
    for (SceneItem *view : images)
        view->applyModelState();
    if (accepted)
        selection::setOpacity(*scene_, undoStack_, percent / 100.0);
    afterSelectionAction();
}

void MainWindow::buildActions()
{
    using G = ActionGroup;

    // File.
    actions_->add(QStringLiteral("new_scene"), QStringLiteral("&New Scene"),
                  QKeySequence(QStringLiteral("Ctrl+N")), G::Always,
                  [this](bool) { newScene(); });
    actions_->add(QStringLiteral("open"), QStringLiteral("&Open"), QKeySequence::Open, G::Always,
                  [this](bool) { openFileDialog(); });
    actions_->add(QStringLiteral("save"), QStringLiteral("&Save"),
                  QKeySequence(QStringLiteral("Ctrl+S")), G::ItemsInScene,
                  [this](bool) { saveDocument(); });
    actions_->add(QStringLiteral("save_as"), QStringLiteral("Save &As..."),
                  QKeySequence(QStringLiteral("Ctrl+Shift+S")), G::ItemsInScene,
                  [this](bool) { saveDocumentAs(); });
    actions_->add(QStringLiteral("export_bee"),
                  QStringLiteral("Export BeeRef File (.bee)"), {}, G::ItemsInScene,
                  [this](bool) { exportBee(); });
    actions_->add(QStringLiteral("quit"), QStringLiteral("&Quit"), QKeySequence::Quit, G::Always,
                  [this](bool) { close(); });

    // Edit.
    actions_->add(QStringLiteral("undo"), QStringLiteral("&Undo"), QKeySequence::Undo, G::CanUndo,
                  [this](bool) { applyHistoryStep(true); });
    actions_->add(QStringLiteral("redo"), QStringLiteral("&Redo"),
                  QKeySequence(QStringLiteral("Ctrl+Shift+Z")), G::CanRedo,
                  [this](bool) { applyHistoryStep(false); });
    actions_->add(QStringLiteral("select_all"), QStringLiteral("&Select All"),
                  QKeySequence::SelectAll, G::Always, [this](bool) { selectAll(); });
    actions_->add(QStringLiteral("deselect_all"), QStringLiteral("Deselect &All"),
                  QKeySequence(QStringLiteral("Ctrl+Shift+A")), G::Always,
                  [this](bool) { deselectAll(); });
    actions_->add(QStringLiteral("cut"), QStringLiteral("Cu&t"), QKeySequence::Cut, G::Selection,
                  [this](bool) { input_->cut(); updateActions(); });
    actions_->add(QStringLiteral("copy"), QStringLiteral("&Copy"), QKeySequence::Copy,
                  G::Selection, [this](bool) { input_->copy(); });
    actions_->add(QStringLiteral("paste"), QStringLiteral("&Paste"), QKeySequence::Paste,
                  G::Always, [this](bool) { pasteAtPointer(); });
    actions_->add(QStringLiteral("delete"), QStringLiteral("&Delete"),
                  QKeySequence(Qt::Key_Delete), G::Selection,
                  [this](bool) { deleteSelection(); });
    actions_->add(QStringLiteral("raise_to_top"), QStringLiteral("&Raise to Top"),
                  QKeySequence(Qt::Key_PageUp), G::Selection,
                  [this](bool) { raiseSelectionToTop(); });
    actions_->add(QStringLiteral("lower_to_bottom"), QStringLiteral("Lower to Bottom"),
                  QKeySequence(Qt::Key_PageDown), G::Selection,
                  [this](bool) { lowerSelectionToBottom(); });

    // View.
    actions_->add(QStringLiteral("fit_scene"), QStringLiteral("&Fit Scene"),
                  QKeySequence(QStringLiteral("1")), G::Always,
                  [this](bool) { view_->fitScene(); });
    actions_->add(QStringLiteral("fit_selection"), QStringLiteral("Fit &Selection"),
                  QKeySequence(QStringLiteral("2")), G::Selection,
                  [this](bool) { view_->fitSelection(); });

    // Insert.
    actions_->add(QStringLiteral("insert_images"), QStringLiteral("&Images..."),
                  QKeySequence(QStringLiteral("Ctrl+I")), G::Always,
                  [this](bool) { insertImages(); });

    // Transform.
    actions_->add(QStringLiteral("crop"), QStringLiteral("&Crop"),
                  QKeySequence(QStringLiteral("Shift+C")), G::SingleImage,
                  [this](bool) { view_->cropSelection(); });
    actions_->add(QStringLiteral("flip_horizontally"), QStringLiteral("Flip &Horizontally"),
                  QKeySequence(QStringLiteral("H")), G::Selection, [this](bool) {
                      selection::flip(*scene_, undoStack_, false);
                      afterSelectionAction();
                  });
    actions_->add(QStringLiteral("flip_vertically"), QStringLiteral("Flip &Vertically"),
                  QKeySequence(QStringLiteral("V")), G::Selection, [this](bool) {
                      selection::flip(*scene_, undoStack_, true);
                      afterSelectionAction();
                  });
    actions_->add(QStringLiteral("reset_scale"), QStringLiteral("Reset &Scale"), {}, G::Selection,
                  [this](bool) {
                      selection::resetScale(*scene_, undoStack_);
                      afterSelectionAction();
                  });
    actions_->add(QStringLiteral("reset_rotation"), QStringLiteral("Reset &Rotation"), {},
                  G::Selection, [this](bool) {
                      selection::resetRotation(*scene_, undoStack_);
                      afterSelectionAction();
                  });
    actions_->add(QStringLiteral("reset_flip"), QStringLiteral("Reset &Flip"), {}, G::Selection,
                  [this](bool) {
                      selection::resetFlip(*scene_, undoStack_);
                      afterSelectionAction();
                  });
    actions_->add(QStringLiteral("reset_crop"), QStringLiteral("Reset Cro&p"), {}, G::Selection,
                  [this](bool) {
                      selection::resetCrop(*scene_, undoStack_);
                      afterSelectionAction();
                  });
    actions_->add(QStringLiteral("reset_transforms"), QStringLiteral("Reset &All"),
                  QKeySequence(QStringLiteral("R")), G::Selection, [this](bool) {
                      selection::resetTransforms(*scene_, undoStack_);
                      afterSelectionAction();
                  });

    // Normalize.
    actions_->add(QStringLiteral("normalize_height"), QStringLiteral("&Height"),
                  QKeySequence(QStringLiteral("Shift+H")), G::Selection,
                  [this](bool) { normalizeSelection(0); });
    actions_->add(QStringLiteral("normalize_width"), QStringLiteral("&Width"),
                  QKeySequence(QStringLiteral("Shift+W")), G::Selection,
                  [this](bool) { normalizeSelection(1); });
    actions_->add(QStringLiteral("normalize_size"), QStringLiteral("&Size"),
                  QKeySequence(QStringLiteral("Shift+S")), G::Selection,
                  [this](bool) { normalizeSelection(2); });

    // Arrange.
    actions_->add(QStringLiteral("arrange_optimal"), QStringLiteral("&Optimal"),
                  QKeySequence(QStringLiteral("Shift+O")), G::Selection, [this](bool) {
                      logging::info(QStringLiteral("Arrange optimal: not ported yet"));
                  });
    actions_->add(QStringLiteral("arrange_horizontal"),
                  QStringLiteral("&Horizontal (by filename)"), {}, G::Selection,
                  [this](bool) { arrangeSelection(0); });
    actions_->add(QStringLiteral("arrange_vertical"), QStringLiteral("&Vertical (by filename)"),
                  {}, G::Selection, [this](bool) { arrangeSelection(1); });
    actions_->add(QStringLiteral("arrange_square"), QStringLiteral("&Square (by filename)"), {},
                  G::Selection, [this](bool) { arrangeSelection(2); });
    // Not in the reference's menus: it runs this after imports, which do
    // not exist yet. "optimal" in Items/arrange_default maps to square.
    actions_->add(QStringLiteral("arrange_default"), QStringLiteral("Arrange &Default"), {},
                  G::Selection, [this](bool) { arrangeSelectionDefault(); });

    // Images.
    actions_->add(QStringLiteral("change_opacity"), QStringLiteral("Change &Opacity..."), {},
                  G::Selection, [this](bool) { changeOpacity(); });
    actions_->add(QStringLiteral("grayscale"), QStringLiteral("&Grayscale"),
                  QKeySequence(QStringLiteral("G")), G::Selection, [this](bool checked) {
                      selection::setGrayscale(*scene_, undoStack_, checked);
                      afterSelectionAction();
                  },
                  true, [this]() {
                      const QVector<SceneItem *> images = selection::imageSelection(*scene_);
                      return !images.isEmpty() && images.first()->item()->grayscale();
                  });
    actions_->add(QStringLiteral("show_color_gamut"), QStringLiteral("Show &Color Gamut"), {},
                  G::SingleImage, [this](bool) { showColorGamut(); });
    actions_->add(QStringLiteral("sample_color"), QStringLiteral("Sample Color"),
                  QKeySequence(QStringLiteral("S")), G::ItemsInScene,
                  [this](bool) { view_->startSampleColor(); });

    // View: window and rendering toggles. The reference's defaults are
    // session-only; Show Menu Bar starts checked here because the
    // reference's unchecked default would leave the menu unreachable
    // once hidden.
    actions_->add(QStringLiteral("fullscreen"), QStringLiteral("&Fullscreen"),
                  QKeySequence(Qt::Key_F11), G::Always,
                  [this](bool checked) {
                      checked ? showFullScreen() : showNormal();
                  },
                  true);
    actions_->add(QStringLiteral("always_on_top"), QStringLiteral("&Always On Top"), {},
                  G::Always,
                  [this](bool checked) {
                      const bool visible = isVisible();
                      setWindowFlag(Qt::WindowStaysOnTopHint, checked);
                      if (visible)
                          show();
                  },
                  true);
    actions_->add(QStringLiteral("show_scrollbars"), QStringLiteral("Show &Scrollbars"), {},
                  G::Always,
                  [this](bool checked) {
                      const Qt::ScrollBarPolicy policy =
                          checked ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
                      view_->setHorizontalScrollBarPolicy(policy);
                      view_->setVerticalScrollBarPolicy(policy);
                  },
                  true);
    QAction *menubarAction = actions_->add(
        QStringLiteral("show_menubar"), QStringLiteral("Show &Menu Bar"), {}, G::Always,
        [this](bool checked) { menuBar()->setVisible(checked); }, true);
    // Deliberate deviation: the reference defaults this off, which would
    // leave its own toggle unreachable once hidden.
    menubarAction->setChecked(true);
    QAction *titlebarAction = actions_->add(
        QStringLiteral("show_titlebar"), QStringLiteral("Show &Title Bar"), {}, G::Always,
        [this](bool checked) {
            const bool visible = isVisible();
            setWindowFlag(Qt::FramelessWindowHint, !checked);
            if (visible)
                show();
        },
        true);
    // The window starts with its title bar, like the reference's default.
    titlebarAction->setChecked(true);
    QAction *smoothAction = actions_->add(
        QStringLiteral("smooth_images"), QStringLiteral("&Smooth Images"), {}, G::Always,
        [this](bool checked) {
            rendering::setSmoothPixmaps(checked);
            view_->viewport()->update();
        },
        true);
    // Smooth drawing is on by default, matching rendering's own default.
    smoothAction->setChecked(true);
    actions_->add(QStringLiteral("move_window"), QStringLiteral("Move &Window"),
                  QKeySequence(QStringLiteral("Ctrl+M")), G::Always,
                  [this](bool) { view_->toggleMoveWindow(); });

    // Images: the metadata editor panel (shortcut only, like the
    // reference) and the info window.
    // The Go port's binding: the panel owns the I key, and its Info tab
    // replaces the old Image Info window.
    actions_->add(QStringLiteral("metadata_panel"), QStringLiteral("Edit Image &Metadata"),
                  QKeySequence(Qt::Key_I), G::SingleImage,
                  [this](bool) { metadataPanel_->toggle(); });

    // A runtime preview of the HUD style, shortcut only like the
    // reference (Ctrl+Shift+H).
    actions_->add(QStringLiteral("hud_preview"), QStringLiteral("Toggle &HUD Preview"),
                  QKeySequence(QStringLiteral("Ctrl+Shift+H")), G::Always,
                  [this](bool) { toggleHudPreview(); });

    // Settings.
    actions_->add(QStringLiteral("settings"), QStringLiteral("&Settings"), {}, G::Always,
                  [this](bool) { openSettingsDialog(); });
    actions_->add(QStringLiteral("keyboard_settings"), QStringLiteral("&Keyboard && Mouse"), {},
                  G::Always, [this](bool) { openControlsDialog(); });
    actions_->add(QStringLiteral("open_settings_dir"), QStringLiteral("&Open Settings Folder"),
                  {}, G::Always, [this](bool) { openSettingsDir(); });

    // Help.
    actions_->add(QStringLiteral("help"), QStringLiteral("&Help"), QKeySequence(Qt::Key_F1),
                  G::Always, [this](bool) { openHelp(); });
    // The reference binds Help twice.
    actions_->setDefaultShortcuts(QStringLiteral("help"),
                                  {QStringLiteral("F1"), QStringLiteral("Ctrl+H")});
    actions_->add(QStringLiteral("about"), QStringLiteral("&About"), {}, G::Always,
                  [this](bool) { showAbout(); });
    actions_->add(QStringLiteral("debuglog"), QStringLiteral("Show &Debug Log"), {}, G::Always,
                  [this](bool) { openDebugLog(); });
}

void MainWindow::buildMenus()
{
    // File: the Export submenu arrives with the export phase; the
    // reference's Open Recent submenu is rebuilt whenever it is shown.
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    actions_->append(fileMenu, QStringLiteral("new_scene"));
    actions_->append(fileMenu, QStringLiteral("open"));
    recentMenu_ = fileMenu->addMenu(QStringLiteral("Open &Recent"));
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    rebuildRecentMenu();
    actions_->appendSeparator(fileMenu);
    actions_->append(fileMenu, QStringLiteral("save"));
    actions_->append(fileMenu, QStringLiteral("save_as"));
    auto *exportMenu = fileMenu->addMenu(QStringLiteral("&Export"));
    actions_->append(exportMenu, QStringLiteral("export_bee"));
    actions_->appendSeparator(fileMenu);
    actions_->append(fileMenu, QStringLiteral("quit"));

    auto *editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    actions_->append(editMenu, QStringLiteral("undo"));
    actions_->append(editMenu, QStringLiteral("redo"));
    actions_->appendSeparator(editMenu);
    actions_->append(editMenu, QStringLiteral("select_all"));
    actions_->append(editMenu, QStringLiteral("deselect_all"));
    actions_->appendSeparator(editMenu);
    actions_->append(editMenu, QStringLiteral("cut"));
    actions_->append(editMenu, QStringLiteral("copy"));
    actions_->append(editMenu, QStringLiteral("paste"));
    actions_->append(editMenu, QStringLiteral("delete"));
    actions_->appendSeparator(editMenu);
    actions_->append(editMenu, QStringLiteral("raise_to_top"));
    actions_->append(editMenu, QStringLiteral("lower_to_bottom"));

    // View: the window and scrollbar toggles arrive with the window
    // slice.
    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    actions_->append(viewMenu, QStringLiteral("fit_scene"));
    actions_->append(viewMenu, QStringLiteral("fit_selection"));
    actions_->appendSeparator(viewMenu);
    actions_->append(viewMenu, QStringLiteral("fullscreen"));
    actions_->append(viewMenu, QStringLiteral("always_on_top"));
    actions_->append(viewMenu, QStringLiteral("show_scrollbars"));
    actions_->append(viewMenu, QStringLiteral("show_menubar"));
    actions_->append(viewMenu, QStringLiteral("show_titlebar"));
    actions_->append(viewMenu, QStringLiteral("smooth_images"));
    actions_->appendSeparator(viewMenu);
    actions_->append(viewMenu, QStringLiteral("move_window"));

    // Insert: text arrives with text editing.
    auto *insertMenu = menuBar()->addMenu(QStringLiteral("&Insert"));
    actions_->append(insertMenu, QStringLiteral("insert_images"));

    auto *transformMenu = menuBar()->addMenu(QStringLiteral("&Transform"));
    actions_->append(transformMenu, QStringLiteral("crop"));
    actions_->append(transformMenu, QStringLiteral("flip_horizontally"));
    actions_->append(transformMenu, QStringLiteral("flip_vertically"));
    actions_->appendSeparator(transformMenu);
    actions_->append(transformMenu, QStringLiteral("reset_scale"));
    actions_->append(transformMenu, QStringLiteral("reset_rotation"));
    actions_->append(transformMenu, QStringLiteral("reset_flip"));
    actions_->append(transformMenu, QStringLiteral("reset_crop"));
    actions_->append(transformMenu, QStringLiteral("reset_transforms"));

    auto *normalizeMenu = menuBar()->addMenu(QStringLiteral("&Normalize"));
    actions_->append(normalizeMenu, QStringLiteral("normalize_height"));
    actions_->append(normalizeMenu, QStringLiteral("normalize_width"));
    actions_->append(normalizeMenu, QStringLiteral("normalize_size"));

    auto *arrangeMenu = menuBar()->addMenu(QStringLiteral("&Arrange"));
    actions_->append(arrangeMenu, QStringLiteral("arrange_optimal"));
    actions_->append(arrangeMenu, QStringLiteral("arrange_horizontal"));
    actions_->append(arrangeMenu, QStringLiteral("arrange_vertical"));
    actions_->append(arrangeMenu, QStringLiteral("arrange_square"));
    actions_->appendSeparator(arrangeMenu);
    actions_->append(arrangeMenu, QStringLiteral("arrange_default"));

    auto *imagesMenu = menuBar()->addMenu(QStringLiteral("&Images"));
    actions_->append(imagesMenu, QStringLiteral("change_opacity"));
    actions_->append(imagesMenu, QStringLiteral("grayscale"));
    actions_->appendSeparator(imagesMenu);
    actions_->append(imagesMenu, QStringLiteral("show_color_gamut"));
    actions_->append(imagesMenu, QStringLiteral("sample_color"));

    // Settings: the Keyboard & Mouse editor arrives with the bindings.
    auto *settingsMenu = menuBar()->addMenu(QStringLiteral("&Settings"));
    actions_->append(settingsMenu, QStringLiteral("settings"));
    actions_->append(settingsMenu, QStringLiteral("keyboard_settings"));
    actions_->append(settingsMenu, QStringLiteral("open_settings_dir"));

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    actions_->append(helpMenu, QStringLiteral("help"));
    actions_->append(helpMenu, QStringLiteral("about"));
    actions_->append(helpMenu, QStringLiteral("debuglog"));
}

void MainWindow::updateActions()
{
    if (!actions_)
        return;
    ActionState state;
    state.itemsInScene = document_ && !document_->items().isEmpty();
    const QVector<SceneItem *> selected = selection::selectionItems(*scene_);
    state.selection = !selected.isEmpty();
    state.singleImage =
        selected.size() == 1 && selected.first()->isPixmap() && !selected.first()->isError();
    state.canUndo = undoStack_.canUndo();
    state.canRedo = undoStack_.canRedo();
    actions_->setState(state);
}

void MainWindow::updateTitle()
{
    const QString app = QString::fromLatin1(constants::AppName);
    const QString path = document_ ? document_->path() : QString();

    QString title = app;
    if (document_ && (!path.isEmpty() || document_->isModified())) {
        const QString name =
            path.isEmpty() ? QStringLiteral("[Untitled]") : QFileInfo(path).fileName();
        const QString modified = document_->isModified() ? QStringLiteral("*") : QString();
        title = QStringLiteral("%1%2 - %3").arg(name, modified, app);
    }

    QStringList extras;
    const qint64 rss = util::processRssBytes();
    if (rss > 0)
        extras.append(QStringLiteral("RAM %1").arg(util::formatSize(rss)));
    if (!path.isEmpty() && path.endsWith(QStringLiteral(".beex")) && QFileInfo::exists(path))
        extras.append(util::formatSize(QFileInfo(path).size()));
    if (!extras.isEmpty())
        title += QStringLiteral(" (%1)").arg(extras.join(QStringLiteral(" | ")));

    setWindowTitle(title);
}

} // namespace ui
