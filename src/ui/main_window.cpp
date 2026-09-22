#include "main_window.h"

#include "constants.h"
#include "input_controller.h"
#include "logging.h"
#include "settings.h"
#include "util/format.h"
#include "util/memory.h"

#include <QAction>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QTimer>

namespace ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    scene_ = new Scene(this);
    view_ = new View(this);
    loader_ = new LevelLoader(this);
    input_ = new InputController(scene_, &undoStack_, this);
    view_->setBoardScene(scene_);
    view_->setLevelLoader(loader_);
    view_->setMimeFilter([this](const QMimeData &data) { return input_->acceptsMimeData(data); });
    connect(view_, &View::mimeDropped, this, [this](const QMimeData *data, const QPointF &pos) {
        input_->insertMimeData(*data, pos, view_->transform().m11());
    });
    connect(input_, &InputController::message, this, [](const QString &text) {
        logging::info(text);
    });
    setCentralWidget(view_);

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
}

MainWindow::~MainWindow()
{
    // Order matters: stop workers before the document (and its board
    // connection and temp copy) goes away.
    view_->setLevelLoader(nullptr);
    loader_->shutdown();
    scene_->setDocument(nullptr);
    undoStack_.setDocument(nullptr);
    document_.reset();
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
    view_->fitScene();
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

void MainWindow::applyHistoryStep(bool undo)
{
    const bool changed = undo ? undoStack_.undo() : undoStack_.redo();
    if (!changed)
        return;
    scene_->syncDocument();
    if (document_)
        document_->setModified(!undoStack_.isClean());
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
