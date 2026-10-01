#include "metadata_panel.h"

#include "hud.h"
#include "scene.h"
#include "scene_item.h"
#include "selection_ops.h"
#include "settings.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDesktopServices>
#include <QEvent>
#include <QMouseEvent>
#include <QCompleter>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringListModel>
#include <QTabWidget>
#include <QTextCursor>
#include <QVBoxLayout>

namespace ui {
namespace {

const QString kName = QStringLiteral("name");
const QString kAuthor = QStringLiteral("author");
const QString kYear = QStringLiteral("year");
const QString kCollection = QStringLiteral("collection");
const QString kOriginUrl = QStringLiteral("origin_url");
const QString kNotes = QStringLiteral("notes");

// How many suggestions a completion popup shows.
constexpr int kCompletionLimit = 15;

// What a batch field whose images disagree shows, so the panel never
// claims a shared value it does not have.
const QString kMixed = QStringLiteral("(multiple)");

// The edit limits of the metadata fields. They clamp what is typed or
// pasted, not what a board already holds: populate() writes the stored
// values with the signals blocked and the line edits are not limited by
// property (setMaxLength truncates setText too), so opening and saving a
// board never rewrites a longer value it did not edit. Name matches the
// usual filesystem limit, author and collection keep the completion
// folding cheap and the OSD readable, URL matches the browsers'
// practical limit, the year holds a sign and six digits, and notes stay
// a couple of pages (they live in the items' JSON, so 3000 of them are
// still small).
constexpr int kNameLimit = 255;
constexpr int kAuthorLimit = 128;
constexpr int kCollectionLimit = 128;
constexpr int kUrlLimit = 2048;
constexpr int kNotesLimit = 4096;
constexpr int kYearLimit = 7;
constexpr int kYearMax = 999999;

// How one field is stored and edited.
enum class FieldKind {
    Text,      // a single-line string
    Multiline, // a string with line breaks
    Integer,   // a JSON number; an empty editor removes the key
};

// The metadata fields, in the order the Meta tab shows them. This table
// is the single place a field is declared: buildMetaFields, populate,
// draftEdits, commitDraft and the suggestion setup all iterate it. The
// label names both the field and its editor widget (panel + label).
struct FieldSpec
{
    QString key;
    FieldKind kind;
    int limit;
    bool suggestions;
    // Shared fields can be written to a whole selection at once; the
    // per-image ones (name, URL, notes) stay single-image.
    bool batch;
    QString label;
};

const QVector<FieldSpec> &fieldSpecs()
{
    static const QVector<FieldSpec> specs = {
        {kName, FieldKind::Text, kNameLimit, false, false, QStringLiteral("Name")},
        {kAuthor, FieldKind::Text, kAuthorLimit, true, true, QStringLiteral("Author")},
        {kYear, FieldKind::Integer, kYearLimit, false, true, QStringLiteral("Year")},
        {kCollection, FieldKind::Text, kCollectionLimit, true, true,
         QStringLiteral("Collection")},
        {kOriginUrl, FieldKind::Text, kUrlLimit, false, false, QStringLiteral("URL")},
        {kNotes, FieldKind::Multiline, kNotesLimit, false, false, QStringLiteral("Notes")},
    };
    return specs;
}

// The schemes the app is willing to hand to the desktop. http and https
// only: a file path or a script in a metadata field must never launch.
const QStringList &openableSchemes()
{
    static const QStringList schemes = {QStringLiteral("http"), QStringLiteral("https")};
    return schemes;
}

// The stored text of one metadata field, normalized on commit:
// single-line fields collapse every whitespace run -- newlines included
// -- into one space; the multi-line notes keep their line breaks with the
// runs of blanks inside each line collapsed. Both trim the edges.
QString normalizedFieldText(QString text, bool singleLine)
{
    static const QRegularExpression anySpace(QStringLiteral("\\s+"));
    static const QRegularExpression blanks(QStringLiteral("[ \\t\\r\\f\\v]+"));
    if (singleLine)
        return text.replace(anySpace, QStringLiteral(" ")).trimmed();
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines)
        line = line.replace(blanks, QStringLiteral(" ")).trimmed();
    return lines.join(QLatin1Char('\n')).trimmed();
}

QString number(double value, int precision)
{
    return QString::number(value, 'f', precision);
}

// Applies one edited value to a stored (filename, meta) pair: a name
// replaces the filename, a year is a JSON number (an empty year removes
// the key) and anything else is a string in the meta object. A year
// that cannot be parsed (a half-typed leftover) is left alone.
void applyMetadataChange(QString &filename, QJsonObject &meta, const QString &key,
                         const QString &text, bool integer)
{
    if (integer) {
        if (text.isEmpty()) {
            meta.remove(key);
            return;
        }
        bool ok = false;
        const int year = text.toInt(&ok);
        if (ok)
            meta.insert(key, QJsonValue(year));
        return;
    }
    if (key == kName)
        filename = text;
    else
        meta.insert(key, text);
}

} // namespace

