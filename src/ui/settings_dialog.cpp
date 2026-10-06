#include "settings_dialog.h"

#include "constants.h"
#include "logging.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ui {
namespace {

using Kind = FieldUi::Kind;

FieldUi makeField(const QString &key, const QString &title, const QString &help, Kind kind)
{
    FieldUi field;
    field.key = key;
    field.title = title;
    field.help = help;
    field.kind = kind;
    return field;
}

FieldUi radioField(const QString &key, const QString &title, const QString &help,
                   const QVector<std::tuple<QString, QString, QString>> &options)
{
    FieldUi field = makeField(key, title, help, Kind::Radio);
    for (const auto &option : options)
        field.options.append({std::get<0>(option), std::get<1>(option), std::get<2>(option)});
    return field;
}

FieldUi integerField(const QString &key, const QString &title, const QString &help, int minimum,
                     int maximum)
{
    FieldUi field = makeField(key, title, help, Kind::Integer);
    field.minimum = minimum;
    field.maximum = maximum;
    return field;
}

FieldUi checkboxField(const QString &key, const QString &title, const QString &help,
                      const QString &label)
{
    FieldUi field = makeField(key, title, help, Kind::Checkbox);
    field.label = label;
    return field;
}

// The field metadata of widgets/settings.py.
const QVector<FieldUi> &fieldTable()
{
    static const QVector<FieldUi> fields = {
        checkboxField(QStringLiteral("Save/confirm_close_unsaved"),
                      QStringLiteral("Confirm when closing an unsaved file:"),
                      QStringLiteral("When about to close an unsaved file, should BeeXRef ask "
                                     "for confirmation?"),
                      QStringLiteral("Confirm when closing")),
        checkboxField(QStringLiteral("Save/incremental"),
                      QStringLiteral("Incremental saves:"),
                      QStringLiteral("Write only what changed into the board file instead of "
                                     "rewriting all of it. Faster on large boards; turn this off "
                                     "to always write a complete new file."),
                      QStringLiteral("Write only what changed")),
        // The window colours.
        radioField(QStringLiteral("View/theme"), QStringLiteral("Theme:"),
                   QStringLiteral("The colours of the window, menus and dialogs. 'Follow the "
                                  "system' uses the desktop's light or dark setting; the canvas "
                                  "and the HUD keep their dark look in every mode."),
                   {{QStringLiteral("system"), QStringLiteral("Follow the system"),
                     QStringLiteral("Use the desktop's light or dark setting")},
                    {QStringLiteral("dark"), QStringLiteral("Dark"),
                     QStringLiteral("A dark window around the dark canvas")},
                    {QStringLiteral("light"), QStringLiteral("Light"),
                     QStringLiteral("A light window around the dark canvas")}}),
        radioField(QStringLiteral("Items/lod_method"), QStringLiteral("LOD Method:"),
                   QStringLiteral("How image levels of detail are generated. Applies to images "
                                  "that have been saved to a bee file."),
                   {{QStringLiteral("single"), QStringLiteral("One LOD per image"),
                     QStringLiteral("Images are always kept in memory at full resolution")},
                    {QStringLiteral("fixed"), QStringLiteral("Fixed fractions"),
                     QStringLiteral("Multiple levels at fixed fractions of the original size")},
                    {QStringLiteral("ram_budget"), QStringLiteral("RAM budget"),
                     QStringLiteral("Like fixed fractions, but levels are downgraded to keep "
                                    "the total memory usage below the RAM budget")}}),
        makeField(QStringLiteral("Items/lod_fractions"), QStringLiteral("LOD Fractions:"),
                  QStringLiteral("Comma-separated list of fractions (0-1) used by the fixed "
                                 "fractions and RAM budget LOD methods. E.g. "
                                 "\"1,0.5,0.25,0.125,0.0625\""),
                  Kind::LineEdit),
        radioField(QStringLiteral("Items/lod_quality"), QStringLiteral("LOD Image Quality:"),
                   QStringLiteral("How LOD levels are downscaled. Smooth reduces moiré on "
                                  "patterned images at a small decoding cost; Fast is quicker "
                                  "when decoding many levels."),
                   {{QStringLiteral("smooth"), QStringLiteral("Smooth"),
                     QStringLiteral("Progressive halving filter; reduces moiré and aliasing")},
                    {QStringLiteral("fast"), QStringLiteral("Fast"),
                     QStringLiteral("Single-step scaling; faster level decoding")}}),
        checkboxField(QStringLiteral("Items/undo_cache"),
                      QStringLiteral("Session disk cache:"),
                      QStringLiteral("Keep session data on disk instead of in memory: decoded "
                                     "LOD levels and the encoded bytes of images held in the "
                                     "undo history. The cache file is deleted when BeeXRef "
                                     "exits."),
                      QStringLiteral("Cache session data to disk")),
        integerField(QStringLiteral("Items/lod_decode_threads"),
                     QStringLiteral("Decode Threads:"),
                     QStringLiteral("How many worker threads decode image levels in the "
                                    "background. More threads fill the screen faster on multi-core "
                                    "machines; one is always enough to work."),
                     1, 64),
        integerField(QStringLiteral("Items/lod_ram_cache_mb"),
                     QStringLiteral("Decoded Level Cache (MB, 0 = off):"),
                     QStringLiteral("Decoded levels kept in memory so revisiting an image (for "
                                    "example panning back) does not read or decode it again. "
                                    "Costs no extra memory for levels already on screen."),
                     0, 65536),
        integerField(QStringLiteral("Items/lod_cache_settle_percent"),
                     QStringLiteral("Release Off-screen Cache (% per 10s, 0 = off):"),
                     QStringLiteral("After 10 seconds without interaction, this share of the "
                                    "off-screen decoded-level cache is released, oldest first, "
                                    "repeated every 10 seconds. The viewport is never touched. "
                                    "0 keeps the cache."),
                     0, 50),
        integerField(QStringLiteral("Items/lod_primary_budget_mb"),
                     QStringLiteral("Memory Budget (MB, 0 = off):"),
                     QStringLiteral("Always-on cap on the decoded image bytes kept in memory. "
                                    "Requests that would exceed it are not decoded. 0 leaves it "
                                    "unlimited."),
                     0, 65536),
        radioField(QStringLiteral("Items/image_storage_format"),
                   QStringLiteral("Image Storage:"),
                   QStringLiteral("How images entering the board are encoded. Existing images "
                                  "are left alone; File ▸ Compact Board re-encodes them. "
                                  "Already lossy sources (jpeg, webp) are never re-encoded."),
                   {{QStringLiteral("original"), QStringLiteral("Keep Originals"),
                     QStringLiteral("Store the source bytes as they are; best quality, "
                                    "largest board")},
                    {QStringLiteral("lossless"), QStringLiteral("Compact Lossless"),
                     QStringLiteral("Re-encode losslessly as WebP when that is smaller; no "
                                    "pixel changes")},
                    {QStringLiteral("compact"), QStringLiteral("Compact (Imperceptible)"),
                     QStringLiteral("WebP at quality 95 for photographs; transparency stays "
                                    "lossless")}}),
        integerField(QStringLiteral("Items/image_allocation_limit"),
                     QStringLiteral("Maximum Image Size:"),
                     QStringLiteral("The maximum image size that can be loaded (in megabytes). "
                                    "Set to 0 for no limitation."),
                     0, 10000),
        integerField(QStringLiteral("Items/arrange_gap"), QStringLiteral("Arrange Gap:"),
                     QStringLiteral("The gap between images when using arrange actions."), 0,
                     200),
        radioField(QStringLiteral("Items/arrange_default"),
                   QStringLiteral("Default Arrange Method:"),
                   QStringLiteral("How images are arranged when inserted in batch"),
                   {{QStringLiteral("optimal"), QStringLiteral("Optimal"),
                     QStringLiteral("Arrange Optimal")},
                    {QStringLiteral("horizontal"), QStringLiteral("Horizontal (by filename)"),
                     QStringLiteral("Arrange Horizontal (by filename)")},
                    {QStringLiteral("vertical"), QStringLiteral("Vertical (by filename)"),
                     QStringLiteral("Arrange Vertical (by filename)")},
                    {QStringLiteral("square"), QStringLiteral("Square (by filename)"),
                     QStringLiteral("Arrange Square (by filename)")}}),
        checkboxField(QStringLiteral("Items/double_click_spotlight"),
                      QStringLiteral("Double-click an image:"),
                      QStringLiteral("Double-clicking an image fits it in the view. Should it "
                                     "also be spotlighted, a view-only raise above the other "
                                     "images? The configured z-order is not changed."),
                      QStringLiteral("Also spotlight it")),
    };
    return fields;
}

QString valueToText(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
        return QString::number(value.toInt());
    default:
        return value.toString();
    }
}

