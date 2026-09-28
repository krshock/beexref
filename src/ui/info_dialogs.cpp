#include "info_dialogs.h"

#include "constants.h"
#include "settings.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ui {

// --- AboutDialog ------------------------------------------------------

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About %1").arg(QString::fromLatin1(constants::AppName)));

    auto *layout = new QVBoxLayout(this);
    // The app logo above the text, like the welcome card's.
    auto *logo = new QLabel(this);
    logo->setObjectName(QStringLiteral("aboutLogo"));
    logo->setPixmap(QIcon(QStringLiteral(":/assets/logo.png")).pixmap(64, 64));
    logo->setAlignment(Qt::AlignHCenter);
    layout->addWidget(logo);
    auto *label = new QLabel(this);
    label->setObjectName(QStringLiteral("aboutText"));
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    // The licence name links to the full text.
    label->setOpenExternalLinks(true);
    const QString notice =
        QStringLiteral("%1 is free software under the <a href=\"%2\">%3</a> or later. "
                       "You may use, study, share and improve it; no warranty.")
            .arg(QString::fromLatin1(constants::AppName),
                 QString::fromLatin1(constants::LicenseUrl),
                 QString::fromLatin1(constants::LicenseName));
    label->setText(QStringLiteral("<h2>%1 %2</h2><p>%3</p><p>%4</p><p>%5</p><p>%6</p>")
                       .arg(QString::fromLatin1(constants::AppName),
                            QString::fromLatin1(constants::Version),
                            QString::fromLatin1(constants::AppNameFull),
                            QString::fromUtf8(constants::Copyright),
                            QString::fromUtf8(constants::ForkCopyright), notice));
    layout->addWidget(label);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    // The wrapped rich text lets the layout collapse to its narrowest
    // line, so the box needs a size of its own.
    resize(340, 350);
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

} // namespace ui
