#include "settingsdialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Discord Rich Presence Settings"));
    setMinimumWidth(400);

    m_appIdEdit = new QLineEdit(this);
    const bool hasDefault = !QStringLiteral(DEFAULT_DISCORD_APP_ID).isEmpty();
    m_appIdEdit->setPlaceholderText(hasDefault
        ? tr("Leave blank to use the built-in Application ID")
        : tr("Required — get yours from discord.com/developers/applications"));

    auto *hint = new QLabel(hasDefault
        ? tr("Override the built-in Discord Application ID (optional). "
             "Only needed if you want Rich Presence to appear under a different application name.")
        : tr("Required. Create a Discord Application at discord.com/developers/applications "
             "to obtain an Application ID."),
        this);
    hint->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *form = new QFormLayout;
    form->addRow(tr("Application ID:"), m_appIdEdit);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    QSettings settings;
    m_appIdEdit->setText(
        settings.value(QStringLiteral("DiscordRichPresence/app_id")).toString());
}

void SettingsDialog::accept()
{
    QSettings settings;
    settings.setValue(QStringLiteral("DiscordRichPresence/app_id"),
                      m_appIdEdit->text().trimmed());
    QDialog::accept();
}
