#pragma once

#include "color_tools.h"
#include "doc/item.h"

#include <QDialog>
#include <QHash>
#include <QImage>
#include <QVector>
#include <QThread>
#include <QWidget>

class QSlider;

namespace ui {

// The reference's gamut plot: a black disc with one dot per
// hue/saturation bucket whose count reaches the threshold. The wheel is
// rendered once per change and only scaled when painting.
class GamutPlot : public QWidget
{
    Q_OBJECT

public:
    explicit GamutPlot(QWidget *parent = nullptr);

    void setGamut(const QHash<colors::GamutKey, int> &gamut);
    void setThreshold(int threshold);
    int threshold() const { return threshold_; }
    // Dots currently drawn: buckets whose count reaches the threshold.
    int visibleDots() const { return visibleDots_; }

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize minimumSizeHint() const override { return QSize(200, 200); }

private:
    // The reference's wheel radius in pixels.
    static constexpr int kRadius = 250;

    // One bucket's dot, drawn once the count reaches the threshold.
    struct Dot
    {
        QPointF position;
        QColor color;
        int count = 0;
    };

    void rebuildWheel();

    QHash<colors::GamutKey, int> gamut_;
    int threshold_ = 20;
    bool ready_ = false;
    QImage disc_;  // the black wheel, drawn once per gamut
    QImage wheel_; // the disc plus the qualifying dots
    QVector<Dot> dots_;
    int visibleDots_ = 0;
};

// Decodes the item's source (or the given fallback level when there is
// none) and counts its gamut off the GUI thread, like the reference's
// painter thread.
class GamutThread : public QThread
{
    Q_OBJECT

public:
    GamutThread(doc::SourcePtr source, QImage fallback, QObject *parent = nullptr);

    const QHash<colors::GamutKey, int> &gamut() const { return gamut_; }

signals:
    void gamutReady();

protected:
    void run() override;

private:
    doc::SourcePtr source_;
    QImage fallback_;
    QHash<colors::GamutKey, int> gamut_;
};

// The Images menu's "Show Color Gamut": the wheel plus a threshold
// slider, as the reference's GamutDialog.
class GamutDialog : public QDialog
{
    Q_OBJECT

public:
    GamutDialog(QWidget *parent, const doc::ItemPtr &item, const QImage &fallbackLevel);
    // Waits for the counting thread, so closing the dialog while it
    // still runs never destroys a live QThread.
    ~GamutDialog() override;

private:
    GamutPlot *plot_ = nullptr;
    QSlider *threshold_ = nullptr;
    GamutThread *thread_ = nullptr;
};

} // namespace ui
