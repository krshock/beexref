#include "controls.h"

#include <QDir>

namespace ui::controls {
namespace {

const QString kNoModifier = QStringLiteral("No Modifier");
const QString kNotConfigured = QStringLiteral("Not Configured");

QString sectionOf(const QString &key)
{
    return key.section(QLatin1Char('/'), 0, 0);
}

QString nameOf(const QString &key)
{
    return key.section(QLatin1Char('/'), 1);
}

// The app stores lists joined with ", " and splits them back.
QStringList splitList(const QString &value)
{
    QStringList values;
    for (const QString &part : value.split(QStringLiteral(", "))) {
        if (!part.isEmpty())
            values.append(part);
    }
    return values;
}

QString joinList(const QStringList &values)
{
    return values.join(QStringLiteral(", "));
}

QString normalizeModifiers(const QStringList &modifiers)
{
    QStringList names;
    for (const QString &name : modifiers) {
        if (modifierNames().contains(name) && !names.contains(name))
            names.append(name);
    }
    // "No Modifier" stands alone.
    if (names.contains(kNoModifier) && names.size() > 1)
        names.removeAll(kNoModifier);
    return joinList(names);
}

// Whether a Qt modifier set matches a stored name list exactly.
bool modifiersMatch(const QStringList &names, Qt::KeyboardModifiers modifiers)
{
    const bool noModifier = names.contains(kNoModifier) || names.isEmpty();
    Qt::KeyboardModifiers expected;
    if (!noModifier) {
        if (names.contains(QStringLiteral("Shift")))
            expected |= Qt::ShiftModifier;
        if (names.contains(QStringLiteral("Ctrl")))
            expected |= Qt::ControlModifier;
        if (names.contains(QStringLiteral("Alt")))
            expected |= Qt::AltModifier;
        if (names.contains(QStringLiteral("Meta")))
            expected |= Qt::MetaModifier;
    }
    // Qt reports keypad/numpad as extra bits; ignore those.
    modifiers &= Qt::KeyboardModifierMask;
    return expected == modifiers;
}

bool buttonMatches(const QString &button, Qt::MouseButton actual)
{
    if (button == QStringLiteral("Left"))
        return actual == Qt::LeftButton;
    if (button == QStringLiteral("Middle"))
        return actual == Qt::MiddleButton;
    return false; // "Not Configured" never matches
}

} // namespace

QStringList modifierNames()
{
    return {kNoModifier, QStringLiteral("Shift"), QStringLiteral("Ctrl"), QStringLiteral("Alt"),
            QStringLiteral("Meta")};
}

QStringList buttonNames()
{
    // The map has no right button; kept for compatibility.
    return {kNotConfigured, QStringLiteral("Left"), QStringLiteral("Middle")};
}

const QVector<MouseBinding> &defaultMouseBindings()
{
    static const QVector<MouseBinding> bindings = {
        {QStringLiteral("zoom1"), QStringLiteral("zoom"), QStringLiteral("Zoom"),
         QStringLiteral("Middle"), {QStringLiteral("Ctrl")}, true, false},
        {QStringLiteral("zoom2"), QStringLiteral("zoom"), QStringLiteral("Zoom (alternative)"),
         kNotConfigured, {}, true, false},
        {QStringLiteral("pan1"), QStringLiteral("pan"), QStringLiteral("Pan"),
         QStringLiteral("Middle"), {kNoModifier}, false, false},
        {QStringLiteral("pan2"), QStringLiteral("pan"), QStringLiteral("Pan (alternative)"),
         QStringLiteral("Left"), {QStringLiteral("Alt")}, false, false},
        {QStringLiteral("movewindow1"), QStringLiteral("movewindow"),
         QStringLiteral("Move Window"), QStringLiteral("Left"),
         {QStringLiteral("Ctrl"), QStringLiteral("Alt")}, false, false},
        {QStringLiteral("movewindow2"), QStringLiteral("movewindow"),
         QStringLiteral("Move Window"), kNotConfigured, {}, false, false},
    };
    return bindings;
}

const QVector<WheelBinding> &defaultWheelBindings()
{
    static const QVector<WheelBinding> bindings = {
        {QStringLiteral("zoom1"), QStringLiteral("zoom"), QStringLiteral("Zoom"),
         {kNoModifier}, true, false},
        {QStringLiteral("zoom2"), QStringLiteral("zoom"), QStringLiteral("Zoom (alternative)"),
         {}, true, false},
        {QStringLiteral("pan_horizontal1"), QStringLiteral("pan_horizontal"),
         QStringLiteral("Pan horizontally"), {QStringLiteral("Shift")}, true, false},
        {QStringLiteral("pan_horizontal2"), QStringLiteral("pan_horizontal"),
         QStringLiteral("Pan horizontally (alternative)"), {}, true, false},
        {QStringLiteral("pan_vertical1"), QStringLiteral("pan_vertical"),
         QStringLiteral("Pan vertically"),
         {QStringLiteral("Shift"), QStringLiteral("Ctrl")}, true, false},
        {QStringLiteral("pan_vertical2"), QStringLiteral("pan_vertical"),
         QStringLiteral("Pan vertically (alternative)"), {}, true, false},
    };
    return bindings;
}

// --- Store ------------------------------------------------------------

Store::Store()
    : file_(QDir(settings::configDir()).filePath(QStringLiteral("KeyboardSettings.ini")))
{
    file_.load();
}

MouseBinding Store::mouse(const QString &id) const
{
    for (const MouseBinding &binding : defaultMouseBindings()) {
        if (binding.id != id)
            continue;
        MouseBinding resolved = binding;
        const QString buttonKey = QStringLiteral("Mouse/%1_button").arg(id);
        const QString modifiersKey = QStringLiteral("Mouse/%1_modifiers").arg(id);
        const QString invertedKey = QStringLiteral("Mouse/%1_inverted").arg(id);
        if (file_.contains(sectionOf(buttonKey), nameOf(buttonKey)))
            resolved.button = file_.value(sectionOf(buttonKey), nameOf(buttonKey));
        if (file_.contains(sectionOf(modifiersKey), nameOf(modifiersKey)))
            resolved.modifiers = splitList(file_.value(sectionOf(modifiersKey), nameOf(modifiersKey)));
        if (file_.contains(sectionOf(invertedKey), nameOf(invertedKey)))
            resolved.inverted =
                file_.value(sectionOf(invertedKey), nameOf(invertedKey)) == QLatin1String("true");
        return resolved;
    }
    return {};
}

WheelBinding Store::wheel(const QString &id) const
{
    for (const WheelBinding &binding : defaultWheelBindings()) {
        if (binding.id != id)
            continue;
        WheelBinding resolved = binding;
        const QString modifiersKey = QStringLiteral("MouseWheel/%1_modifiers").arg(id);
        const QString invertedKey = QStringLiteral("MouseWheel/%1_inverted").arg(id);
        if (file_.contains(sectionOf(modifiersKey), nameOf(modifiersKey)))
            resolved.modifiers = splitList(file_.value(sectionOf(modifiersKey), nameOf(modifiersKey)));
        if (file_.contains(sectionOf(invertedKey), nameOf(invertedKey)))
            resolved.inverted =
                file_.value(sectionOf(invertedKey), nameOf(invertedKey)) == QLatin1String("true");
        return resolved;
    }
    return {};
}

QVector<MouseBinding> Store::mouseBindings() const
{
    QVector<MouseBinding> bindings;
    for (const MouseBinding &binding : defaultMouseBindings())
        bindings.append(mouse(binding.id));
    return bindings;
}

QVector<WheelBinding> Store::wheelBindings() const
{
    QVector<WheelBinding> bindings;
    for (const WheelBinding &binding : defaultWheelBindings())
        bindings.append(wheel(binding.id));
    return bindings;
}

QStringList Store::actionShortcuts(const QString &id, const QStringList &defaults) const
{
    const QString key = QStringLiteral("Actions/%1").arg(id);
    if (!file_.contains(sectionOf(key), nameOf(key)))
        return defaults;
    return splitList(file_.value(sectionOf(key), nameOf(key)));
}

void Store::setActionShortcuts(const QString &id, const QStringList &values,
                               const QStringList &defaults)
{
    const QString key = QStringLiteral("Actions/%1").arg(id);
    if (values == defaults) {
        file_.remove(sectionOf(key), nameOf(key));
    } else {
        file_.setValue(sectionOf(key), nameOf(key), joinList(values));
    }
    file_.sync();
}

void Store::setMouse(const MouseBinding &binding)
{
    const QString buttonKey = QStringLiteral("Mouse/%1_button").arg(binding.id);
    const QString modifiersKey = QStringLiteral("Mouse/%1_modifiers").arg(binding.id);
    const QString invertedKey = QStringLiteral("Mouse/%1_inverted").arg(binding.id);

    MouseBinding defaults;
    for (const MouseBinding &candidate : defaultMouseBindings()) {
        if (candidate.id == binding.id)
            defaults = candidate;
    }
    if (binding.button == defaults.button)
        file_.remove(sectionOf(buttonKey), nameOf(buttonKey));
    else
        file_.setValue(sectionOf(buttonKey), nameOf(buttonKey), binding.button);

    const QString modifiers = normalizeModifiers(binding.modifiers);
    if (modifiers == normalizeModifiers(defaults.modifiers))
        file_.remove(sectionOf(modifiersKey), nameOf(modifiersKey));
    else
        file_.setValue(sectionOf(modifiersKey), nameOf(modifiersKey), modifiers);

    if (binding.inverted == defaults.inverted)
        file_.remove(sectionOf(invertedKey), nameOf(invertedKey));
    else
        file_.setValue(sectionOf(invertedKey), nameOf(invertedKey),
                       binding.inverted ? QStringLiteral("true") : QStringLiteral("false"));
    file_.sync();
}

void Store::setWheel(const WheelBinding &binding)
{
    const QString modifiersKey = QStringLiteral("MouseWheel/%1_modifiers").arg(binding.id);
    const QString invertedKey = QStringLiteral("MouseWheel/%1_inverted").arg(binding.id);

    WheelBinding defaults;
    for (const WheelBinding &candidate : defaultWheelBindings()) {
        if (candidate.id == binding.id)
            defaults = candidate;
    }
    const QString modifiers = normalizeModifiers(binding.modifiers);
    if (modifiers == normalizeModifiers(defaults.modifiers))
        file_.remove(sectionOf(modifiersKey), nameOf(modifiersKey));
    else
        file_.setValue(sectionOf(modifiersKey), nameOf(modifiersKey), modifiers);

    if (binding.inverted == defaults.inverted)
        file_.remove(sectionOf(invertedKey), nameOf(invertedKey));
    else
        file_.setValue(sectionOf(invertedKey), nameOf(invertedKey),
                       binding.inverted ? QStringLiteral("true") : QStringLiteral("false"));
    file_.sync();
}

void Store::restoreDefaults()
{
    file_.removeSection(QStringLiteral("Actions"));
    file_.removeSection(QStringLiteral("Mouse"));
    file_.removeSection(QStringLiteral("MouseWheel"));
    file_.sync();
}

// --- Bindings ---------------------------------------------------------

Bindings Bindings::load()
{
    const Store store;
    Bindings bindings;
    bindings.mouse_ = store.mouseBindings();
    bindings.wheel_ = store.wheelBindings();
    return bindings;
}

Bindings::Match Bindings::mouseAction(Qt::MouseButton button,
                                      Qt::KeyboardModifiers modifiers) const
{
    Match match;
    for (const MouseBinding &binding : mouse_) {
        if (!buttonMatches(binding.button, button))
            continue;
        if (!modifiersMatch(binding.modifiers, modifiers))
            continue;
        match.valid = true;
        match.group = binding.group;
        match.inverted = binding.inverted;
        return match;
    }
    return match;
}

Bindings::Match Bindings::wheelAction(Qt::KeyboardModifiers modifiers) const
{
    Match match;
    for (const WheelBinding &binding : wheel_) {
        if (!modifiersMatch(binding.modifiers, modifiers))
            continue;
        match.valid = true;
        match.group = binding.group;
        match.inverted = binding.inverted;
        return match;
    }
    return match;
}

} // namespace ui::controls