QVector<QPair<QString, QString>> itemInfoRows(const SceneItem *view)
{
    QVector<QPair<QString, QString>> rows;
    if (!view)
        return rows;
    const doc::Item &item = *view->item();

    const QString source =
        item.filename.isEmpty() ? item.data.value(QStringLiteral("filename")).toString()
                                : item.filename;
    QSize size = item.originalSize();
    if (!size.isValid() || size.isEmpty())
        size = view->boundingRect().size().toSize();
    const QString format = item.format.isEmpty() ? QStringLiteral("?") : item.format;
    const QRectF crop = item.hasCrop() ? item.crop() : view->boundingRect();

    rows.append({QStringLiteral("Source"), source});
    rows.append({QStringLiteral("Size"),
                 QStringLiteral("%1 x %2").arg(size.width()).arg(size.height())});
    rows.append({QStringLiteral("Format"), format});
    rows.append({QStringLiteral("Position"),
                 QStringLiteral("%1, %2").arg(number(item.x, 1), number(item.y, 1))});
    rows.append({QStringLiteral("Z-order"), number(item.z, 1)});
    rows.append({QStringLiteral("Scale"), number(item.scale, 2)});
    rows.append({QStringLiteral("Rotation"), QStringLiteral("%1°").arg(number(item.rotation, 1))});
    rows.append({QStringLiteral("Flipped"),
                 item.flip < 0 ? QStringLiteral("Yes") : QStringLiteral("No")});
    rows.append({QStringLiteral("Opacity"),
                 QStringLiteral("%1%").arg(number(item.opacity() * 100.0, 0))});
    rows.append({QStringLiteral("Grayscale"),
                 item.grayscale() ? QStringLiteral("Yes") : QStringLiteral("No")});
    rows.append({QStringLiteral("Crop"),
                 QStringLiteral("%1, %2, %3 x %4")
                     .arg(number(crop.x(), 1), number(crop.y(), 1), number(crop.width(), 1))
                     .arg(number(crop.height(), 1))});
    rows.append({QStringLiteral("Save ID"),
                 item.id != 0 ? QString::number(item.id) : QStringLiteral("Not saved")});
    return rows;
}