QString sectionOf(const QString &key)
{
    return key.section(QLatin1Char('/'), 0, 0);
}

QString nameOf(const QString &key)
{
    return key.section(QLatin1Char('/'), 1);
}

} // namespace

// --- SettingsGroup ----------------------------------------------------

SettingsGroup::SettingsGroup(const FieldUi &field, QWidget *parent)
    : QGroupBox(parent)
    , field_(field)
{
    setObjectName(field.key);
    auto *layout = new QVBoxLayout(this);
    if (!field.help.isEmpty()) {
        auto *help = new QLabel(field.help, this);
        help->setWordWrap(true);
        layout->addWidget(help);
    }

    settings::File file(settings::iniPath());
    file.load();
    const QVariant value = settings::valueOrDefault(file, field.key);
    markChanged(file);

    switch (field.kind) {
    case Kind::Radio: {
        auto *group = new QWidget(this);
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        for (const FieldUi::Option &option : field.options) {
            auto *button = new QRadioButton(option.label, group);
            button->setObjectName(option.value);
            if (!option.tip.isEmpty())
                button->setToolTip(option.tip);
            button->setChecked(option.value == value.toString());
            connect(button, &QRadioButton::toggled, this, [this, option](bool checked) {
                if (checked)
                    applyValue(option.value);
            });
            groupLayout->addWidget(button);
        }
        input_ = group;
        layout->addWidget(group);
        break;
    }
    case Kind::Integer: {
        auto *spin = new QSpinBox(this);
        spin->setRange(field.minimum, field.maximum);
        spin->setValue(value.toInt());
        connect(spin, &QSpinBox::valueChanged, this,
                [this](int number) { applyValue(number); });
        input_ = spin;
        layout->addWidget(spin);
        break;
    }
    case Kind::Checkbox: {
        auto *box = new QCheckBox(field.label, this);
        box->setChecked(value.toBool());
        connect(box, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) { applyValue(state == Qt::Checked); });
        input_ = box;
        layout->addWidget(box);
        break;
    }
    case Kind::LineEdit: {
        auto *edit = new QLineEdit(value.toString(), this);
        connect(edit, &QLineEdit::textChanged, this,
                [this](const QString &text) { applyValue(text); });
        input_ = edit;
        layout->addWidget(edit);
        break;
    }
    }
    if (input_)
        input_->setObjectName(field.key);
    layout->addStretch(100);
}

