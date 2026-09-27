#include "color_gamut.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {

// --- GamutPlot --------------------------------------------------------

GamutPlot::GamutPlot(QWidget *parent)
    : QWidget(parent)
{
}

void GamutPlot::setGamut(const QHash<colors::GamutKey, int> &gamut)
{
    gamut_ = gamut;
    ready_ = true;

    // The disc never changes once the gamut is known; the dots are
    // collected here so a threshold change only redraws them.
    disc_ = QImage(2 * kRadius, 2 * kRadius, QImage::Format_ARGB32_Premultiplied);
    disc_.fill(Qt::transparent);
    {
        QPainter painter(&disc_);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawEllipse(QPoint(kRadius, kRadius), kRadius, kRadius);
    }

    dots_.clear();
    dots_.reserve(gamut.size());
    for (auto it = gamut.cbegin(); it != gamut.cend(); ++it) {
        Dot dot;
        dot.position = colors::gamutDotPosition(it.key(), kRadius);
        // Achromatic buckets (hue -1, saturation 0) become white.
        dot.color.setHsv(qMax(0, it.key().hue), it.key().saturation, 255);
        dot.count = it.value();
        dots_.append(dot);
    }
    // Busiest first, so a high threshold stops early.
    std::sort(dots_.begin(), dots_.end(),
              [](const Dot &a, const Dot &b) { return a.count > b.count; });

    rebuildWheel();
    update();
}

void GamutPlot::setThreshold(int threshold)
{
    if (threshold_ == threshold)
        return;
    threshold_ = threshold;
    rebuildWheel();
    update();
}

void GamutPlot::rebuildWheel()
{
    wheel_ = disc_.copy();
    visibleDots_ = 0;
    if (!ready_)
        return;

    QPainter painter(&wheel_);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    // The dots are sorted by count, so the first one below the
    // threshold ends the pass.
    for (const Dot &dot : dots_) {
        if (dot.count < threshold_)
            break;
        painter.setBrush(dot.color);
        painter.drawEllipse(dot.position, 3, 3);
        ++visibleDots_;
    }
}

void GamutPlot::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    if (!ready_) {
        painter.drawText(10, 20, QStringLiteral("Counting pixels..."));
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const int size = qMin(width(), height());
    const double x = qMax((width() - size) / 2.0, 0.0);
    const double y = qMax((height() - size) / 2.0, 0.0);
    painter.drawImage(QRectF(x, y, size, size), wheel_);
}

// --- GamutThread ------------------------------------------------------

GamutThread::GamutThread(doc::SourcePtr source, QImage fallback, QObject *parent)
    : QThread(parent)
    , source_(std::move(source))
    , fallback_(std::move(fallback))
{
}

void GamutThread::run()
{
    QImage image;
    if (source_ && source_->isValid()) {
        const QByteArray bytes = source_->bytes();
        if (!bytes.isEmpty())
            image = QImage::fromData(bytes);
    }
    if (image.isNull())
        image = fallback_;
    gamut_ = colors::gamutHistogram(image);
    emit gamutReady();
}

// --- GamutDialog ------------------------------------------------------

GamutDialog::GamutDialog(QWidget *parent, const doc::ItemPtr &item, const QImage &fallbackLevel)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Color Gamut"));

    auto *layout = new QHBoxLayout(this);
    plot_ = new GamutPlot(this);
    layout->addWidget(plot_, 1);

    auto *controls = new QVBoxLayout();
    controls->addWidget(new QLabel(QStringLiteral("Threshold:"), this));
    threshold_ = new QSlider(Qt::Horizontal, this);
    threshold_->setRange(0, 500);
    threshold_->setValue(20);
    // Continuous, like the Go port: the wheel follows the slider while
    // it is dragged (the Python port waits for the release).
    threshold_->setTracking(true);
    connect(threshold_, &QSlider::valueChanged, this,
            [this](int value) { plot_->setThreshold(value); });
    controls->addWidget(threshold_, 0, Qt::AlignHCenter);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    controls->addWidget(buttons);
    layout->addLayout(controls, 0);

    // The histogram is counted off the GUI thread; the plot shows
    // "Counting pixels..." until it arrives.
    doc::SourcePtr source;
    if (item && item->isPixmap())
        source = item->source;
    thread_ = new GamutThread(std::move(source), fallbackLevel, this);
    connect(thread_, &GamutThread::gamutReady, this, [this]() {
        if (thread_)
            plot_->setGamut(thread_->gamut());
    });
    connect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
    thread_->start();
}

GamutDialog::~GamutDialog()
{
    if (thread_ && thread_->isRunning())
        thread_->wait();
}

} // namespace ui
