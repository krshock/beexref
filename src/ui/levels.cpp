#include "levels.h"

#include "constants.h"

#include <algorithm>

namespace ui {
namespace {

bool usableFraction(double fraction)
{
    return fraction > 0.0 && fraction <= 1.0;
}

QVector<double> sortedUsableFractions(const QString &csv)
{
    QVector<double> fractions;
    for (double fraction : parseLevelFractions(csv)) {
        if (usableFraction(fraction))
            fractions.append(fraction);
    }
    std::sort(fractions.begin(), fractions.end());
    return fractions;
}

} // namespace

LodSettings normalized(const LodSettings &settings)
{
    LodSettings result = settings;
    if (result.method.isEmpty())
        result.method = QStringLiteral("fixed");
    if (result.fractions.isEmpty())
        result.fractions = QStringLiteral("1,0.5,0.25,0.125,0.0625");
    if (result.budgetMB <= 0)
        result.budgetMB = 1024;
    if (result.quality.isEmpty())
        result.quality = QStringLiteral("smooth");
    if (result.primaryBudgetMB < 0)
        result.primaryBudgetMB = 0;
    if (result.ramCacheMB < 0)
        result.ramCacheMB = 0;
    return result;
}

QVector<double> parseLevelFractions(const QString &csv)
{
    QVector<double> fractions;
    for (const QString &part : csv.split(QLatin1Char(','))) {
        bool ok = false;
        const double value = part.trimmed().toDouble(&ok);
        if (ok)
            fractions.append(value);
    }
    return fractions;
}

QSize scaledLevelSize(const QSize &original, double fraction)
{
    return QSize(qMax(1, qRound(original.width() * fraction)),
                 qMax(1, qRound(original.height() * fraction)));
}

QVector<Level> buildLevels(const LodSettings &settings, const QSize &original,
                           double floorFraction)
{
    QVector<Level> levels;
    if (original.width() <= 0 || original.height() <= 0)
        return levels;

    const LodSettings config = normalized(settings);
    // Anything that is not the fixed family is the single-level method,
    // as in the reference.
    if (config.method != QLatin1String("fixed") && config.method != QLatin1String("ram_budget")) {
        levels.append({1.0, original});
    } else {
        for (double fraction : sortedUsableFractions(config.fractions)) {
            const QSize size = scaledLevelSize(original, fraction);
            if (fraction < 1.0
                && qMax(size.width(), size.height()) < constants::kFloorLevelSize) {
                continue;
            }
            levels.append({fraction, size});
        }
    }

    // A saved floor thumbnail is the coarsest level whatever its
    // fraction, so an item never decodes finer than what the board
    // already stores.
    if (floorFraction > 0.0 && floorFraction <= 1.0) {
        QVector<Level> kept;
        for (const Level &level : levels) {
            if (level.fraction > floorFraction)
                kept.append(level);
        }
        kept.prepend({floorFraction, scaledLevelSize(original, floorFraction)});
        levels = kept;
    }
    return levels;
}

qint64 levelBudgetBytes(const LodSettings &settings)
{
    const LodSettings config = normalized(settings);
    if (config.method != QLatin1String("ram_budget"))
        return 0;
    return qint64(config.budgetMB) * 1024 * 1024;
}

double desiredLevelFraction(const QVector<Level> &levels, double wantedWidth, double wantedHeight)
{
    for (const Level &level : levels) {
        if (level.covers(wantedWidth, wantedHeight))
            return level.fraction;
    }
    return 1.0;
}

} // namespace ui
