#include "theme.h"

#include "settings.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace ui::theme {
namespace {

bool captured = false;
QString startupStyle;
QPalette startupPalette;

// The two window palettes. Both keep the accent the HUD uses, so
// selections and links read the same in either mode.
QPalette darkPalette()
{
    const QColor window(43, 43, 43);
    const QColor base(30, 30, 30);
    const QColor button(58, 58, 58);
    const QColor text(230, 230, 230);
    const QColor disabled(128, 128, 128);
    const QColor accent(90, 181, 179);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, disabled);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, QColor(255, 90, 90));
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::LinkVisited, accent);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, QColor(20, 20, 20));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(70, 70, 70));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return palette;
}

QPalette lightPalette()
{
    const QColor window(240, 240, 240);
    const QColor base(255, 255, 255);
    const QColor button(228, 228, 228);
    const QColor text(30, 30, 30);
    const QColor disabled(150, 150, 150);
    const QColor accent(60, 150, 148);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, disabled);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, QColor(200, 40, 40));
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::LinkVisited, accent);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(190, 190, 190));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return palette;
}

} // namespace

Mode modeForSetting(const QString &setting)
{
    if (setting == QLatin1String("dark"))
        return Mode::Dark;
    if (setting == QLatin1String("light"))
        return Mode::Light;
    return Mode::System;
}

Mode resolve(Mode mode, Qt::ColorScheme scheme)
{
    if (mode != Mode::System)
        return mode;
    if (scheme == Qt::ColorScheme::Light)
        return Mode::Light;
    // Dark, and the Unknown a platform without a theme reports: the
    // canvas and the HUD are dark by design, so the window is too.
    return Mode::Dark;
}

void captureStartup()
{
    if (captured)
        return;
    captured = true;
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return;
    startupStyle = app->style()->objectName();
    startupPalette = app->palette();
}

void apply(Mode mode, Qt::ColorScheme scheme)
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return;
    if (mode == Mode::System && scheme != Qt::ColorScheme::Unknown) {
        // The platform theme already paints the desktop's own colours:
        // put back the style and palette the application started with.
        if (!captured)
            return;
        if (QStyle *style = QStyleFactory::create(startupStyle))
            app->setStyle(style);
        app->setPalette(startupPalette);
        return;
    }
    const Mode look = resolve(mode, scheme);
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app->setStyle(fusion);
    app->setPalette(look == Mode::Dark ? darkPalette() : lightPalette());
}

void applyFromSettings()
{
    captureStartup();
    settings::File file(settings::iniPath());
    file.load();
    const Mode mode = modeForSetting(
        settings::valueOrDefault(file, QStringLiteral("View/theme")).toString());
    apply(mode, QGuiApplication::styleHints()->colorScheme());
}

} // namespace ui::theme
