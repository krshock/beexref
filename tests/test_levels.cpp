#include <QtTest>

#include "ui/levels.h"

class TestLevels : public QObject
{
    Q_OBJECT

private slots:
    void parsesFractions();
    void scalesSizes();
    void fixedLadderSkipsLevelsBelowTheFloor();
    void floorThumbnailBecomesTheCoarsestLevel();
    void singleMethodAlwaysOffersTheOriginal();
    void budgetOnlyForRamBudget();
    void methodsAreSettingsValuesWithAFallback();
    void desiredFractionPicksTheCheapestCoveringLevel();
};

void TestLevels::parsesFractions()
{
    QCOMPARE(ui::parseLevelFractions(QStringLiteral("1,0.5,0.25")).size(), 3);
    QCOMPARE(ui::parseLevelFractions(QStringLiteral("1, foo, 0.5")).size(), 2);
    QCOMPARE(ui::parseLevelFractions(QString()).size(), 0);
    QCOMPARE(ui::parseLevelFractions(QStringLiteral("1, 0.5 ,0.25")).at(1), 0.5);
}

void TestLevels::scalesSizes()
{
    QCOMPARE(ui::scaledLevelSize(QSize(300, 200), 0.5), QSize(150, 100));
    QCOMPARE(ui::scaledLevelSize(QSize(300, 200), 1.0), QSize(300, 200));
    // Never below 1x1.
    QCOMPARE(ui::scaledLevelSize(QSize(10, 10), 0.01), QSize(1, 1));
}

void TestLevels::fixedLadderSkipsLevelsBelowTheFloor()
{
    ui::LodSettings settings;
    const QVector<ui::Level> levels = ui::buildLevels(settings, QSize(300, 200), 0);
    // 1.0, 0.5, 0.25 survive; 0.125 (37 px) and 0.0625 are below the
    // 64 px floor. The ladder is ascending, coarsest first.
    QCOMPARE(levels.size(), 3);
    QCOMPARE(levels.at(0).fraction, 0.25);
    QCOMPARE(levels.at(0).size, QSize(75, 50));
    QCOMPARE(levels.at(1).fraction, 0.5);
    QCOMPARE(levels.at(1).size, QSize(150, 100));
    QCOMPARE(levels.at(2).fraction, 1.0);
    QCOMPARE(levels.at(2).size, QSize(300, 200));

    // A big image keeps every fraction above the floor.
    const QVector<ui::Level> big = ui::buildLevels(settings, QSize(2000, 1000), 0);
    QCOMPARE(big.size(), 5);
    QCOMPARE(big.first().fraction, 0.0625);
    QCOMPARE(big.last().fraction, 1.0);
}

void TestLevels::floorThumbnailBecomesTheCoarsestLevel()
{
    ui::LodSettings settings;
    const QVector<ui::Level> levels = ui::buildLevels(settings, QSize(300, 200), 0.25);
    QCOMPARE(levels.size(), 3);
    QCOMPARE(levels.at(0).fraction, 0.25);
    QCOMPARE(levels.at(1).fraction, 0.5);
    QCOMPARE(levels.at(2).fraction, 1.0);

    // A floor finer than some ladder entries drops those entries.
    const QVector<ui::Level> finer = ui::buildLevels(settings, QSize(300, 200), 0.5);
    QCOMPARE(finer.size(), 2);
    QCOMPARE(finer.at(0).fraction, 0.5);
    QCOMPARE(finer.at(1).fraction, 1.0);
}

void TestLevels::singleMethodAlwaysOffersTheOriginal()
{
    ui::LodSettings settings;
    settings.method = QStringLiteral("single");
    const QVector<ui::Level> levels = ui::buildLevels(settings, QSize(300, 200), 0);
    QCOMPARE(levels.size(), 1);
    QCOMPARE(levels.at(0).fraction, 1.0);
    QCOMPARE(levels.at(0).size, QSize(300, 200));

    // The saved thumbnail is still the loading floor.
    const QVector<ui::Level> withFloor = ui::buildLevels(settings, QSize(300, 200), 0.25);
    QCOMPARE(withFloor.size(), 2);
    QCOMPARE(withFloor.at(0).fraction, 0.25);
    QCOMPARE(withFloor.at(1).fraction, 1.0);
}

void TestLevels::budgetOnlyForRamBudget()
{
    ui::LodSettings settings;
    QCOMPARE(ui::levelBudgetBytes(settings), qint64(0));

    settings.method = QStringLiteral("ram_budget");
    settings.budgetMB = 256;
    QCOMPARE(ui::levelBudgetBytes(settings), qint64(256) * 1024 * 1024);
}

void TestLevels::methodsAreSettingsValuesWithAFallback()
{
    // The method ids are the Items/lod_method settings values, so the
    // registry and the settings dialog's radio options agree.
    QVERIFY(ui::levelMethod(QStringLiteral("single")));
    QVERIFY(ui::levelMethod(QStringLiteral("fixed")));
    const ui::LevelMethod *ramBudget = ui::levelMethod(QStringLiteral("ram_budget"));
    QVERIFY(ramBudget);
    QVERIFY(ramBudget->budgetFromSettings);
    QVERIFY(!ui::levelMethod(QStringLiteral("single"))->usesFractions);
    QCOMPARE(ui::levelMethod(QStringLiteral("nonsense")), nullptr);

    // An unknown id keeps the reference's single-level fallback.
    ui::LodSettings settings;
    settings.method = QStringLiteral("nonsense");
    settings.fractions = QStringLiteral("1,0.5");
    const QVector<ui::Level> levels = ui::buildLevels(settings, QSize(400, 200), 0);
    QCOMPARE(levels.size(), 1);
    QCOMPARE(levels.first().fraction, 1.0);
    QCOMPARE(ui::levelBudgetBytes(settings), qint64(0));
}

void TestLevels::desiredFractionPicksTheCheapestCoveringLevel()
{
    const QVector<ui::Level> levels = ui::buildLevels(ui::LodSettings(), QSize(300, 200), 0);
    // Wanted 100x70 is covered by the 150x100 level.
    QCOMPARE(ui::desiredLevelFraction(levels, 100, 70), 0.5);
    // Wanted 160x110 needs the full level.
    QCOMPARE(ui::desiredLevelFraction(levels, 160, 110), 1.0);
    QCOMPARE(ui::desiredLevelFraction(levels, 300, 200), 1.0);
}

QTEST_GUILESS_MAIN(TestLevels)

#include "test_levels.moc"