MetadataPanel::MetadataPanel(Scene *scene, doc::UndoStack *stack, QWidget *parent)
    : QWidget(parent)
    , scene_(scene)
    , stack_(stack)
{
    setObjectName(QStringLiteral("MetadataPanel"));
    setMinimumWidth(kMinWidth);

    {
        settings::File file(settings::iniPath());
        file.load();
        keep_ = file.boolValue(QStringLiteral("View"), QStringLiteral("panel_keep"), false);
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    titleLabel_ = new QLabel(QStringLiteral("Item"), this);
    titleLabel_->setObjectName(QStringLiteral("panelTitle"));
    QFont bold = titleLabel_->font();
    bold.setBold(true);
    titleLabel_->setFont(bold);
    layout->addWidget(titleLabel_);

    tabs_ = new QTabWidget(this);

    // Info: read-only rows, rebuilt per item.
    auto *infoScroll = new QScrollArea(tabs_);
    infoScroll->setWidgetResizable(true);
    infoPage_ = new QWidget(infoScroll);
    infoForm_ = new QFormLayout(infoPage_);
    infoForm_->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    infoScroll->setWidget(infoPage_);
    tabs_->addTab(infoScroll, QStringLiteral("Info"));

    // Meta: the draft.
    auto *metaScroll = new QScrollArea(tabs_);
    metaScroll->setWidgetResizable(true);
    auto *metaPage = new QWidget(metaScroll);
    auto *metaLayout = new QVBoxLayout(metaPage);
    buildMetaFields(metaLayout);
    metaScroll->setWidget(metaPage);
    tabs_->addTab(metaScroll, QStringLiteral("Meta"));
    layout->addWidget(tabs_, 1);

    auto *footer = new QHBoxLayout();
    keepBox_ = new QCheckBox(QStringLiteral("Keep"), this);
    keepBox_->setObjectName(QStringLiteral("panelKeep"));
    keepBox_->setChecked(keep_);
    footer->addWidget(keepBox_);
    footer->addStretch();
    closeButton_ = new QPushButton(QStringLiteral("Close"), this);
    closeButton_->setObjectName(QStringLiteral("panelClose"));
    closeButton_->setAutoDefault(false);
    footer->addWidget(closeButton_);
    saveButton_ = new QPushButton(QStringLiteral("Save"), this);
    saveButton_->setObjectName(QStringLiteral("panelSave"));
    saveButton_->setAutoDefault(false);
    saveButton_->setEnabled(false);
    footer->addWidget(saveButton_);
    layout->addLayout(footer);

    connect(saveButton_, &QPushButton::clicked, this, [this]() { commitDraft(); });
    connect(closeButton_, &QPushButton::clicked, this, [this]() { closePanel(); });
    connect(keepBox_, &QCheckBox::checkStateChanged, this,
            [this](Qt::CheckState state) { setKeep(state == Qt::Checked); });

    // Undo and redo must be reflected in an open panel.
    if (stack_) {
        stack_->addChangedCallback([this]() {
            markSuggestionsDirty();
            if (!batchItems_.isEmpty())
                populateBatch(batchItems_);
            else if (item_)
                populate(item_);
        });
    }

    setPanelVisible(false);
}

void MetadataPanel::setScene(Scene *scene)
{
    if (scene_ == scene)
        return;
    if (scene_)
        disconnect(scene_, nullptr, this, nullptr);
    scene_ = scene;
    markSuggestionsDirty();
    if (scene_) {
        connect(scene_, &QGraphicsScene::selectionChanged, this, [this]() { refresh(); });
        connect(scene_, &Scene::itemsChanged, this, [this]() { markSuggestionsDirty(); });
    }
    closePanel();
}

void MetadataPanel::setKeep(bool keep)
{
    keep_ = keep;
    {
        settings::File file(settings::iniPath());
        file.load();
        file.setValue(QStringLiteral("View"), QStringLiteral("panel_keep"),
                      keep ? QStringLiteral("true") : QStringLiteral("false"));
        file.sync();
    }
    if (!keep)
        manual_ = isVisible() && scene_ && selection::selectionItems(*scene_).size() == 1;
    refresh();
}

void MetadataPanel::toggle()
{
    if (isVisible()) {
        closePanel();
        return;
    }
    manual_ = true;
    refresh();
}

void MetadataPanel::refresh()
{
    if (!scene_) {
        closePanel();
        return;
    }
    const QVector<SceneItem *> selected = selection::selectionItems(*scene_);
    const QVector<SceneItem *> images = selection::imageSelection(*scene_);
    const bool one = selected.size() == 1 && images.size() == 1;
    const bool batch = images.size() >= 2;

    if (one) {
        // A manual panel with no item yet is the one just opened; with
        // Keep it follows whichever single image is selected.
        const bool mine =
            keep_ || (manual_ && (item_ == nullptr || item_ == selected.first()));
        if (mine) {
            if (item_ != selected.first())
                commitDraft();
            setPanelVisible(true);
            populate(selected.first());
            return;
        }
    } else if (batch) {
        // A batch panel edits whichever images are selected: a new batch
        // commits the draft and repopulates instead of closing, so a
        // selection can be built one image at a time.
        if (keep_ || manual_) {
            if (batchItems_ != images)
                commitDraft();
            setPanelVisible(true);
            populateBatch(images);
            return;
        }
    }

    // Any other selection commits the draft and closes the panel, as
    // the Go port's one-shot panel does.
    commitDraft();
    item_ = nullptr;
    batchItems_.clear();
    manual_ = false;
    setPanelVisible(false);
}

void MetadataPanel::closePanel()
{
    item_ = nullptr;
    batchItems_.clear();
    manual_ = false;
    saveButton_->setEnabled(false);
    titleLabel_->setText(QStringLiteral("Item"));
    setPanelVisible(false);
}

void MetadataPanel::forgetItem(SceneItem *view)
{
    if (item_ == view || batchItems_.contains(view))
        closePanel();
}

bool MetadataPanel::isDirty() const
{
    return (item_ != nullptr || !batchItems_.isEmpty()) && !draftEdits().isEmpty();
}

void MetadataPanel::setPanelVisible(bool visible)
{
    if (isVisible() == visible && !visible) {
        // Still hidden: keep the caller's state consistent.
        if (!isHidden())
            hide();
        return;
    }
    if (visible == isVisible())
        return;
    if (visible) {
        show();
    } else {
        // Remember the width the user had, so reopening restores it.
        if (width() > kMinWidth)
            preferredWidth_ = width();
        hide();
    }
    emit visibilityChanged();
}

void MetadataPanel::populate(SceneItem *item)
{
    item_ = item;
    if (!item_)
        return;
    batchItems_.clear();
    applyFieldVisibility();
    for (Field &field : fields_) {
        if (field.batch) {
            if (auto *line = qobject_cast<QLineEdit *>(field.editor))
                line->setPlaceholderText(field.labelText);
        }
    }
    populating_ = true;
    for (Field &field : fields_)
        field.editor->blockSignals(true);
    for (Field &field : fields_)
        setEditorText(field, storedValue(field));
    for (Field &field : fields_)
        field.editor->blockSignals(false);
    populating_ = false;
    if (Field *url = fieldFor(kOriginUrl))
        updateUrlAffordance(*url);

    saveButton_->setEnabled(false);
    titleLabel_->setText(QStringLiteral("Image"));
    tabs_->setTabVisible(0, true);
    rebuildInfoRows(item);
}

void MetadataPanel::populateBatch(const QVector<SceneItem *> &items)
{
    item_ = nullptr;
    batchItems_ = items;
    if (batchItems_.isEmpty())
        return;
    applyFieldVisibility();

    populating_ = true;
    for (Field &field : fields_)
        field.editor->blockSignals(true);
    for (Field &field : fields_) {
        field.touched = false;
        if (!field.batch)
            continue;
        // The value the whole batch shares, if any; a mixed field starts
        // empty and says so, and only what was typed is written back.
        bool same = true;
        const QString value = itemValue(*items.first()->item(), field);
        for (SceneItem *view : items) {
            if (itemValue(*view->item(), field) != value) {
                same = false;
                break;
            }
        }
        field.baseline = same ? value : QString();
        setEditorText(field, field.baseline);
        if (auto *line = qobject_cast<QLineEdit *>(field.editor))
            line->setPlaceholderText(same ? field.labelText : kMixed);
    }
    for (Field &field : fields_)
        field.editor->blockSignals(false);
    populating_ = false;

    // The per-image fields and the Info tab do not describe a batch.
    tabs_->setTabVisible(0, false);
    tabs_->setCurrentIndex(1);
    saveButton_->setEnabled(false);
    titleLabel_->setText(baseTitle());
}

// Shows only the fields that apply to the current mode, rebuilding the
// row order so a hidden field leaves no gap behind; the batch keeps its
// rows packed at the top, like the single-image page.
void MetadataPanel::applyFieldVisibility()
{
    const bool batch = !batchItems_.isEmpty();
    while (QLayoutItem *item = metaLayout_->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->hide();
        delete item;
    }
    for (Field &field : fields_) {
        const bool visible = !batch || field.batch;
        field.label->setVisible(visible);
        field.editor->setVisible(visible);
        if (!visible)
            continue;
        metaLayout_->addWidget(field.label);
        metaLayout_->addWidget(field.editor, field.multiline ? 1 : 0);
    }
    if (batch)
        metaLayout_->addStretch(1);
}

QString MetadataPanel::baseTitle() const
{
    if (!batchItems_.isEmpty())
        return QStringLiteral("%1 images").arg(batchItems_.size());
    return QStringLiteral("Image");
}

void MetadataPanel::rebuildInfoRows(SceneItem *item)
{
    while (infoForm_->rowCount() > 0)
        infoForm_->removeRow(0);
    for (const auto &row : itemInfoRows(item)) {
        auto *name = new QLabel(QStringLiteral("%1:").arg(row.first), infoPage_);
        QFont bold = name->font();
        bold.setBold(true);
        name->setFont(bold);
        auto *value = new QLabel(row.second, infoPage_);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoForm_->addRow(name, value);
    }
}

QVector<QPair<QString, QString>> MetadataPanel::draftEdits() const
{
    QVector<QPair<QString, QString>> changes;
    const bool batch = !batchItems_.isEmpty();
    if (!batch && !item_)
        return changes;
    // The values are normalized here, so what is compared (and stored) is
    // the canonical form. The integer fields keep their text as typed
    // (only trimmed) and are parsed when they are written.
    for (const Field &field : fields_) {
        if (batch && !field.batch)
            continue;
        const QString edited = field.integer
            ? editorText(field).trimmed()
            : normalizedFieldText(editorText(field), !field.multiline);
        // In a batch a touched field applies even when it went back to
        // its starting value: that is how a mixed field is cleared.
        if (edited != storedValue(field) || (batch && field.touched))
            changes.append({field.key, edited});
    }
    return changes;
}

QString MetadataPanel::editorText(const Field &field) const
{
    if (field.multiline)
        return static_cast<QPlainTextEdit *>(field.editor)->toPlainText();
    return static_cast<QLineEdit *>(field.editor)->text();
}

void MetadataPanel::setEditorText(const Field &field, const QString &text)
{
    if (field.multiline)
        static_cast<QPlainTextEdit *>(field.editor)->setPlainText(text);
    else
        static_cast<QLineEdit *>(field.editor)->setText(text);
}

QString MetadataPanel::storedValue(const Field &field) const
{
    // A batch draft compares against the value the batch started from.
    if (!batchItems_.isEmpty())
        return field.baseline;
    if (!item_)
        return {};
    return itemValue(*item_->item(), field);
}

// The stored text of one field on one item: what populate() shows and
// the batch compares across images.
QString MetadataPanel::itemValue(const doc::Item &item, const Field &field) const
{
    if (field.key == kName)
        return item.filename;
    const QJsonValue value = item.meta.value(field.key);
    if (field.integer) {
        // A number round-trips as it was typed; a foreign string (another
        // tool's "c. 1880") is shown as it is and only replaced when the
        // field is edited.
        if (value.isDouble())
            return QString::number(qRound64(value.toDouble()));
        return value.toString();
    }
    return value.toString();
}

MetadataPanel::Field *MetadataPanel::fieldFor(const QString &key)
{
    for (Field &field : fields_) {
        if (field.key == key)
            return &field;
    }
    return nullptr;
}

void MetadataPanel::updateDirty()
{
    if (populating_ || (batchItems_.isEmpty() && !item_))
        return;
    const bool dirty = isDirty();
    saveButton_->setEnabled(dirty);
    const QString base = baseTitle();
    titleLabel_->setText(dirty ? base + QStringLiteral(" \u2022") : base);
}

void MetadataPanel::markSuggestionsDirty()
{
    for (Field &field : fields_)
        field.candidatesDirty = true;
}

void MetadataPanel::showSuggestions(Field &field, const QString &text)
{
    if (!field.completer || !scene_)
        return;
    if (field.candidatesDirty) {
        QStringList raw;
        const QList<SceneItem *> views = scene_->itemViews();
        raw.reserve(views.size());
        for (SceneItem *view : views)
            raw.append(view->item()->meta.value(field.key).toString());
        field.candidates = fuzzy::candidates(raw);
        field.candidatesDirty = false;
    }
    const QVector<fuzzy::Candidate> matches =
        fuzzy::search(field.candidates, text, kCompletionLimit);
    QStringList rows;
    rows.reserve(matches.size());
    for (const fuzzy::Candidate &candidate : matches)
        rows.append(candidate.display);
    field.model->setStringList(rows);
    if (rows.isEmpty()) {
        field.completer->popup()->hide();
        return;
    }
    // UnfilteredPopupCompletion shows the model as it is: the fuzzy
    // matches, no prefix filtering on top.
    field.completer->complete();
}

void MetadataPanel::buildMetaFields(QVBoxLayout *layout)
{
    metaLayout_ = layout;
    QWidget *page = layout->parentWidget();
    const QVector<FieldSpec> &specs = fieldSpecs();
    fields_.reserve(specs.size());
    for (const FieldSpec &spec : specs) {
        Field field;
        field.key = spec.key;
        field.integer = spec.kind == FieldKind::Integer;
        field.multiline = spec.kind == FieldKind::Multiline;
        field.limit = spec.limit;
        field.suggestions = spec.suggestions;
        field.batch = spec.batch;

        field.labelText = spec.label;
        field.label = new QLabel(field.labelText, page);
        layout->addWidget(field.label);
        const QString editorName = QStringLiteral("panel") + spec.label.at(0).toUpper()
            + spec.label.mid(1).toLower();
        if (field.multiline) {
            auto *editor = new QPlainTextEdit(page);
            editor->setObjectName(editorName);
            editor->setPlaceholderText(spec.label);
            field.editor = editor;
            layout->addWidget(editor, 1);
        } else {
            auto *editor = new QLineEdit(page);
            editor->setObjectName(editorName);
            editor->setPlaceholderText(spec.label);
            if (field.integer)
                editor->setValidator(new QIntValidator(-kYearMax, kYearMax, editor));
            field.editor = editor;
            layout->addWidget(editor);
        }
        fields_.append(field);
    }
    // Wire once every field exists, so the lambdas keep a stable pointer
    // into the table.
    for (Field &field : fields_) {
        wireField(field);
        if (field.key == kOriginUrl) {
            field.label->installEventFilter(this);
            updateUrlAffordance(field);
        }
    }
}

void MetadataPanel::wireField(Field &field)
{
    Field *entry = &field;
    if (field.multiline) {
        auto *editor = static_cast<QPlainTextEdit *>(field.editor);
        connect(editor, &QPlainTextEdit::textChanged, this, [this, entry]() {
            clampField(*entry);
            updateDirty();
        });
        return;
    }
    auto *editor = static_cast<QLineEdit *>(field.editor);
    connect(editor, &QLineEdit::textChanged, this, [this]() { updateDirty(); });
    connect(editor, &QLineEdit::textEdited, this, [entry]() { entry->touched = true; });
    if (field.key == kOriginUrl) {
        connect(editor, &QLineEdit::textChanged, this,
                [this, entry]() { updateUrlAffordance(*entry); });
    }
    // The clamp only covers user edits (textEdited): a longer value a
    // board already holds is shown as it is and only clamped if edited.
    connect(editor, &QLineEdit::textEdited, this, [this, entry](const QString &text) {
        if (!entry->integer && text.size() > entry->limit) {
            const int cursor = static_cast<QLineEdit *>(entry->editor)->cursorPosition();
            {
                const QSignalBlocker blocker(entry->editor);
                static_cast<QLineEdit *>(entry->editor)->setText(text.left(entry->limit));
                static_cast<QLineEdit *>(entry->editor)
                    ->setCursorPosition(qMin(cursor, entry->limit));
            }
            updateDirty();
            return;
        }
        if (entry->suggestions)
            showSuggestions(*entry, text);
    });
    if (field.suggestions)
        setupSuggestions(field);
}

void MetadataPanel::setupSuggestions(Field &field)
{
    auto *editor = static_cast<QLineEdit *>(field.editor);
    field.model = new QStringListModel(this);
    field.completer = new QCompleter(field.model, this);
    field.completer->setWidget(editor);
    // Unfiltered: the model already holds the fuzzy matches, and the line
    // edit overwrites the completion prefix with the typed text, so a
    // prefix-filtered popup would drop them (it matches on the display
    // text, accents and all).
    field.completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    field.completer->setCaseSensitivity(Qt::CaseInsensitive);
    editor->setCompleter(field.completer);
    connect(field.completer, QOverload<const QString &>::of(&QCompleter::activated), this,
            [editor](const QString &text) { editor->setText(text); });
}

void MetadataPanel::clampField(Field &field)
{
    if (!field.multiline)
        return;
    auto *editor = static_cast<QPlainTextEdit *>(field.editor);
    const QString text = editor->toPlainText();
    if (text.size() <= field.limit)
        return;
    const int cursor = editor->textCursor().position();
    const QSignalBlocker blocker(editor);
    editor->setPlainText(text.left(field.limit));
    QTextCursor clamped = editor->textCursor();
    clamped.setPosition(qMin(cursor, field.limit));
    editor->setTextCursor(clamped);
}

QUrl openableWebUrl(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QUrl url(trimmed, QUrl::StrictMode);
    if (!url.isValid() || !openableSchemes().contains(url.scheme().toLower()))
        return {};
    return url;
}

void MetadataPanel::updateUrlAffordance(Field &field)
{
    if (!field.label || field.key != kOriginUrl)
        return;
    const bool openable = openableWebUrl(editorText(field)).isValid();
    // The affordance is the underline and a small glyph: the caption keeps
    // the normal label colour, so it never clashes with a dark theme.
    QFont font = field.label->font();
    font.setUnderline(openable);
    field.label->setFont(font);
    field.label->setText(openable ? field.labelText + QStringLiteral(" ↗") : field.labelText);
    field.label->setCursor(openable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    field.label->setToolTip(openable ? QStringLiteral("Double-click to open in the browser")
                                     : QString());
}

void MetadataPanel::openFieldUrl(const Field &field)
{
    const QUrl url = openableWebUrl(editorText(field));
    if (!url.isValid())
        return;
    if (!QDesktopServices::openUrl(url)) {
        if (QWidget *host = window())
            hud::toast(host, QStringLiteral("Couldn't open the link"));
    }
}

bool MetadataPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        for (const Field &field : fields_) {
            if (field.label == watched && field.key == kOriginUrl) {
                openFieldUrl(field);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MetadataPanel::commitDraft()
{
    if (!batchItems_.isEmpty()) {
        commitBatchDraft();
        return;
    }
    if (!item_)
        return;
    const QVector<QPair<QString, QString>> changes = draftEdits();
    if (changes.isEmpty()) {
        // Nothing to store (an edit that normalization made equal, say a
        // trailing space): put the canonical text back rather than pushing
        // an empty undo step.
        populate(item_);
        return;
    }
    markSuggestionsDirty();

    const doc::ChangeItemCommand::State before =
        doc::ChangeItemCommand::State::capture(*item_->item());
    for (const auto &change : changes) {
        Field *field = fieldFor(change.first);
        applyMetadataChange(item_->item()->filename, item_->item()->meta, change.first,
                            change.second, field && field->integer);
    }
    if (stack_) {
        stack_->push(std::make_unique<doc::ChangeItemCommand>(
            item_->item(), before, doc::ChangeItemCommand::State::capture(*item_->item()),
            QStringLiteral("Edit metadata")));
    }
    if (scene_ && scene_->document())
        scene_->document()->setModified(true);

    // Update the baseline before the stack callbacks refresh the panel,
    // so the same edit is not committed twice.
    populating_ = true;
    saveButton_->setEnabled(false);
    titleLabel_->setText(QStringLiteral("Image"));
    populating_ = false;
    emit modified();
}

// The batch commit: each selected image gets the fields that were
// edited, all of it one undo step.
void MetadataPanel::commitBatchDraft()
{
    const QVector<SceneItem *> items = batchItems_;
    const QVector<QPair<QString, QString>> changes = draftEdits();
    if (changes.isEmpty()) {
        populateBatch(items);
        return;
    }
    markSuggestionsDirty();

    if (stack_)
        stack_->beginMacro(QStringLiteral("Edit metadata"));
    for (SceneItem *view : items) {
        const doc::ItemPtr item = view->item();
        const doc::ChangeItemCommand::State before =
            doc::ChangeItemCommand::State::capture(*item);
        doc::ChangeItemCommand::State after = before;
        for (const auto &change : changes) {
            Field *field = fieldFor(change.first);
            applyMetadataChange(after.filename, after.meta, change.first, change.second,
                                field && field->integer);
        }
        if (after == before)
            continue;
        if (stack_) {
            stack_->push(std::make_unique<doc::ChangeItemCommand>(
                item, before, after, QStringLiteral("Edit metadata")));
        } else {
            after.apply(*item);
        }
    }
    if (stack_)
        stack_->endMacro();
    if (scene_ && scene_->document())
        scene_->document()->setModified(true);

    // Back to a clean draft: the fields now hold the applied values.
    populateBatch(items);
    emit modified();
}

} // namespace ui
