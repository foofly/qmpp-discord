#include "discordipc.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QUuid>

static constexpr quint32 OP_HANDSHAKE = 0;
static constexpr quint32 OP_FRAME     = 1;
static constexpr int RECONNECT_MS     = 15000;

// ── Construction ─────────────────────────────────────────────────────────────

DiscordIPC::DiscordIPC(QObject *parent)
    : QObject(parent)
#ifdef HAVE_WEBSOCKETS
    , m_webSocket(QString(), QWebSocketProtocol::VersionLatest, this)
#endif
{
    // Unix socket signals
    connect(&m_socket, &QLocalSocket::connected,     this, &DiscordIPC::onSocketConnected);
    connect(&m_socket, &QLocalSocket::readyRead,     this, &DiscordIPC::onSocketReadyRead);
    connect(&m_socket, &QLocalSocket::disconnected,  this, &DiscordIPC::onSocketDisconnected);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, &DiscordIPC::onSocketError);

#ifdef HAVE_WEBSOCKETS
    // WebSocket signals (arRPC fallback for Flatpak Discord/Vesktop)
    connect(&m_webSocket, &QWebSocket::connected,           this, &DiscordIPC::onWsConnected);
    connect(&m_webSocket, &QWebSocket::textMessageReceived, this, &DiscordIPC::onWsTextReceived);
    connect(&m_webSocket, &QWebSocket::disconnected,        this, &DiscordIPC::onWsDisconnected);
    connect(&m_webSocket, &QWebSocket::errorOccurred,       this, &DiscordIPC::onWsError);
#endif

    m_reconnectTimer.setInterval(RECONNECT_MS);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &DiscordIPC::connectToDiscord);
}

DiscordIPC::~DiscordIPC()
{
    m_reconnectTimer.stop();
    m_socket.disconnectFromServer();
#ifdef HAVE_WEBSOCKETS
    m_webSocket.close();
#endif
}

// ── Public API ───────────────────────────────────────────────────────────────

void DiscordIPC::setAppId(const QString &appId)
{
    if (m_appId == appId)
        return;
    m_appId = appId;
    m_reconnectTimer.stop();
    m_socket.disconnectFromServer();
#ifdef HAVE_WEBSOCKETS
    m_webSocket.close();
#endif
    m_transport = Transport::None;
    m_ready = false;
    if (!m_appId.isEmpty())
        connectToDiscord();
}

void DiscordIPC::connectToDiscord()
{
    if (m_appId.isEmpty())
        return;
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        return;
#ifdef HAVE_WEBSOCKETS
    if (m_webSocket.state() != QAbstractSocket::UnconnectedState)
        return;
#endif

    // Prefer the Unix socket (native Discord / Vesktop native).
    // Fall straight through to WebSocket when no socket file exists
    // (e.g. Vesktop running inside a Flatpak sandbox).
    const QString path = findSocketPath();
    if (!path.isEmpty())
        tryUnixSocket();
    else {
#ifdef HAVE_WEBSOCKETS
        tryWebSocket();
#else
        m_reconnectTimer.start();
#endif
    }
}

// ── Private: connection helpers ───────────────────────────────────────────────

QString DiscordIPC::findSocketPath() const
{
    QStringList bases;
    const auto tryEnv = [&](const char *name) {
        const QString val = qEnvironmentVariable(name);
        if (!val.isEmpty())
            bases << val;
    };
    tryEnv("XDG_RUNTIME_DIR");
    tryEnv("TMPDIR");
    tryEnv("TMP");
    tryEnv("TEMP");
    bases << QStringLiteral("/tmp");

    // Standard locations (native Discord, Vesktop native)
    for (const QString &base : std::as_const(bases))
        for (int i = 0; i <= 9; ++i) {
            const QString path = base + QStringLiteral("/discord-ipc-") + QString::number(i);
            if (QFileInfo::exists(path))
                return path;
        }

    const QString xdgRun = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!xdgRun.isEmpty()) {
        // Flatpak per-app runtime directories:
        // $XDG_RUNTIME_DIR/.flatpak/<app-id>/xdg-run/discord-ipc-N
        QDir flatpakDir(xdgRun + QStringLiteral("/.flatpak"));
        for (const QString &app : flatpakDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString base = flatpakDir.filePath(app + QStringLiteral("/xdg-run"));
            for (int i = 0; i <= 9; ++i) {
                const QString path = base + QStringLiteral("/discord-ipc-") + QString::number(i);
                if (QFileInfo::exists(path))
                    return path;
            }
        }

        // Snap per-app runtime directories:
        // $XDG_RUNTIME_DIR/snap.<name>/discord-ipc-N
        QDir xdgDir(xdgRun);
        for (const QString &entry : xdgDir.entryList(QStringList{QStringLiteral("snap.*")},
                                                      QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString base = xdgDir.filePath(entry);
            for (int i = 0; i <= 9; ++i) {
                const QString path = base + QStringLiteral("/discord-ipc-") + QString::number(i);
                if (QFileInfo::exists(path))
                    return path;
            }
        }
    }

    return {};
}

