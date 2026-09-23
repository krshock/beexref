#include "welcome_overlay.h"

#include "constants.h"
#include "hud.h"
#include "theme.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace ui {
namespace {

// The reference's layout constants.
constexpr int kCardWidth = 460;
constexpr int kLogoSizeStart = 64;
constexpr int kLogoSizeEmpty = 40;
constexpr int kIconSize = 18;
constexpr int kFilesMinHeight = 430;
constexpr int kSmallHeight = 360;
constexpr int kLogoMinHeight = 280;
constexpr int kSubtitleMinHeight = 260;
constexpr int kHintMinHeight = 220;

const QString kStartHint = QStringLiteral("Paste or drop images here.<br>"
                                          "Right-click for more options.");
const QString kStartHintCompact = QStringLiteral("Paste or drop images here.");
const QString kEmptyHint = QStringLiteral("Drop images here, or press Ctrl+I to insert.");

QLabel *roleLabel(const QString &role, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(role);
    label->setAlignment(Qt::AlignCenter);
    return label;
}

} // namespace

WelcomeOverlay::WelcomeOverlay(QWidget *canvas)
    : QWidget(canvas)
    , canvas_(canvas)
{
    setObjectName(QStringLiteral("WelcomeOverlay"));
    logo_ = QPixmap(QStringLiteral(":/assets/logo.png"));
    if (canvas_)
        canvas_->installEventFilter(this);

    content_ = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content_);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(14);

    card_ = makeCard();
    auto *cardLayout = qobject_cast<hud::HudPanel *>(card_)->bodyLayout();
    logoLabel_ = new QLabel(card_);
    logoLabel_->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(logoLabel_, 0, Qt::AlignHCenter);
    titleLabel_ = roleLabel(QStringLiteral("HUDDisplay"), card_);
    subtitleLabel_ = roleLabel(QStringLiteral("HUDSubtitle"), card_);
    hintLabel_ = roleLabel(QStringLiteral("HUDHint"), card_);
    cardLayout->addWidget(titleLabel_);
    cardLayout->addWidget(subtitleLabel_);
    cardLayout->addWidget(hintLabel_);

    insertButton_ = makeButton(QStringLiteral("Insert Images…"), QStyle::SP_FileIcon);
    openButton_ = makeButton(QStringLiteral("Open File…"), QStyle::SP_DialogOpenButton);
    undoButton_ = makeButton(QStringLiteral("Undo"), QStyle::SP_ArrowBack);
    connect(insertButton_, &QPushButton::clicked, this, &WelcomeOverlay::insertImagesRequested);
    connect(openButton_, &QPushButton::clicked, this, &WelcomeOverlay::openFileRequested);
    connect(undoButton_, &QPushButton::clicked, this, &WelcomeOverlay::undoRequested);
    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    buttonRow->addStretch(1);
    for (QPushButton *button : {insertButton_, openButton_, undoButton_})
        buttonRow->addWidget(button);
    buttonRow->addStretch(1);
    cardLayout->addLayout(buttonRow);

    filesCard_ = makeCard();
    auto *filesLayout = qobject_cast<hud::HudPanel *>(filesCard_)->bodyLayout();
    auto *heading = roleLabel(QStringLiteral("HUDSubtitle"), filesCard_);
    heading->setText(QStringLiteral("Recent Files"));
    filesLayout->addWidget(heading);
    filesView_ = new QListWidget(filesCard_);
    filesView_->setObjectName(QStringLiteral("HUDRecentFiles"));
    filesView_->setFrameShape(QFrame::NoFrame);
    filesView_->setMaximumHeight(150);
    filesView_->setMouseTracking(true);
    filesLayout->addWidget(filesView_);
    filesCard_->hide();
    connect(filesView_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item)
            emit recentFileActivated(item->data(Qt::UserRole).toString());
    });

    contentLayout->addWidget(card_, 0, Qt::AlignHCenter);
    contentLayout->addWidget(filesCard_, 0, Qt::AlignHCenter);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addWidget(content_, 0, Qt::AlignCenter);
    layout->addStretch(1);

    setMode(Mode::Start);
    hide();
}

void WelcomeOverlay::setCanvas(QWidget *canvas)
{
    if (canvas_)
        canvas_->removeEventFilter(this);
    canvas_ = canvas;
    if (canvas_) {
        canvas_->installEventFilter(this);
        resize(canvas_->size());
    }
}

QWidget *WelcomeOverlay::makeCard()
{
    auto *card = new hud::HudPanel(content_);
    card->setMaximumWidth(kCardWidth);
    card->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    card->bodyLayout()->setContentsMargins(20, 16, 20, 16);
    card->bodyLayout()->setSpacing(6);
    return card;
}

