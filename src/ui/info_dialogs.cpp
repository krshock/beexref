#include "info_dialogs.h"

#include "constants.h"
#include "scene_item.h"
#include "settings.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ui {
namespace {

QString number(double value, int precision)
{
    return QString::number(value, 'f', precision);
}

QString dataString(const doc::Item &item, const QString &key)
{
    return item.data.value(key).toString();
}

} // namespace

QVector<QPair<QString, QString>> itemMetadata(const SceneItem *view)
{
    QVector<QPair<QString, QString>> rows;
    if (!view)
        return rows;
    const doc::Item &item = *view->item();

    const QString source =
        item.filename.isEmpty() ? dataString(item, QStringLiteral("filename")) : item.filename;
    rows.append({QStringLiteral("Source"), source});
    rows.append({QStringLiteral("Origin URL"),
                 item.meta.value(QStringLiteral("origin_url")).toString()});
    rows.append({QStringLiteral("Notes"), item.meta.value(QStringLiteral("notes")).toString()});

    if (item.isPixmap()) {
        QSize size = item.originalSize();
        if (!size.isValid() || size.isEmpty())
            size = view->boundingRect().size().toSize();
        rows.append({QStringLiteral("Size"),
                     QStringLiteral("%1 x %2").arg(size.width()).arg(size.height())});
    } else {
        const QRectF bounds = view->boundingRect();
        rows.append({QStringLiteral("Size"), QStringLiteral("%1 x %2")
                                                 .arg(bounds.width(), 0, 'f', 0)
                                                 .arg(bounds.height(), 0, 'f', 0)});
    }

    rows.append({QStringLiteral("Position"),
                 QStringLiteral("%1, %2").arg(number(item.x, 1), number(item.y, 1))});
    rows.append({QStringLiteral("Z-order"), number(item.z, 1)});
    rows.append({QStringLiteral("Scale"), number(item.scale, 2)});
    rows.append({QStringLiteral("Rotation"), QStringLiteral("%1°").arg(number(item.rotation, 1))});
    rows.append({QStringLiteral("Flipped"), item.flip < 0 ? QStringLiteral("Yes")
                                                          : QStringLiteral("No")});
    rows.append({QStringLiteral("Opacity"),
                 QStringLiteral("%1%").arg(number(item.opacity() * 100.0, 0))});
    if (item.isPixmap()) {
        rows.append({QStringLiteral("Grayscale"), item.grayscale() ? QStringLiteral("Yes")
                                                                   : QStringLiteral("No")});
        const QRectF crop = item.hasCrop() ? item.crop() : view->boundingRect();
        rows.append({QStringLiteral("Crop"),
                     QStringLiteral("%1, %2, %3 x %4")
                         .arg(number(crop.x(), 1), number(crop.y(), 1),
                              number(crop.width(), 1))
                         .arg(number(crop.height(), 1))});
    }
    rows.append({QStringLiteral("Save ID"),
                 item.id != 0 ? QString::number(item.id) : QStringLiteral("Not saved")});
    return rows;
}

// --- HelpDialog -------------------------------------------------------

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("%1 Help").arg(QString::fromLatin1(constants::AppName)));

    auto *tabs = new QTabWidget(this);
    auto *page = new QWidget(tabs);
    auto *pageLayout = new QVBoxLayout(page);

    auto *label = new QLabel(page);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFile html(QStringLiteral(":/assets/documentation/controls.html"));
    if (html.open(QIODevice::ReadOnly | QIODevice::Text))
        label->setText(QString::fromUtf8(html.readAll()));
    html.close();

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setWidget(label);
    pageLayout->addWidget(scroll);
    tabs->addTab(page, QStringLiteral("&Controls"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(640, 520);
}

// --- DebugLogDialog ---------------------------------------------------

DebugLogDialog::DebugLogDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("%1 Debug Log").arg(QString::fromLatin1(constants::AppName)));

    auto *layout = new QVBoxLayout(this);
    auto *nameWidget = new QLabel(settings::logPath(), this);
    nameWidget->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(nameWidget);

    auto *log = new QPlainTextEdit(this);
    log->setReadOnly(true);
    QFile file(settings::logPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        log->setPlainText(QString::fromUtf8(file.readAll()));
    layout->addWidget(log);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *copyButton = new QPushButton(QStringLiteral("Co&py To Clipboard"), this);
    copyButton->setAutoDefault(false);
    connect(copyButton, &QPushButton::clicked, this,
            [log]() { QGuiApplication::clipboard()->setText(log->toPlainText()); });
    buttons->addButton(copyButton, QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    resize(720, 520);
}

// --- ImageInfoDialog --------------------------------------------------

ImageInfoDialog::ImageInfoDialog(QWidget *parent, const SceneItem *view)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("%1 Image Info").arg(QString::fromLatin1(constants::AppName)));
    setWindowModality(Qt::WindowModal);
    setMinimumWidth(600);

    auto *layout = new QVBoxLayout(this);
    auto *grid = new QGridLayout();
    layout->addLayout(grid);

    int row = 0;
    for (const auto &entry : itemMetadata(view)) {
        auto *name = new QLabel(QStringLiteral("%1:").arg(entry.first), this);
        grid->addWidget(name, row, 0, Qt::AlignTop);
        if (entry.first == QLatin1String("Source")) {
            auto *value = new QPlainTextEdit(entry.second, this);
            value->setReadOnly(true);
            value->setLineWrapMode(QPlainTextEdit::WidgetWidth);
            value->setMaximumHeight(80);
            grid->addWidget(value, row, 1);
        } else {
            auto *value = new QLabel(entry.second, this);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setWordWrap(true);
            grid->addWidget(value, row, 1);
        }
        ++row;
    }
    grid->setColumnStretch(1, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace ui