void DiscordIPC::tryUnixSocket()
{
    m_transport = Transport::UnixSocket;
    m_socket.connectToServer(findSocketPath(), QIODevice::ReadWrite);
}

#ifdef HAVE_WEBSOCKETS
void DiscordIPC::tryWebSocket()
{
    m_transport = Transport::WebSocket;
    // The client_id in the URL query is how arRPC identifies the caller.
    const QUrl url(QStringLiteral("ws://127.0.0.1:6463/?v=1&client_id=") + m_appId);
    m_webSocket.open(url);
}
#endif

// ── Private: framing ──────────────────────────────────────────────────────────

// Packs and sends a Discord binary IPC frame over the Unix socket.
// Layout: [uint32 opcode LE][uint32 length LE][JSON payload]
// Written as one call to avoid pipe fragmentation.
void DiscordIPC::sendFrame(quint32 opcode, const QByteArray &json)
{
    if (m_socket.state() != QLocalSocket::ConnectedState)
        return;

    const quint32 len = static_cast<quint32>(json.size());
    QByteArray frame(8 + json.size(), Qt::Uninitialized);
    char *d = frame.data();
    d[0] = static_cast<char>(opcode        & 0xFF);
    d[1] = static_cast<char>((opcode >> 8) & 0xFF);
    d[2] = static_cast<char>((opcode >>16) & 0xFF);
    d[3] = static_cast<char>((opcode >>24) & 0xFF);
    d[4] = static_cast<char>(len        & 0xFF);
    d[5] = static_cast<char>((len >> 8) & 0xFF);
    d[6] = static_cast<char>((len >>16) & 0xFF);
    d[7] = static_cast<char>((len >>24) & 0xFF);
    std::memcpy(d + 8, json.constData(), json.size());

    m_socket.write(frame);
    m_socket.flush();
}