void SettingsGroup::applyValue(const QVariant &value)
{
    if (ignore_)
        return;
    settings::File file(settings::iniPath());
    file.load();
    const QString text = valueToText(value);
    if (text == file.value(sectionOf(field_.key), nameOf(field_.key))) {
        // Already what is on disk; still refresh the changed marker.
        markChanged(file);
        return;
    }
    file.setValue(sectionOf(field_.key), nameOf(field_.key), text);
    file.sync();
    markChanged(file);
    emit changed(field_.key);
}

void SettingsGroup::writeTo(settings::File &file) const
{
    QVariant value;
    switch (field_.kind) {
    case Kind::Radio: {
        for (const FieldUi::Option &option : field_.options) {
            auto *button = input_->findChild<QRadioButton *>(option.value);
            if (button && button->isChecked())
                value = option.value;
        }
        break;
    }
    case Kind::Integer:
        value = static_cast<QSpinBox *>(input_)->value();
        break;
    case Kind::Checkbox:
        value = static_cast<QCheckBox *>(input_)->isChecked();
        break;
    case Kind::LineEdit:
        value = static_cast<QLineEdit *>(input_)->text();
        break;
    }
    file.setValue(sectionOf(field_.key), nameOf(field_.key), valueToText(value));
}

void SettingsGroup::refresh(const settings::File &file)
{
    ignore_ = true;
    const QVariant value = settings::valueOrDefault(file, field_.key);
    switch (field_.kind) {
    case Kind::Radio:
        for (const FieldUi::Option &option : field_.options) {
            auto *button = input_->findChild<QRadioButton *>(option.value);
            if (button)
                button->setChecked(option.value == value.toString());
        }
        break;
    case Kind::Integer:
        static_cast<QSpinBox *>(input_)->setValue(value.toInt());
        break;
    case Kind::Checkbox:
        static_cast<QCheckBox *>(input_)->setChecked(value.toBool());
        break;
    case Kind::LineEdit:
        static_cast<QLineEdit *>(input_)->setText(value.toString());
        break;
    }
    ignore_ = false;
    markChanged(file);
}

