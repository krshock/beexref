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
    QString label;
};

const QVector<FieldSpec> &fieldSpecs()
{
    static const QVector<FieldSpec> specs = {
        {kName, FieldKind::Text, kNameLimit, false, QStringLiteral("Name")},
        {kAuthor, FieldKind::Text, kAuthorLimit, true, QStringLiteral("Author")},
        {kYear, FieldKind::Integer, kYearLimit, false, QStringLiteral("Year")},
        {kCollection, FieldKind::Text, kCollectionLimit, true, QStringLiteral("Collection")},
        {kOriginUrl, FieldKind::Text, kUrlLimit, false, QStringLiteral("URL")},
        {kNotes, FieldKind::Multiline, kNotesLimit, false, QStringLiteral("Notes")},
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
            if (item_)
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
    const bool one =
        selected.size() == 1 && selected.first()->isPixmap() && !selected.first()->isError();

    // A manual panel with no item yet is the one just opened.
    bool manual = false;
    if (manual_)
        manual = item_ == nullptr || (one && item_ == selected.first());

    if (one && (keep_ || manual)) {
        if (item_ && item_ != selected.first())
            commitDraft();
        setPanelVisible(true);
        populate(selected.first());
        return;
    }

    // Any other selection commits the draft and closes the panel, as
    // the Go port's one-shot panel does.
    commitDraft();
    item_ = nullptr;
    manual_ = false;
    setPanelVisible(false);
}

void MetadataPanel::closePanel()
{
    item_ = nullptr;
    manual_ = false;
    saveButton_->setEnabled(false);
    titleLabel_->setText(QStringLiteral("Item"));
    setPanelVisible(false);
}

void MetadataPanel::forgetItem(SceneItem *view)
{
    if (item_ == view)
        closePanel();
}

bool MetadataPanel::isDirty() const
{
    return item_ != nullptr && !draftEdits().isEmpty();
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
    rebuildInfoRows(item);
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
    if (!item_)
        return changes;
    // The values are normalized here, so what is compared (and stored) is
    // the canonical form. The integer fields keep their text as typed
    // (only trimmed) and are parsed when they are written.
    for (const Field &field : fields_) {
        const QString edited = field.integer
            ? editorText(field).trimmed()
            : normalizedFieldText(editorText(field), !field.multiline);
        if (edited != storedValue(field))
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
    if (!item_)
        return {};
    const doc::ItemPtr item = item_->item();
    if (field.key == kName)
        return item->filename;
    const QJsonValue value = item->meta.value(field.key);
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
    if (populating_ || !item_)
        return;
    const bool dirty = isDirty();
    saveButton_->setEnabled(dirty);
    titleLabel_->setText(dirty ? QStringLiteral("Image \u2022") : QStringLiteral("Image"));
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
        if (field && field->integer) {
            // An empty year removes the key; a half-typed leftover ("-")
            // stores nothing.
            if (change.second.isEmpty()) {
                item_->item()->meta.remove(change.first);
                continue;
            }
            bool ok = false;
            const int year = change.second.toInt(&ok);
            if (!ok)
                continue;
            item_->item()->meta.insert(change.first, QJsonValue(year));
        } else if (change.first == kName) {
            item_->item()->filename = change.second;
        } else {
            item_->item()->meta.insert(change.first, change.second);
        }
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

} // namespace ui
