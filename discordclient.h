#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QSslSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QString>

// Minimal Discord client: REST + Gateway v10, text-only.
// Protocol recovered from Discord's Electron app.asar:
//   REST   https://discord.com/api
//   Gateway wss://gateway.discord.gg/?v=10&encoding=json
//
// Use a BOT token (Authorization: Bot <token>). Never hardcode a real token.

class DiscordClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY statusChanged)
    Q_PROPERTY(QString currentGuildId READ currentGuildId NOTIFY currentGuildIdChanged)
    Q_PROPERTY(QString currentChannelId READ currentChannelId NOTIFY currentChannelIdChanged)
    Q_PROPERTY(QVariantList guilds READ guilds NOTIFY guildsChanged)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList activeMessages READ activeMessages NOTIFY activeMessagesChanged)
    Q_PROPERTY(QJsonObject user READ user NOTIFY userChanged)

public:
    explicit DiscordClient(QObject *parent = nullptr);
    void setToken(const QString &token);
    void setUseBotPrefix(bool useBot);
    void connectGateway();

    Q_INVOKABLE void sendMessage(const QString &channelId, const QString &content);
    Q_INVOKABLE void selectChannel(const QString &channelId);
    Q_INVOKABLE void selectGuild(const QString &guildId);
    Q_INVOKABLE void loadMoreMessages();

    QString status() const { return m_status; }
    QString currentGuildId() const { return m_currentGuildId; }
    QString currentChannelId() const { return m_currentChannelId; }
    QVariantList guilds() const { return m_guilds; }
    QVariantList channels() const { return m_channels; }
    QVariantList messages() const { return m_messages; }
    QVariantList activeMessages() const { return m_activeMessages; }
    QJsonObject user() const { return m_user; }

    signals:
    void statusChanged();
    void currentGuildIdChanged();
    void currentChannelIdChanged();
    void guildsChanged();
    void channelsChanged();
    void messagesChanged();
    void activeMessagesChanged();
    void userChanged();
    void errorOccurred(const QString &msg);

private slots:
    void onRestReply(QNetworkReply *reply);
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError);
    void onSslErrors(const QList<QSslError> &errors);
    void onHeartbeat();

private:
    enum RestOp { None, Me, MyGuilds, GuildChannels, ChannelMessages, ChannelMessagesMore, SendMessage };
    void restGet(const QString &path);
    void restPost(const QString &path, const QJsonObject &body);
    void fetchGuildChannels(const QString &guildId);
    void sendTextFrame(const QString &text);
    void sendOp(int op, const QJsonObject &d);
    void identify();
    void handleFrame(int opcode, bool fin, const QByteArray &payload);
    void processHttp();
    void processFrames();

    void setStatus(const QString &s);

    QString m_token;
    bool m_useBotPrefix = true;
    QString m_status = "Connecting...";
    QString m_currentGuildId;
    QString m_currentChannelId;
    RestOp m_restOp = None;
    QNetworkAccessManager m_nam;
    QSslSocket m_sock;
    QTimer m_hbTimer;
    QTimer m_retryTimer;
    QByteArray m_buffer;
    bool m_inHttp = false;
    QString m_handshakePath;
    QString m_handshakeHost;
    qint64 m_lastHeartbeat = 0;
    qint64 m_lastHeartbeatAck = 0;
    QJsonObject m_user;
    QVariantList m_guilds;
    QVariantList m_channels;
    QVariantList m_messages;
    QMap<QString, QJsonArray> m_messageCache;
    QVariantList m_activeMessages;
    QString m_sessionId;
    qint64 m_lastSeq = 0;
    bool m_resumable = false;
    bool m_wantResume = false;

    // Rate-limit retry (one pending request).
    QString m_retryPath;
    QJsonObject m_retryBody;
    bool m_retryIsPost = false;
    RestOp m_retryOp = None;

    QString m_lastRestPath;
    QJsonObject m_lastRestBody;
    bool m_lastRestIsPost = false;

    QVariantList activeMessagesFrom(const QString &channelId) const;
    void sendResume();
    void doRetry();
};
