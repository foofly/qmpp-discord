#include "discordrichpresence.h"
#include "discordipc.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

#include <qmmp/soundcore.h>
#include <qmmp/trackinfo.h>

DiscordRichPresence::DiscordRichPresence(QObject *parent)
    : QObject(parent)
    , m_ipc(new DiscordIPC(this))
    , m_nam(new QNetworkAccessManager(this))
{
    SoundCore *core = SoundCore::instance();
    connect(core, &SoundCore::trackInfoChanged, this, &DiscordRichPresence::onTrackInfoChanged);
    connect(core, &SoundCore::stateChanged,     this, &DiscordRichPresence::onStateChanged);

    m_state = core->state();
    reload();
}

DiscordRichPresence::~DiscordRichPresence()
{
    m_ipc->clearActivity();
}

static QString effectiveAppId()
{
    QSettings settings;
    const QString userOverride = settings.value(QStringLiteral("DiscordRichPresence/app_id")).toString();
    if (!userOverride.isEmpty())
        return userOverride;
    return QStringLiteral(DEFAULT_DISCORD_APP_ID);
}

void DiscordRichPresence::reload()
{
    const QString appId = effectiveAppId();
    m_ipc->setAppId(appId);
    if (!appId.isEmpty()) {
        m_ipc->connectToDiscord();
        updatePresence();
    } else {
        m_ipc->clearActivity();
    }
}

void DiscordRichPresence::onTrackInfoChanged()
{
    // New track — clear cached art so a fresh fetch starts
    m_coverUrl.clear();
    m_lastCoverQuery.clear();
    updatePresence();
}

void DiscordRichPresence::onStateChanged(Qmmp::State state)
{
    m_state = state;
    updatePresence();
}

void DiscordRichPresence::updatePresence()
{
    if (m_state == Qmmp::Stopped
        || m_state == Qmmp::NormalError
        || m_state == Qmmp::FatalError) {
        m_ipc->clearActivity();
        return;
    }

    SoundCore *core = SoundCore::instance();
    const TrackInfo &info = core->trackInfo();

    const QString title  = info.value(Qmmp::TITLE);
    const QString artist = info.value(Qmmp::ARTIST);
    const QString album  = info.value(Qmmp::ALBUM);

    // Fall back to filename when no title tag is present.
    // Strip the extension then any leading track-number prefix (e.g. "08-8 - ").
    QString details = title;
    if (details.isEmpty()) {
        details = core->path().section(QLatin1Char('/'), -1);
        details = details.section(QLatin1Char('.'), 0, -2);
        static const QRegularExpression trackPrefix(
            QStringLiteral("^\\d+([\\-.]\\d+)?\\s*[\\-.]\\s*"));
        details.remove(trackPrefix);
    }

    QString state;
    if (m_state == Qmmp::Paused) {
        state = QStringLiteral("Paused");
    } else {
        if (!artist.isEmpty() && !album.isEmpty())
            state = QStringLiteral("by %1 — %2").arg(artist, album);
        else if (!artist.isEmpty())
            state = QStringLiteral("by %1").arg(artist);
        else if (!album.isEmpty())
            state = album;
    }

    qint64 startTimestamp = 0;
    if (m_state == Qmmp::Playing)
        startTimestamp = QDateTime::currentSecsSinceEpoch() - core->elapsed() / 1000;

    m_ipc->setActivity(details, state, startTimestamp, m_coverUrl);

    // Kick off a cover art fetch for new tracks while playing.
    // (Skip while paused — the track hasn't changed, art is already set.)
    if (m_state == Qmmp::Playing && m_coverUrl.isEmpty())
        fetchCoverArt(artist, album, details);
}

void DiscordRichPresence::fetchCoverArt(const QString &artist,
                                         const QString &album,
                                         const QString &title)
{
    // Build a search query: prefer "artist album", fall back to title alone.
    const QString searchTerm = artist.isEmpty()
        ? title
        : (album.isEmpty() ? artist : artist + QLatin1Char(' ') + album);

    if (searchTerm.isEmpty() || searchTerm == m_lastCoverQuery)
        return;

    m_lastCoverQuery = searchTerm;

    // iTunes Search API — free, no key, returns HTTPS artwork URLs that
    // Discord accepts directly as large_image values.
    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("term"),   searchTerm);
    query.addQueryItem(QStringLiteral("entity"), QStringLiteral("album"));
    query.addQueryItem(QStringLiteral("limit"),  QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("media"),  QStringLiteral("music"));
    url.setQuery(query);

    qDebug("DiscordRichPresence: fetching cover art for \"%s\"",
           qUtf8Printable(searchTerm));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("qmmp-discord-rich-presence"));

    QNetworkReply *reply = m_nam->get(request);
    const QString capturedQuery = searchTerm;

    connect(reply, &QNetworkReply::finished, this, [this, reply, capturedQuery]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qDebug("DiscordRichPresence: cover art request failed: %s",
                   qUtf8Printable(reply->errorString()));
            return;
        }

        const QJsonArray results =
            QJsonDocument::fromJson(reply->readAll()).object()
                .value(QStringLiteral("results")).toArray();

        if (results.isEmpty()) {
            qDebug("DiscordRichPresence: no iTunes results for \"%s\"",
                   qUtf8Printable(capturedQuery));
            return;
        }

        QString artUrl = results.first().toObject()
                             .value(QStringLiteral("artworkUrl100")).toString();
        if (artUrl.isEmpty()) {
            qDebug("DiscordRichPresence: artworkUrl100 missing in iTunes response");
            return;
        }

        // Upgrade the thumbnail: replace any NxNbb size specifier with 600x600bb.
        static const QRegularExpression sizeRe(QStringLiteral("\\d+x\\d+bb\\.jpg"));
        artUrl.replace(sizeRe, QStringLiteral("600x600bb.jpg"));

        qDebug("DiscordRichPresence: cover art URL: %s", qUtf8Printable(artUrl));

        // Only apply if the track hasn't changed while we were waiting.
        if (m_lastCoverQuery != capturedQuery)
            return;

        m_coverUrl = artUrl;
        updatePresence();
    });
}
