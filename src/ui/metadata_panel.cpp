#include "metadata_panel.h"

#include "scene.h"
#include "scene_item.h"
#include "selection_ops.h"
#include "settings.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCompleter>
#include <QFormLayout>
#include <QHBoxLayout>
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
const QString kOriginUrl = QStringLiteral("origin_url");
const QString kNotes = QStringLiteral("notes");

// How many author suggestions the popup shows.
constexpr int kAuthorCompletionLimit = 15;

// The edit limits of the metadata fields. They clamp what is typed or
// pasted, not what a board already holds: populate() writes the stored
// values with the signals blocked and QLineEdit::maxLength does not
// truncate setText, so opening and saving a board never rewrites a longer
// value it did not edit. Name matches the usual filesystem limit, author
// keeps the completion folding cheap and the OSD readable, URL matches
// the browsers' practical limit, and notes stay a couple of pages (they
// live in the items' JSON, so 3000 of them are still small).
constexpr int kNameLimit = 255;
constexpr int kAuthorLimit = 128;
constexpr int kUrlLimit = 2048;
constexpr int kNotesLimit = 4096;

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
    nameEdit_ = new QLineEdit(metaPage);
    nameEdit_->setObjectName(QStringLiteral("panelName"));
    nameEdit_->setPlaceholderText(QStringLiteral("Name"));
    authorEdit_ = new QLineEdit(metaPage);
    authorEdit_->setObjectName(QStringLiteral("panelAuthor"));
    authorEdit_->setPlaceholderText(QStringLiteral("Author"));
    urlEdit_ = new QLineEdit(metaPage);
    urlEdit_->setObjectName(QStringLiteral("panelUrl"));
    urlEdit_->setPlaceholderText(QStringLiteral("URL"));
    notesEdit_ = new QPlainTextEdit(metaPage);
    notesEdit_->setObjectName(QStringLiteral("panelNotes"));
    notesEdit_->setPlaceholderText(QStringLiteral("Notes"));
    metaLayout->addWidget(new QLabel(QStringLiteral("Name"), metaPage));
    metaLayout->addWidget(nameEdit_);
    metaLayout->addWidget(new QLabel(QStringLiteral("Author"), metaPage));
    metaLayout->addWidget(authorEdit_);
    metaLayout->addWidget(new QLabel(QStringLiteral("URL"), metaPage));
    metaLayout->addWidget(urlEdit_);
    metaLayout->addWidget(new QLabel(QStringLiteral("Notes"), metaPage));
    metaLayout->addWidget(notesEdit_, 1);
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

    for (QLineEdit *editor : {nameEdit_, authorEdit_, urlEdit_})
        connect(editor, &QLineEdit::textChanged, this, [this]() { updateDirty(); });
    connect(notesEdit_, &QPlainTextEdit::textChanged, this, [this]() {
        // The notes have no maxLength: clamp an over-long edit here. The
        // stored text is written with the signals blocked, so a longer
        // value from a board is left alone.
        const QString text = notesEdit_->toPlainText();
        if (text.size() > kNotesLimit) {
            const int cursor = notesEdit_->textCursor().position();
            const QSignalBlocker blocker(notesEdit_);
            notesEdit_->setPlainText(text.left(kNotesLimit));
            QTextCursor clamped = notesEdit_->textCursor();
            clamped.setPosition(qMin(cursor, kNotesLimit));
            notesEdit_->setTextCursor(clamped);
        }
        updateDirty();
    });

    // The fields clamp what the user types or pastes (textEdited fires
    // only for user edits), never what a board already holds:
    // QLineEdit::setMaxLength would also truncate populate()'s
    // programmatic setText and rewrite a longer stored value just by
    // opening the panel.
    const auto clampEdit = [this](QLineEdit *editor, int limit) {
        connect(editor, &QLineEdit::textEdited, this,
                [this, editor, limit](const QString &text) {
                    if (text.size() <= limit)
                        return;
                    const int cursor = editor->cursorPosition();
                    {
                        const QSignalBlocker blocker(editor);
                        editor->setText(text.left(limit));
                        editor->setCursorPosition(qMin(cursor, limit));
                    }
                    updateDirty();
                });
    };
    clampEdit(nameEdit_, kNameLimit);
    clampEdit(authorEdit_, kAuthorLimit);
    clampEdit(urlEdit_, kUrlLimit);

    // The author field autocompletes from the authors already used in the
    // board, matched fuzzily and without diacritics; the candidate list is
    // cached and rebuilt only when the document changes, never per
    // selection or keystroke.
    authorModel_ = new QStringListModel(this);
    authorCompleter_ = new QCompleter(authorModel_, this);
    authorCompleter_->setWidget(authorEdit_);
    // Unfiltered: the model already holds the fuzzy matches, and the
    // line edit overwrites the completion prefix with the typed text, so
    // a prefix-filtered popup would drop them (it matches on the display
    // text, accents and all).
    authorCompleter_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    authorCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
    authorEdit_->setCompleter(authorCompleter_);
    connect(authorEdit_, &QLineEdit::textEdited, this,
            [this](const QString &text) { showAuthorCompletions(text); });
    connect(authorCompleter_, QOverload<const QString &>::of(&QCompleter::activated), this,
            [this](const QString &text) { authorEdit_->setText(text); });
    connect(saveButton_, &QPushButton::clicked, this, [this]() { commitDraft(); });
    connect(closeButton_, &QPushButton::clicked, this, [this]() { closePanel(); });
    connect(keepBox_, &QCheckBox::checkStateChanged, this,
            [this](Qt::CheckState state) { setKeep(state == Qt::Checked); });

    // Undo and redo must be reflected in an open panel.
    if (stack_) {
        stack_->addChangedCallback([this]() {
            authorCandidatesDirty_ = true;
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
    authorCandidatesDirty_ = true;
    if (scene_) {
        connect(scene_, &QGraphicsScene::selectionChanged, this, [this]() { refresh(); });
        connect(scene_, &Scene::itemsChanged, this,
                [this]() { authorCandidatesDirty_ = true; });
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
    const QSignalBlocker nameBlocker(nameEdit_);
    const QSignalBlocker authorBlocker(authorEdit_);
    const QSignalBlocker urlBlocker(urlEdit_);
    const QSignalBlocker notesBlocker(notesEdit_);
    nameEdit_->setText(item->item()->filename);
    authorEdit_->setText(item->item()->meta.value(kAuthor).toString());
    urlEdit_->setText(item->item()->meta.value(kOriginUrl).toString());
    notesEdit_->setPlainText(item->item()->meta.value(kNotes).toString());
    populating_ = false;

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
    // the canonical form.
    const QVector<QPair<QString, QString>> texts = {
        {kName, normalizedFieldText(nameEdit_->text(), true)},
        {kAuthor, normalizedFieldText(authorEdit_->text(), true)},
        {kOriginUrl, normalizedFieldText(urlEdit_->text(), true)},
        {kNotes, normalizedFieldText(notesEdit_->toPlainText(), false)},
    };
    for (const auto &entry : texts) {
        if (entry.second != currentValue(entry.first))
            changes.append(entry);
    }
    return changes;
}

QString MetadataPanel::currentValue(const QString &field) const
{
    if (!item_)
        return {};
    if (field == kName)
        return item_->item()->filename;
    return item_->item()->meta.value(field).toString();
}

void MetadataPanel::updateDirty()
{
    if (populating_ || !item_)
        return;
    const bool dirty = isDirty();
    saveButton_->setEnabled(dirty);
    titleLabel_->setText(dirty ? QStringLiteral("Image •") : QStringLiteral("Image"));
}

void MetadataPanel::showAuthorCompletions(const QString &text)
{
    if (!authorCompleter_ || !scene_)
        return;
    if (authorCandidatesDirty_) {
        QStringList raw;
        const QList<SceneItem *> views = scene_->itemViews();
        raw.reserve(views.size());
        for (SceneItem *view : views)
            raw.append(view->item()->meta.value(kAuthor).toString());
        authorCandidates_ = fuzzy::candidates(raw);
        authorCandidatesDirty_ = false;
    }
    const QVector<fuzzy::Candidate> matches =
        fuzzy::search(authorCandidates_, text, kAuthorCompletionLimit);
    QStringList rows;
    rows.reserve(matches.size());
    for (const fuzzy::Candidate &candidate : matches)
        rows.append(candidate.display);
    authorModel_->setStringList(rows);
    if (rows.isEmpty()) {
        authorCompleter_->popup()->hide();
        return;
    }
    // UnfilteredPopupCompletion shows the model as it is: the fuzzy
    // matches, no prefix filtering on top.
    authorCompleter_->complete();
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
    authorCandidatesDirty_ = true;

    const doc::ChangeItemCommand::State before =
        doc::ChangeItemCommand::State::capture(*item_->item());
    for (const auto &change : changes) {
        if (change.first == kName)
            item_->item()->filename = change.second;
        else
            item_->item()->meta.insert(change.first, change.second);
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
