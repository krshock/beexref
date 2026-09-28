#include <QtTest>

#include <QDir>
#include <QFile>

#include "settings.h"
#include "test_env.h"
#include "ui/controls.h"

using namespace ui::controls;

class TestControls : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void modifierAndButtonNames();
    void storeDefaultsAndOverrides();
    void storeRestoresDefaults();
    void bindingMatching();
    void bindingMatchingWithOverrides();

private:
    QTemporaryDir dir_;
};

void TestControls::init()
{
    QVERIFY(dir_.isValid());
    settings::setSettingsDir(dir_.path());
}

void TestControls::cleanup()
{
    // Some tests clear the override; point the next one at the throwaway
    // directory again rather than at the user's real configuration.
    testenv::isolate();
}

void TestControls::modifierAndButtonNames()
{
    // The reference's names, used verbatim in KeyboardSettings.ini.
    QCOMPARE(modifierNames(),
             QStringList({QStringLiteral("No Modifier"), QStringLiteral("Shift"),
                          QStringLiteral("Ctrl"), QStringLiteral("Alt"),
                          QStringLiteral("Meta")}));
    QCOMPARE(buttonNames(), QStringList({QStringLiteral("Not Configured"),
                                         QStringLiteral("Left"),
                                         QStringLiteral("Middle")}));
}

void TestControls::storeDefaultsAndOverrides()
{
    Store store;
    QCOMPARE(store.path(), QDir(settings::configDir()).filePath(QStringLiteral("KeyboardSettings.ini")));

    // Without an override the defaults come back.
    const QStringList defaults{QStringLiteral("Ctrl+Z")};
    QCOMPARE(store.actionShortcuts(QStringLiteral("undo"), defaults), defaults);

    store.setActionShortcuts(QStringLiteral("undo"), {QStringLiteral("Ctrl+U")}, defaults);
    QCOMPARE(store.actionShortcuts(QStringLiteral("undo"), defaults),
             QStringList({QStringLiteral("Ctrl+U")}));
    {
        settings::File file(store.path());
        file.load();
        QCOMPARE(file.value(QStringLiteral("Actions"), QStringLiteral("undo")),
                 QStringLiteral("Ctrl+U"));
    }

    // Setting a value back to its default removes the entry.
    store.setActionShortcuts(QStringLiteral("undo"), defaults, defaults);
    {
        settings::File file(store.path());
        file.load();
        QVERIFY(!file.contains(QStringLiteral("Actions"), QStringLiteral("undo")));
    }

    // Clearing writes an empty value, which resolves to no shortcut.
    store.setActionShortcuts(QStringLiteral("undo"), {}, defaults);
    QVERIFY(store.actionShortcuts(QStringLiteral("undo"), defaults).isEmpty());

    // Mouse bindings resolve from the defaults and store overrides.
    const MouseBinding pan = store.mouse(QStringLiteral("pan1"));
    QCOMPARE(pan.button, QStringLiteral("Middle"));
    QCOMPARE(pan.modifiers, QStringList({QStringLiteral("No Modifier")}));
    QVERIFY(!pan.inverted);
    QVERIFY(!pan.invertible);

    MouseBinding moved = pan;
    moved.button = QStringLiteral("Left");
    moved.inverted = true;
    store.setMouse(moved);
    const MouseBinding resolved = store.mouse(QStringLiteral("pan1"));
    QCOMPARE(resolved.button, QStringLiteral("Left"));
    QVERIFY(resolved.inverted);
    {
        settings::File file(store.path());
        file.load();
        QCOMPARE(file.value(QStringLiteral("Mouse"), QStringLiteral("pan1_button")),
                 QStringLiteral("Left"));
        QCOMPARE(file.value(QStringLiteral("Mouse"), QStringLiteral("pan1_inverted")),
                 QStringLiteral("true"));
    }

    // Wheel bindings, same rules.
    WheelBinding zoom = store.wheel(QStringLiteral("zoom1"));
    QCOMPARE(zoom.modifiers, QStringList({QStringLiteral("No Modifier")}));
    zoom.modifiers = {QStringLiteral("Ctrl")};
    store.setWheel(zoom);
    QCOMPARE(store.wheel(QStringLiteral("zoom1")).modifiers,
             QStringList({QStringLiteral("Ctrl")}));
}

