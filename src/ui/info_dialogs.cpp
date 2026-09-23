#include "info_dialogs.h"

#include "constants.h"
#include "settings.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ui {

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

} // namespace ui