#include "menu_layout.h"

namespace ui {
namespace {

MenuEntry action(const char *id)
{
    MenuEntry entry;
    entry.id = QString::fromLatin1(id);
    return entry;
}

MenuEntry separator()
{
    return {};
}

MenuEntry submenu(const char *title, const QVector<QString> &ids)
{
    MenuEntry entry;
    entry.submenuTitle = QString::fromLatin1(title);
    entry.submenuIds = ids;
    return entry;
}

MenuEntry recentFiles()
{
    MenuEntry entry;
    entry.submenuTitle = QStringLiteral("Open &Recent");
    entry.recent = true;
    return entry;
}

MenuEntry grayscaleMethodMenu()
{
    MenuEntry entry;
    entry.submenuTitle = QStringLiteral("Grayscale &Method");
    entry.grayscaleMethods = true;
    return entry;
}

} // namespace

const QVector<MenuDef> &menuLayout()
{
    static const QVector<MenuDef> layout = {
        {QStringLiteral("&File"),
         {
             action("new_scene"),
             action("new_window"),
             action("open"),
             recentFiles(),
             separator(),
             action("save"),
             action("save_as"),
             submenu("&Export",
                     {QStringLiteral("export_bee"), QStringLiteral("export_scene"),
                      QStringLiteral("export_images")}),
             action("compact_board"),
             separator(),
             action("quit"),
         }},
        {QStringLiteral("&Edit"),
         {
             action("undo"),
             action("redo"),
             separator(),
             action("select_all"),
             action("deselect_all"),
             separator(),
             action("cut"),
             action("copy"),
             action("paste"),
             action("delete"),
             separator(),
             action("raise_to_top"),
             action("lower_to_bottom"),
             action("spotlight_item"),
         }},
        {QStringLiteral("&View"),
         {
             action("fit_scene"),
             action("fit_selection"),
             separator(),
             action("fullscreen"),
             action("always_on_top"),
             action("show_scrollbars"),
             action("show_menubar"),
             action("show_status"),
             action("show_titlebar"),
             action("smooth_images"),
             separator(),
             action("move_window"),
         }},
        {QStringLiteral("&Insert"),
         {
             action("insert_images"),
             action("insert_text"),
         }},
        {QStringLiteral("&Transform"),
         {
             action("crop"),
             action("flip_horizontally"),
             action("flip_vertically"),
             separator(),
             action("reset_scale"),
             action("reset_rotation"),
             action("reset_flip"),
             action("reset_crop"),
             action("reset_transforms"),
         }},
        {QStringLiteral("&Normalize"),
         {
             action("normalize_height"),
             action("normalize_width"),
             action("normalize_size"),
         }},
        {QStringLiteral("&Arrange"),
         {
             action("arrange_optimal"),
             action("arrange_horizontal"),
             action("arrange_vertical"),
             action("arrange_square"),
             separator(),
             action("arrange_default"),
         }},
        {QStringLiteral("&Images"),
         {
             action("change_opacity"),
             action("grayscale"),
             grayscaleMethodMenu(),
             separator(),
             action("show_color_gamut"),
             action("sample_color"),
         }},
        {QStringLiteral("&Settings"),
         {
             action("settings"),
             action("keyboard_settings"),
             action("open_settings_dir"),
         }},
        {QStringLiteral("&Help"),
         {
             action("help"),
             action("about"),
             action("debuglog"),
         }},
    };
    return layout;
}

} // namespace ui