void TestControls::storeRestoresDefaults()
{
    Store store;
    store.setActionShortcuts(QStringLiteral("undo"), {QStringLiteral("Ctrl+U")},
                             {QStringLiteral("Ctrl+Z")});
    MouseBinding pan = store.mouse(QStringLiteral("pan1"));
    pan.button = QStringLiteral("Left");
    store.setMouse(pan);

    store.restoreDefaults();

    QCOMPARE(store.actionShortcuts(QStringLiteral("undo"), {QStringLiteral("Ctrl+Z")}),
             QStringList({QStringLiteral("Ctrl+Z")}));
    QCOMPARE(store.mouse(QStringLiteral("pan1")).button, QStringLiteral("Middle"));
    {
        settings::File file(store.path());
        file.load();
        QVERIFY(!file.contains(QStringLiteral("Actions"), QStringLiteral("undo")));
        QVERIFY(!file.contains(QStringLiteral("Mouse"), QStringLiteral("pan1_button")));
    }
}

void TestControls::bindingMatching()
{
    const Bindings bindings = Bindings::load();

    // Middle alone pans, Middle+Ctrl drag-zooms, Left+Alt pans too.
    Bindings::Match match = bindings.mouseAction(Qt::MiddleButton, Qt::NoModifier);
    QVERIFY(match.valid);
    QCOMPARE(match.group, QStringLiteral("pan"));

    match = bindings.mouseAction(Qt::MiddleButton, Qt::ControlModifier);
    QVERIFY(match.valid);
    QCOMPARE(match.group, QStringLiteral("zoom"));

    match = bindings.mouseAction(Qt::LeftButton, Qt::AltModifier);
    QVERIFY(match.valid);
    QCOMPARE(match.group, QStringLiteral("pan"));

    // Shift+Middle peeks the scene.
    match = bindings.mouseAction(Qt::MiddleButton, Qt::ShiftModifier);
    QVERIFY(match.valid);
    QCOMPARE(match.group, QStringLiteral("peek"));

    // Left alone is the item interaction, not a binding.
    QVERIFY(!bindings.mouseAction(Qt::LeftButton, Qt::NoModifier).valid);

    // Wheel: bare zooms, Shift pans horizontally, Shift+Ctrl vertically.
    match = bindings.wheelAction(Qt::NoModifier);
    QVERIFY(match.valid);
    QCOMPARE(match.group, QStringLiteral("zoom"));

    match = bindings.wheelAction(Qt::ShiftModifier);
    QVERIFY(match.valid);
    QCOMPARE(match.group, QStringLiteral("pan_horizontal"));

    match = bindings.wheelAction(Qt::ShiftModifier | Qt::ControlModifier);
    QVERIFY(match.valid);
    QCOMPARE(match.group, QStringLiteral("pan_vertical"));

    QVERIFY(!bindings.wheelAction(Qt::ControlModifier).valid);
}

void TestControls::bindingMatchingWithOverrides()
{
    Store store;
    // Move the pan binding to Middle+Shift.
    MouseBinding pan = store.mouse(QStringLiteral("pan1"));
    pan.modifiers = {QStringLiteral("Shift")};
    store.setMouse(pan);
    // Invert the wheel zoom.
    WheelBinding zoom = store.wheel(QStringLiteral("zoom1"));
    zoom.inverted = true;
    store.setWheel(zoom);

    const Bindings bindings = Bindings::load();
    QVERIFY(!bindings.mouseAction(Qt::MiddleButton, Qt::NoModifier).valid);
    const Bindings::Match panMatch = bindings.mouseAction(Qt::MiddleButton, Qt::ShiftModifier);
    QVERIFY(panMatch.valid);
    QCOMPARE(panMatch.group, QStringLiteral("pan"));

    const Bindings::Match wheelMatch = bindings.wheelAction(Qt::NoModifier);
    QVERIFY(wheelMatch.valid);
    QCOMPARE(wheelMatch.group, QStringLiteral("zoom"));
    QVERIFY(wheelMatch.inverted);
}

QTEST_GUILESS_MAIN(TestControls)

#include "test_controls.moc"