void DiscordIPC::sendHandshake()
{
    QJsonObject obj;
    obj[QStringLiteral("v")]         = 1;
    obj[QStringLiteral("client_id")] = m_appId;
    sendFrame(OP_HANDSHAKE, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ── Unix socket slots ─────────────────────────────────────────────────────────

void DiscordIPC::onSocketConnected()
{
    sendHandshake();
}

void DiscordIPC::onSocketReadyRead()
{
    while (m_socket.bytesAvailable() >= 8) {
        const QByteArray header = m_socket.peek(8);
        const quint32 opcode = static_cast<quint8>(header[0])
                             | (static_cast<quint8>(header[1]) << 8)
                             | (static_cast<quint8>(header[2]) << 16)
                             | (static_cast<quint8>(header[3]) << 24);
        const quint32 length  = static_cast<quint8>(header[4])
                             | (static_cast<quint8>(header[5]) << 8)
                             | (static_cast<quint8>(header[6]) << 16)
                             | (static_cast<quint8>(header[7]) << 24);

        if (m_socket.bytesAvailable() < static_cast<qint64>(8 + length))
            break;

        m_socket.read(8);
        const QByteArray payload = m_socket.read(length);

        if (opcode == OP_FRAME && !m_ready) {
            const QJsonObject root = QJsonDocument::fromJson(payload).object();
            if (root.value(QStringLiteral("evt")).toString() == QStringLiteral("READY"))
                onReady();
        }
    }
}

void DiscordIPC::onSocketDisconnected()
{
    m_ready = false;
    m_transport = Transport::None;
    emit disconnected();
    m_reconnectTimer.start();
}

void DiscordIPC::onSocketError(QLocalSocket::LocalSocketError)
{
    if (m_socket.state() != QLocalSocket::ConnectedState) {
        m_ready = false;
        m_transport = Transport::None;
        m_reconnectTimer.start();
    }
}

// ── WebSocket slots ───────────────────────────────────────────────────────────

#ifdef HAVE_WEBSOCKETS
void DiscordIPC::onWsConnected()
{
    // arRPC sends READY automatically after the URL handshake;
    // wait for it before marking ready (handled in onWsTextReceived).
}

void DiscordIPC::onWsTextReceived(const QString &message)
{
    if (m_ready)
        return;
    const QJsonObject root = QJsonDocument::fromJson(message.toUtf8()).object();
    if (root.value(QStringLiteral("evt")).toString() == QStringLiteral("READY"))
        onReady();
}

void DiscordIPC::onWsDisconnected()
{
    m_ready = false;
    m_transport = Transport::None;
    emit disconnected();
    m_reconnectTimer.start();
}

void DiscordIPC::onWsError(QAbstractSocket::SocketError)
{
    if (m_webSocket.state() != QAbstractSocket::ConnectedState) {
        m_ready = false;
        m_transport = Transport::None;
        m_reconnectTimer.start();
    }
}
#endif

// ── Shared ready / activity logic ─────────────────────────────────────────────

void DiscordIPC::onReady()
{
    m_ready = true;
    emit ready();
    sendPendingActivity();
}

void DiscordIPC::sendPendingActivity()
{
    if (m_pendingClear)
        clearActivity();
    else if (m_hasPendingActivity)
        setActivity(m_pendingDetails, m_pendingState, m_pendingTimestamp, m_pendingLargeImage);
}

void DiscordIPC::setActivity(const QString &details, const QString &state,
                             qint64 startTimestamp, const QString &largeImage)
{
    m_pendingDetails     = details;
    m_pendingState       = state;
    m_pendingTimestamp   = startTimestamp;
    m_pendingLargeImage  = largeImage;
    m_hasPendingActivity = true;
    m_pendingClear       = false;

    if (!m_ready)
        return;

    QJsonObject activity;
    if (!details.isEmpty())
        activity[QStringLiteral("details")] = details;
    if (!state.isEmpty())
        activity[QStringLiteral("state")] = state;
    if (startTimestamp > 0) {
        QJsonObject ts;
        ts[QStringLiteral("start")] = startTimestamp;
        activity[QStringLiteral("timestamps")] = ts;
    }
    QJsonObject assets;
    // Use the provided image URL if available, otherwise fall back to the
    // "qmmp" named asset uploaded in the Discord Developer Portal.
    assets[QStringLiteral("large_image")] = largeImage.isEmpty()
        ? QStringLiteral("qmmp") : largeImage;
    assets[QStringLiteral("large_text")]  = QStringLiteral("QMMP Music Player");
    activity[QStringLiteral("assets")]    = assets;

    QJsonObject args;
    args[QStringLiteral("pid")]      = static_cast<qint64>(QCoreApplication::applicationPid());
    args[QStringLiteral("activity")] = activity;

    QJsonObject root;
    root[QStringLiteral("cmd")]   = QStringLiteral("SET_ACTIVITY");
    root[QStringLiteral("nonce")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    root[QStringLiteral("args")]  = args;

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

    if (m_transport == Transport::UnixSocket)
        sendFrame(OP_FRAME, json);
#ifdef HAVE_WEBSOCKETS
    else if (m_transport == Transport::WebSocket)
        m_webSocket.sendTextMessage(QString::fromUtf8(json));
#endif
}

void DiscordIPC::clearActivity()
{
    m_hasPendingActivity = false;
    m_pendingClear       = true;

    if (!m_ready)
        return;

    QJsonObject args;
    args[QStringLiteral("pid")]      = static_cast<qint64>(QCoreApplication::applicationPid());
    args[QStringLiteral("activity")] = QJsonValue::Null;

    QJsonObject root;
    root[QStringLiteral("cmd")]   = QStringLiteral("SET_ACTIVITY");
    root[QStringLiteral("nonce")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    root[QStringLiteral("args")]  = args;

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

    if (m_transport == Transport::UnixSocket)
        sendFrame(OP_FRAME, json);
#ifdef HAVE_WEBSOCKETS
    else if (m_transport == Transport::WebSocket)
        m_webSocket.sendTextMessage(QString::fromUtf8(json));
#endif
}
