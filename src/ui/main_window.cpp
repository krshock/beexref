#include "main_window.h"

#include "cache/session_cache.h"
#include "constants.h"
#include "input_controller.h"
#include "levels.h"
#include "lod_manager.h"
#include "logging.h"
#include "settings.h"
#include "util/format.h"
#include "util/memory.h"

#include <QAction>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QTimer>

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
        input_->setSessionCache(sessionCache_);
    }
    view_->setMimeFilter([this](const QMimeData &data) { return input_->acceptsMimeData(data); });
    connect(view_, &View::mimeDropped, this, [this](const QMimeData *data, const QPointF &pos) {
        input_->insertMimeData(*data, pos, view_->transform().m11());
    });
    connect(view_, &View::documentModified, this, &MainWindow::updateTitle);
    connect(input_, &InputController::message, this, [](const QString &text) {
        logging::info(text);
    });
    connect(input_, &InputController::itemsInserted, this, [this]() {
        view_->lodManager()->logAudit(QStringLiteral("insert"));
        view_->lodManager()->evaluateNow();
        updateTitle();
    });
    setCentralWidget(view_);

    // A window always has a document: a new unsaved board until a file
    // is opened, so paste, drops and undo/redo work from the start.
    document_ = std::make_shared<doc::Document>(doc::Document::create());
    undoStack_.setDocument(document_.get());
    scene_->setDocument(document_);

    auto *editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    auto *undoAction = editMenu->addAction(QStringLiteral("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, [this]() { applyHistoryStep(true); });
    auto *redoAction = editMenu->addAction(QStringLiteral("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, [this]() { applyHistoryStep(false); });
    editMenu->addSeparator();
    auto *cutAction = editMenu->addAction(QStringLiteral("Cu&t"));
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, input_, &InputController::cut);
    auto *copyAction = editMenu->addAction(QStringLiteral("&Copy"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, input_, &InputController::copy);
    auto *pasteAction = editMenu->addAction(QStringLiteral("&Paste"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, [this]() {
        QPoint position = view_->viewport()->mapFromGlobal(QCursor::pos());
        if (!view_->viewport()->rect().contains(position))
            position = view_->viewport()->rect().center();
        input_->paste(view_->mapToScene(position), view_->transform().m11());
    });

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto *openAction = fileMenu->addAction(QStringLiteral("&Open..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFileDialog);
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto *zoomInAction = viewMenu->addAction(QStringLiteral("Zoom &In"));
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction, &QAction::triggered, this, [this]() {
        view_->zoomAt(120, view_->viewport()->rect().center());
    });
    auto *zoomOutAction = viewMenu->addAction(QStringLiteral("Zoom &Out"));
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction, &QAction::triggered, this, [this]() {
        view_->zoomAt(-120, view_->viewport()->rect().center());
    });
    auto *fitSceneAction = viewMenu->addAction(QStringLiteral("&Fit Scene"));
    fitSceneAction->setShortcut(QKeySequence(Qt::Key_F));
    connect(fitSceneAction, &QAction::triggered, view_, &View::fitScene);
    auto *fitSelectionAction = viewMenu->addAction(QStringLiteral("Fit &Selection"));
    fitSelectionAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));
    connect(fitSelectionAction, &QAction::triggered, view_, &View::fitSelection);

    setMinimumSize(400, 300);
    resize(500, 300);
    updateTitle();

    // The title shows live RAM usage and, for saved boards, the file
    // size, as the reference does.
    auto *titleTimer = new QTimer(this);
    connect(titleTimer, &QTimer::timeout, this, &MainWindow::updateTitle);
    titleTimer->start(2000);

    // Decode allocation limit, as the reference applies on startup; the
    // environment variable wins, matching the Python app.
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
    const bool changed = undo ? undoStack_.undo() : undoStack_.redo();
    if (!changed)
        return;
    scene_->syncDocument();
    if (document_)
        document_->setModified(!undoStack_.isClean());
    view_->lodManager()->evaluateNow();
    updateTitle();
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
