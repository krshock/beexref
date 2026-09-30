#pragma once

#include "doc/undo.h"
#include "fuzzy_authors.h"

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

namespace ui {

class Scene;
class SceneItem;

// The Go port's right-hand metadata panel: a docked side panel with an
// Info tab (read-only item rows) and a Meta tab (a draft of name,
// author, URL and notes), plus a Keep/Close/Save footer.
//
// Without Keep it is a one-shot for the image it was opened on: any
// other selection commits the draft and closes it. With Keep it follows
// the selection (single image only) and persists as View/panel_keep.
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
    void commitDraft();
    void populate(SceneItem *item);
    void rebuildInfoRows(SceneItem *item);
    QVector<QPair<QString, QString>> draftEdits() const;
    QString currentValue(const QString &field) const;
    void updateDirty();
    void setPanelVisible(bool visible);
    // The author field's suggestions: the unique authors already used in
    // the board, filtered by a fuzzy (diacritic-insensitive) search.
    void showAuthorCompletions(const QString &text);

    Scene *scene_ = nullptr;
    doc::UndoStack *stack_ = nullptr;
    SceneItem *item_ = nullptr;
    bool keep_ = false;
    bool manual_ = false;
    bool populating_ = false;
    int preferredWidth_ = 320;

    QLabel *titleLabel_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QWidget *infoPage_ = nullptr;
    QFormLayout *infoForm_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *authorEdit_ = nullptr;
    QLineEdit *urlEdit_ = nullptr;
    QPlainTextEdit *notesEdit_ = nullptr;
    // The author suggestions are cached per board (rebuilt when the
    // document changes, never per selection or per keystroke).
    QCompleter *authorCompleter_ = nullptr;
    QStringListModel *authorModel_ = nullptr;
    QVector<fuzzy::Candidate> authorCandidates_;
    bool authorCandidatesDirty_ = true;
    QCheckBox *keepBox_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QPushButton *saveButton_ = nullptr;
};

// The Go port's read-only Info rows (Source, Size, Format, Position,
// Z-order, Scale, Rotation, Flipped, Opacity, Grayscale, Crop, Save ID).
QVector<QPair<QString, QString>> itemInfoRows(const SceneItem *view);

} // namespace ui