QPushButton *WelcomeOverlay::makeButton(const QString &text, QStyle::StandardPixmap icon)
{
    auto *button = new QPushButton(text, card_);
    button->setObjectName(QStringLiteral("HUDButton"));
    button->setProperty("welcome", true);
    button->setAutoDefault(false);
    button->setIcon(style()->standardIcon(icon));
    button->setIconSize(QSize(kIconSize, kIconSize));
    return button;
}

void WelcomeOverlay::setPrimary(QPushButton *button, bool primary)
{
    if (button->property("primary").toBool() == primary)
        return;
    button->setProperty("primary", primary);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void WelcomeOverlay::setMode(Mode mode)
{
    mode_ = mode;
    if (mode == Mode::Empty) {
        titleLabel_->setText(QStringLiteral("Untitled"));
        subtitleLabel_->setText(QStringLiteral("This board is empty"));
        setPrimary(insertButton_, true);
        setPrimary(openButton_, false);
        insertButton_->show();
        openButton_->show();
        undoButton_->show();
    } else {
        titleLabel_->setText(QString::fromLatin1(constants::AppName));
        subtitleLabel_->setText(QStringLiteral("Reference image viewer"));
        setPrimary(openButton_, true);
        setPrimary(insertButton_, false);
        insertButton_->hide();
        undoButton_->hide();
        openButton_->show();
    }
    updateVisibility();
}

void WelcomeOverlay::setBoardName(const QString &name)
{
    // The reference elides a long name in the middle.
    QFont font = titleLabel_->font();
    font.setPointSize(20);
    font.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(font);
    titleLabel_->setText(metrics.elidedText(name, Qt::ElideMiddle, kCardWidth - 48));
    titleLabel_->setToolTip(name);
}

void WelcomeOverlay::setRecentFiles(const QStringList &files)
{
    recentFiles_ = files;
    filesView_->clear();
    for (const QString &path : files) {
        auto *item = new QListWidgetItem(QFileInfo(path).fileName(), filesView_);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        QFont font = item->font();
        font.setUnderline(true);
        item->setFont(font);
    }
    hasRecentFiles_ = !files.isEmpty();
    updateVisibility();
}

void WelcomeOverlay::setUndoState(bool canUndo, const QString &text)
{
    undoButton_->setEnabled(canUndo);
    undoButton_->setToolTip(text);
}

void WelcomeOverlay::setMimeFilter(MimeFilter filter)
{
    mimeFilter_ = std::move(filter);
    setAcceptDrops(true);
}

void WelcomeOverlay::showOverlay()
{
    if (isHidden()) {
        resize(canvas_ ? canvas_->size() : size());
        hud::fadeIn(content_);
    }
    show();
    raise();
}

void WelcomeOverlay::hideOverlay()
{
    hide();
}

void WelcomeOverlay::updateVisibility()
{
    // Hide or shrink optional content when the window is small, like
    // the reference.
    const int height = this->height();
    const bool small = height < kSmallHeight;
    filesCard_->setVisible(hasRecentFiles_ && height >= kFilesMinHeight);
    logoLabel_->setVisible(!logo_.isNull() && height >= kLogoMinHeight);
    subtitleLabel_->setVisible(height >= kSubtitleMinHeight);
    hintLabel_->setVisible(height >= kHintMinHeight);

    const int size = mode_ == Mode::Empty ? (small ? 32 : kLogoSizeEmpty)
                                          : (small ? 40 : kLogoSizeStart);
    if (!logo_.isNull())
        logoLabel_->setPixmap(logo_.scaled(size, size, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
    hintLabel_->setText(mode_ == Mode::Empty ? kEmptyHint
                                             : (small ? kStartHintCompact : kStartHint));
}

bool WelcomeOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == canvas_ && event->type() == QEvent::Resize)
        resize(canvas_->size());
    return QWidget::eventFilter(watched, event);
}

void WelcomeOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateVisibility();
}

void WelcomeOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // Cover the canvas with its own colour, so the overlay reads as the
    // empty board rather than as a panel floating over one.
    QPainter painter(this);
    painter.fillRect(rect(), theme::canvas);
}

void WelcomeOverlay::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && (!mimeFilter_ || mimeFilter_(*event->mimeData())))
        event->acceptProposedAction();
}

void WelcomeOverlay::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

void WelcomeOverlay::dropEvent(QDropEvent *event)
{
    if (!event->mimeData())
        return;
    // The position is the overlay's, which covers the canvas; the window
    // maps it to scene coordinates.
    emit mimeDropped(event->mimeData(), QPointF(event->position().toPoint()));
    event->acceptProposedAction();
}

} // namespace ui
