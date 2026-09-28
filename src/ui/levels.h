#pragma once

#include <QSize>
#include <QString>
#include <QVector>

namespace ui {

// LOD configuration, mirroring the LOD settings.
struct LodSettings
{
    QString method = QStringLiteral("fixed"); // single | fixed | ram_budget
    QString fractions = QStringLiteral("1,0.5,0.25,0.125,0.0625");
    int budgetMB = 1024;
    QString quality = QStringLiteral("smooth"); // fast | smooth
    // Always-on cap on the decoded LOD bytes the manager keeps, in MB.
    // 0 means unlimited. Independent of the LOD method.
    int primaryBudgetMB = 0;
    // Cap on the loader's in-RAM LRU of decoded levels, in MB; 0
    // disables it. A sub-budget inside the primary one.
    int ramCacheMB = 512;
    // Decode worker threads. Always at least one.
    int decodeThreads = 3;
    // Percent of the off-screen decoded-level cache released every 10 s
    // of inactivity; 0 disables settling.
    int cacheSettlePercent = 30;
};

// Fills unset fields with the defaults.
LodSettings normalized(const LodSettings &settings);

// One LOD method, as data: the Items/lod_method settings value, whether
// it builds the fraction ladder, and whether its budget comes from
// Items/lod_ram_budget_mb. An id that is not registered behaves like
// "single".
struct LevelMethod
{
    QString id;
    bool usesFractions = false;
    bool budgetFromSettings = false;
};

// The registered methods, in the settings dialog's order.
const QVector<LevelMethod> &levelMethods();
// The registered method for an id, or nullptr.
const LevelMethod *levelMethod(const QString &id);

// One decode resolution of an image.
struct Level
{
    double fraction = 1.0;
    QSize size;

    qint64 decodedBytes() const { return qint64(size.width()) * size.height() * 4; }
    bool covers(double width, double height) const
    {
        return size.width() >= width && size.height() >= height;
    }
};

// The wanted level covers the item's on-screen size with a little
// headroom, and the visibility test extends the viewport on each side.
// The consumption band of a request: the level loader services bands in
// this order (highest first) and FIFO inside a band. Bands are the
// loader's contract; which hint put a request there is the manager's
// business.
enum class RequestBand {
    Selected, // an explicitly selected image
    Visible,  // on screen (or in a gesture)
    Deferred, // off screen: runs when nothing above it waits
};

inline constexpr double kLevelOverdraw = 1.2;
inline constexpr double kLevelVisibilityMargin = 0.1;

// Parses the settings list; entries that do not parse are skipped and
// values outside (0, 1] are filtered when the ladder is built.
QVector<double> parseLevelFractions(const QString &csv);

QSize scaledLevelSize(const QSize &original, double fraction);

// The item's level ladder: ascending fractions, levels below the floor
// size skipped for fractions under 1, and the saved floor thumbnail
// first when the item has one (pass floorFraction <= 0 for none).
QVector<Level> buildLevels(const LodSettings &settings, const QSize &original,
                           double floorFraction);

// Decoded-bytes budget; 0 when the method has none.
qint64 levelBudgetBytes(const LodSettings &settings);

// The cheapest level that covers the wanted size; 1.0 when none does.
double desiredLevelFraction(const QVector<Level> &levels, double wantedWidth, double wantedHeight);

} // namespace ui
