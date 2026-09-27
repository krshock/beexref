# Extending BeeXRef

BeeXRef keeps its extension points in small data tables and registries,
so most features are one entry plus the code that entry points at. This
guide lists them in the order you are most likely to need them.

The rules that apply to every change here:

- The `.beex` format is frozen: new item data goes into the existing
  `data`/`meta` JSON columns, never into new tables or columns without
  the other ports.
- Match the existing behaviour, quirks included; deliberate deviations
  are called out in comments and commit messages. Upstream BeeRef only
  has the `.bee` format and none of this port's extras, so public
  wording describes the extras as this port's own.
- Every change runs `ctest --preset linux-debug` (the whole suite) and
  the offscreen UI smoke before it is committed.

## A tool (an interactive canvas mode)

Tools are the app's "active modes": crop, colour sampling, text
editing, move-window, pan and drag-zoom.

1. Implement `ui::Tool` (`src/ui/tool.h`): `id()`, `active()`,
   `cancel()`, and any of `mousePress`/`mouseMove`/`mouseRelease`/
   `keyPress` you need. Return `true` for events you consumed.
2. Register it in `View::View()` (`src/ui/view.cpp`):

   ```cpp
   myTool_ = tools_->add<MyTool>(this);
   ```

   `tools_->add` takes ownership; keep the borrowed pointer as a member.
3. Add a `View::startMyTool()` that fills in the tool's session state
   (the item, the press position, ...). `View::cancelModes()` cancels
   whatever is active, so every existing call site (board open, undo,
   Escape) already covers your tool.
4. Wire an action to it: `MainWindow::buildActions()` and one id in
   `src/ui/menu_layout.cpp` (see "A command" below).
5. If the tool holds a `SceneItem *`, add a `cancelIfItem(view)` call to
   the view's `Scene::itemViewAboutToBeRemoved` handler (see
   `cropTool_`/`textEditTool_`), and release any `grabMouse`/cursor
   override in `cancel()`.

Tests: add a Qt Test slot that drives the tool with `QMouseEvent`s (see
`test_ui_scene.cpp`, e.g. `cropModeDragsAndConfirms`).

## An item type

Document items are generic: a type string plus JSON `data`/`meta`, so
the file format and `doc::Item` need no changes for a new type.

1. Add the type name in `src/doc/item.cpp` (`kType...`, `createItem`,
   `isKnownType`). Unknown types still load, round-trip and render as
   `Unsupported item type: <type>`, but they are skipped by exporters.
2. Add a row to the traits table in `src/ui/item_types.cpp`
   (`ui::item_types::forType`): content `bounds`, `paint`, whether
   `crops`/`samples` apply, and the SVG `svgElement` (`"image"`,
   `"text"` or empty to skip).
3. Implement the handler members on `SceneItem`
   (`src/ui/scene_item.{h,cpp}`), next to `boundsPixmap()` and
   `paintPixmap()`. Item-local coordinates are original-image pixels.
4. Put anything type-specific into `Item::data` (persisted) or
   `Item::meta` (export metadata), and expose it through accessors so
   commands can capture and restore it in their `State`.

Tests: `test_doc_item.cpp` for the model, `test_ui_scene.cpp` for the
painting, and `beexref-boardcheck` to prove the file still round-trips.

## An insertion handler (a new drop/paste payload)

`InputController::addInsertHandler()` (`src/ui/input_controller.h`)
claims mime formats ahead of the built-in file/URL/image/text
classification:

```cpp
input->addInsertHandler(
    {QStringList{QStringLiteral("application/x-my-format")},
     [input](const QMimeData &data, const QPointF &pos, double scale) {
         doc::ItemPtr item = doc::createItem(doc::kTypeText);
         item->setText(QString::fromUtf8(data.data(QStringLiteral("application/x-my-format"))));
         input->insertItems({item}, pos, QStringLiteral("Insert my thing"));
         return true;
     },
     QStringLiteral("Could not read the payload")});
```

- `formats` is checked with `hasFormat()` only; drag-enter must never
  read payloads (it can poison the drop-time read on some sources).
- Handlers run before the built-in routes, for drops and pastes (paste
  keeps its internal-items rule first). A handler that returns `false`
  reports `failure` and stops the dispatch.
- `insertItems()` adds the items as one undo step and selects them; it
  is the supported way to put items on the board from outside.

Test: `test_input.cpp`, `customInsertHandlerClaimsItsFormat`.

## A scene export format

`src/ui/export_formats.cpp` holds the registry; the file dialog filter,
the suffix lookup and the size-dialog decision all follow from it:

```cpp
{QStringLiteral("webp"), {QStringLiteral("webp")}, QStringLiteral("WEBP"), true,
 [](QGraphicsScene &scene, const SceneExportFrame &frame, const QSize &size,
    const QString &path) -> QString {
     const QImage image = renderSceneToImage(scene, frame, size, theme::canvas);
     if (!image.save(path, "WEBP", kSceneExportQuality))
         return QStringLiteral("Error writing file");
     return {}; // empty: success
 }},
```

`asksSize` selects the export size dialog for raster
formats. An unknown suffix falls back to the first entry, whose writer
lets `QImage` pick the encoding from the file name.

Test: `test_ui_scene.cpp`, `sceneExportFormatsDriveTheSuffixes`, plus an
end-to-end export of the new suffix.

## An LOD method

`src/ui/levels.cpp` registers the methods behind
`Items/lod_method`: the settings value, whether the method builds the
fraction ladder, and whether its decoded-bytes budget comes from
`Items/lod_ram_budget_mb`. An id that is not registered keeps the
single-level behaviour.

1. Add a `LevelMethod` entry in `levelMethods()`.
2. Add the matching radio option (value, label, tooltip) to the
   `Items/lod_method` field in `src/ui/settings_dialog.cpp`.
3. If the method needs a different ladder shape, extend `buildLevels()`;
   the loader, manager and hints treat levels generically.

Test: `test_levels.cpp` (`methodsAreSettingsValuesWithAFallback`, plus
the ladder/budget slots).

## A setting

Settings are one table in `src/ui/settings_dialog.cpp`
(`fieldTable()`), placed on a tab by key:

```cpp
integerField(QStringLiteral("Items/my_limit"), QStringLiteral("My Limit:"),
             QStringLiteral("What it does, shown as the tooltip."), 1, 100),
```

`radioField`, `checkboxField` and `makeField` cover the other kinds.
Read the value where it is used, not at startup, with
`settings::valueOrDefault(file, key)` after `file.load()` — settings are
read at use time. Defaults live in the field table and in
`src/settings.cpp`.

Tests must never touch the real `~/.config/BeeXRef`: call
`testenv::isolate()` or point `settings::setSettingsDir()` at a
`QTemporaryDir` (see `tests/test_settings.cpp`).

## A command (menu action)

1. Register the action in `MainWindow::buildActions()`:

   ```cpp
   actions_->add(QStringLiteral("my_command"), QStringLiteral("My &Command"),
                 QKeySequence(QStringLiteral("Ctrl+Shift+M")), G::Selection,
                 [this](bool) { myCommand(); });
   ```

   The group (`Always`, `ItemsInScene`, `Selection`, `SingleImage`,
   `CanUndo`, `CanRedo`) decides when the action is enabled;
   `updateActions()` computes the state from the scene.
2. Put the id in the menu table in `src/ui/menu_layout.cpp`. The
   coverage test (`menuLayoutCoversEveryAction`) fails if a registered
   action is not in exactly one menu, and the canvas context menu is
   copied from the menu bar, so both follow automatically.
3. The Keyboard & Mouse editor picks the action up from the registry,
   so its shortcut is remappable without extra work.
4. If the command changes items, push a `doc::UndoStack` command. The
   commands mark their item dirty on redo/undo, so `Scene::syncDocument()`
   refreshes only what changed. Direct mutations (not through a command)
   must call `document->noteItemChanged(item)` before syncing.

Test: a `test_ui_scene.cpp` slot that triggers the action and checks the
document, the undo stack and the scene.

## Board formats

`.beex` is written by `src/board/write.cpp` (streaming, temp file plus
rename) and read through `src/board/sqlite.cpp`; `.bee` is the legacy
upstream interchange shape, import-only, and its schema lives in
`src/board/schema.cpp`. There is no format registry: a new board format
would hook into `board::Board::open()`/`doc::Document::open()` and
`doc::Document::save()`. The shapes are defined by `fileio/legacy.py`,
`fileio/sql.py` and `fileio/export.py` (in the `beerefx` checkout); keep
byte-compatibility and verify with
`beexref-boardcheck [--write-back PATH] <board>...`, which migrates on a
copy, decodes every blob, checks dimensions and diffs all tables.

## Before committing

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --preset linux-debug
QT_QPA_PLATFORM=offscreen ./build/linux-debug/beexref-ui-smoke [board] /tmp/opencode/ui-smoke
cmake --build --preset linux-release
```

Format with `.clang-format` (LLVM base, Allman braces, 4 spaces, 100
columns). Never commit without the maintainer's approval, and never tag
a release from a dirty tree (see `AGENTS.md`).
