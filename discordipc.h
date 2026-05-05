#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QTimer>

#ifdef HAVE_WEBSOCKETS
#include <QWebSocket>
#endif

class DiscordIPC : public QObject
{
    Q_OBJECT
public:
    explicit DiscordIPC(QObject *parent = nullptr);
    ~DiscordIPC();

    void setAppId(const QString &appId);
    void connectToDiscord();
    void setActivity(const QString &details, const QString &state,
                     qint64 startTimestamp, const QString &largeImage = {});
    void clearActivity();
    bool isReady() const { return m_ready; }

signals:
    void ready();
    void disconnected();

private slots:
    // Unix socket
    void onSocketConnected();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError);

#ifdef HAVE_WEBSOCKETS
    // WebSocket fallback (arRPC on port 6463 — used by Flatpak Discord/Vesktop)
    void onWsConnected();
    void onWsTextReceived(const QString &message);
    void onWsDisconnected();
    void onWsError(QAbstractSocket::SocketError);
#endif

private:
    enum class Transport { None, UnixSocket, WebSocket };

    void tryUnixSocket();
    void tryWebSocket();
    void sendFrame(quint32 opcode, const QByteArray &json);
    void sendHandshake();
    void onReady();
    void sendPendingActivity();
    QString findSocketPath() const;

    QLocalSocket m_socket;
#ifdef HAVE_WEBSOCKETS
    QWebSocket   m_webSocket;
#endif
    Transport    m_transport = Transport::None;

    QTimer  m_reconnectTimer;
    QString m_appId;
    bool    m_ready = false;

    QString m_pendingDetails;
    QString m_pendingState;
    qint64  m_pendingTimestamp = 0;
    QString m_pendingLargeImage;
    bool    m_hasPendingActivity = false;
    bool    m_pendingClear = false;
};