void SettingsGroup::markChanged(const settings::File &file)
{
    QString title = field_.title;
    if (settings::valueChanged(file, field_.key))
        title += QLatin1Char(' ') + QString::fromUtf8(constants::kChangedSymbol);
    setTitle(title);
}

// --- SettingsDialog ---------------------------------------------------

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , file_(settings::iniPath())
{
    setWindowTitle(QStringLiteral("%1 Settings").arg(QString::fromLatin1(constants::AppName)));
    load();

    auto *tabs = new QTabWidget(this);

    // Groups are placed by key, so the table order can change freely.
    const auto group = [this](const QString &key) -> SettingsGroup * {
        for (SettingsGroup *candidate : std::as_const(groups_)) {
            if (candidate->key() == key)
                return candidate;
        }
        return nullptr;
    };

    auto *misc = new QWidget(tabs);
    auto *miscLayout = new QGridLayout(misc);
    misc->setLayout(miscLayout);
    miscLayout->addWidget(group(QStringLiteral("Save/confirm_close_unsaved")), 0, 0);
    miscLayout->addWidget(group(QStringLiteral("Save/incremental")), 1, 0);
    miscLayout->addWidget(group(QStringLiteral("View/theme")), 2, 0);
    tabs->addTab(misc, QStringLiteral("&Miscellaneous"));

    auto *perf = new QWidget(tabs);
    auto *perfLayout = new QGridLayout(perf);
    perf->setLayout(perfLayout);
    perfLayout->addWidget(group(QStringLiteral("Items/lod_decode_threads")), 0, 0);
    perfLayout->addWidget(group(QStringLiteral("Items/lod_ram_cache_mb")), 0, 1);
    perfLayout->addWidget(group(QStringLiteral("Items/lod_cache_settle_percent")), 1, 0);
    perfLayout->addWidget(group(QStringLiteral("Items/lod_primary_budget_mb")), 1, 1);
    perfLayout->addWidget(group(QStringLiteral("Items/image_allocation_limit")), 2, 0);
    perfLayout->addWidget(group(QStringLiteral("Items/undo_cache")), 2, 1);
    tabs->addTab(perf, QStringLiteral("&Performance"));

    auto *lod = new QWidget(tabs);
    auto *lodLayout = new QGridLayout(lod);
    lod->setLayout(lodLayout);
    lodLayout->addWidget(group(QStringLiteral("Items/lod_method")), 0, 0);
    lodLayout->addWidget(group(QStringLiteral("Items/lod_quality")), 0, 1);
    lodLayout->addWidget(group(QStringLiteral("Items/lod_fractions")), 1, 0, 1, 2);
    tabs->addTab(lod, QStringLiteral("&LOD"));

    auto *items = new QWidget(tabs);
    auto *itemsLayout = new QGridLayout(items);
    items->setLayout(itemsLayout);
    itemsLayout->addWidget(group(QStringLiteral("Items/image_storage_format")), 0, 0);
    itemsLayout->addWidget(group(QStringLiteral("Items/arrange_gap")), 0, 1);
    itemsLayout->addWidget(group(QStringLiteral("Items/arrange_default")), 1, 0, 1, 2);
    itemsLayout->addWidget(group(QStringLiteral("Items/double_click_spotlight")), 2, 0, 1, 2);
    tabs->addTab(items, QStringLiteral("&Images && Items"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *resetButton = new QPushButton(QStringLiteral("&Restore Defaults"), this);
    resetButton->setAutoDefault(false);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this, QStringLiteral("Restore defaults?"),
            QStringLiteral("Do you want to restore all settings to their default values?"));
        if (reply == QMessageBox::Yes)
            restoreDefaults();
    });
    buttons->addButton(resetButton, QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    resize(760, 520);
}

void SettingsDialog::load()
{
    file_.load();
    for (const FieldUi &field : fieldTable()) {
        auto *group = new SettingsGroup(field, this);
        connect(group, &SettingsGroup::changed, this, &SettingsDialog::settingChanged);
        groups_.append(group);
    }
}

void SettingsDialog::restoreDefaults()
{
    settings::restoreDefaults(file_);
    file_.sync();
    for (SettingsGroup *group : std::as_const(groups_))
        group->refresh(file_);
    logging::info(QStringLiteral("Settings restored to defaults"));
    emit settingsRestored();
}

} // namespace ui
