#pragma once

#include <QObject>
#include <qmmp/qmmp.h>

class DiscordIPC;
class QNetworkAccessManager;

class DiscordRichPresence : public QObject
{
    Q_OBJECT
public:
    explicit DiscordRichPresence(QObject *parent = nullptr);
    ~DiscordRichPresence();

    void reload();

private slots:
    void onTrackInfoChanged();
    void onStateChanged(Qmmp::State state);

private:
    void updatePresence();
    void fetchCoverArt(const QString &artist, const QString &album, const QString &title);

    DiscordIPC             *m_ipc;
    QNetworkAccessManager  *m_nam;
    Qmmp::State             m_state = Qmmp::Stopped;
    QString                 m_coverUrl;       // cached art URL for current track
    QString                 m_lastCoverQuery; // prevents duplicate requests
};
