#include "discordrichpresencefactory.h"
#include "discordrichpresence.h"
#include "settingsdialog.h"

#include <QMessageBox>

GeneralProperties DiscordRichPresenceFactory::properties() const
{
    GeneralProperties p;
    p.name        = tr("Discord Rich Presence");
    p.shortName   = QStringLiteral("discordrichpresence");
    p.hasAbout    = true;
    p.hasSettings = true;
    p.visibilityControl = false;
    return p;
}

QObject *DiscordRichPresenceFactory::create(QObject *parent)
{
    return new DiscordRichPresence(parent);
}

QDialog *DiscordRichPresenceFactory::createSettings(QWidget *parent)
{
    return new SettingsDialog(parent);
}

void DiscordRichPresenceFactory::showAbout(QWidget *parent)
{
    QMessageBox::about(parent,
        tr("About Discord Rich Presence Plugin"),
        tr("Discord Rich Presence Plugin for QMMP\n"
           "Displays currently playing music in Discord and Vesktop.\n\n"
           "Works out of the box with no configuration required.\n"
           "A custom Application ID can be set in the plugin settings."));
}

QString DiscordRichPresenceFactory::translation() const
{
    return {};
}
