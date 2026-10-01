#pragma once

#include "doc/undo.h"
#include "fuzzy_authors.h"

#include <QUrl>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QCompleter;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStringListModel;
class QTabWidget;
class QVBoxLayout;

namespace ui {

class Scene;
class SceneItem;

// The Go port's right-hand metadata panel: a docked side panel with an
// Info tab (read-only item rows) and a Meta tab (a draft of name,
// author, year, collection, URL and notes), plus a Keep/Close/Save
// footer.
//
// Without Keep it is a one-shot: editing another single image commits
// the draft and closes it, while a batch keeps editing whatever images
// are selected (so a selection can be grown one image at a time). With
// Keep it follows the selection and persists as View/panel_keep.
//
// Several selected images turn the Meta tab into a batch editor: only
// the shared fields (Author, Year, Collection) show, a field every
// image shares starts at that value, a mixed one starts empty and says
// "(multiple)", and Save writes what was edited to every selected image
// in one undo step. The fields left untouched are left alone.
class MetadataPanel : public QWidget
{
    Q_OBJECT

public:
    MetadataPanel(Scene *scene, doc::UndoStack *stack, QWidget *parent = nullptr);

    void setScene(Scene *scene);

    // Opens for the single selected image, or closes (discarding the
    // draft) when already open.
    void toggle();
    // Follows the selection, like the Go port's refreshPanel.
    void refresh();
    // Hides the panel, dropping any uncommitted draft.
    void closePanel();
    // Called when the scene is about to delete a view.
    void forgetItem(SceneItem *view);

    bool keep() const { return keep_; }
    void setKeep(bool keep);
    SceneItem *item() const { return item_; }
    bool isDirty() const;

    // The minimum width of the right-hand slot, and the last width the
    // panel had (kept while hidden, runtime only).
    static constexpr int kMinWidth = 160;
    int preferredWidth() const { return preferredWidth_; }

signals:
    // A draft was committed (the document changed).
    void modified();
    // The panel was shown or hidden.
    void visibilityChanged();

private:
    // One editable metadata field. The specs in the .cpp drive creation,
    // populate, comparison, commit and, where asked, the fuzzy
    // suggestions, so a new field is one row plus its constant.
    struct Field
    {
        QString key;
        bool integer = false;     // a JSON number; empty removes the key
        bool multiline = false;   // a QPlainTextEdit instead of a QLineEdit
        int limit = 0;            // editing cap in characters
        bool suggestions = false; // fuzzy completion from the board's values
        bool batch = false;       // offered when several images are edited together
        QString baseline;         // batch: the value the draft compares against
        bool touched = false;     // batch: the editor was edited by hand
        QLabel *label = nullptr;
        QString labelText;      // the caption's text, without the link glyph
        QWidget *editor = nullptr;
        QCompleter *completer = nullptr;
        QStringListModel *model = nullptr;
        QVector<fuzzy::Candidate> candidates;
        bool candidatesDirty = true;
    };

    void buildMetaFields(QVBoxLayout *layout);
    void wireField(Field &field);
    void setupSuggestions(Field &field);
    void showSuggestions(Field &field, const QString &text);
    void markSuggestionsDirty();
    void clampField(Field &field);
    QString editorText(const Field &field) const;
    void setEditorText(const Field &field, const QString &text);
    QString storedValue(const Field &field) const;
    Field *fieldFor(const QString &key);

    void commitDraft();
    void commitBatchDraft();
    void populate(SceneItem *item);
    void populateBatch(const QVector<SceneItem *> &items);
    void applyFieldVisibility();
    QString baseTitle() const;
    QString itemValue(const doc::Item &item, const Field &field) const;
    void rebuildInfoRows(SceneItem *item);
    QVector<QPair<QString, QString>> draftEdits() const;
    void updateDirty();
    void setPanelVisible(bool visible);
    // The URL field: the caption turns into a link and a double-click on
    // it opens the field's web address.
    void updateUrlAffordance(Field &field);
    void openFieldUrl(const Field &field);

protected:
    // The URL caption's double-click (a QLabel has no signal for it).
    bool eventFilter(QObject *watched, QEvent *event) override;

    Scene *scene_ = nullptr;
    doc::UndoStack *stack_ = nullptr;
    SceneItem *item_ = nullptr;
    // The images a batch edit covers; empty while the panel is on one
    // image (or closed).
    QVector<SceneItem *> batchItems_;
    bool keep_ = false;
    bool manual_ = false;
    bool populating_ = false;
    int preferredWidth_ = 320;

    QLabel *titleLabel_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QVBoxLayout *metaLayout_ = nullptr;
    QWidget *infoPage_ = nullptr;
    QFormLayout *infoForm_ = nullptr;
    QVector<Field> fields_;
    QCheckBox *keepBox_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QPushButton *saveButton_ = nullptr;
};

// The Go port's read-only Info rows (Source, Size, Format, Position,
// Z-order, Scale, Rotation, Flipped, Opacity, Grayscale, Crop, Save ID).
QVector<QPair<QString, QString>> itemInfoRows(const SceneItem *view);

// The URL a metadata field can open: only the schemes this app is willing
// to hand to the desktop are accepted (http and https for now); anything
// else -- empty text, a missing or unknown scheme, file://, javascript: --
// yields an invalid QUrl. One place to widen later.
QUrl openableWebUrl(const QString &text);

} // namespace ui
