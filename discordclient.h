#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QSslSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QHash>
#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QMimeDatabase>

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
    Q_PROPERTY(QVariantList dmChannels READ dmChannels NOTIFY dmChannelsChanged)
    Q_PROPERTY(QVariantList members READ members NOTIFY membersChanged)
    Q_PROPERTY(QJsonObject user READ user NOTIFY userChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY userChanged)
    Q_PROPERTY(QString typingText READ typingText NOTIFY typingTextChanged)

public:
    explicit DiscordClient(QObject *parent = nullptr);
    void setToken(const QString &token);
    void setUseBotPrefix(bool useBot);
    void setIntents(int intents);
    int intents() const { return m_intents; }
    void connectGateway();

    Q_INVOKABLE void sendMessage(const QString &channelId, const QString &content);
    Q_INVOKABLE void selectChannel(const QString &channelId);
    Q_INVOKABLE void selectGuild(const QString &guildId);
    Q_INVOKABLE void refreshMembers(const QString &guildId);
    Q_INVOKABLE void loadMoreMessages();
    Q_INVOKABLE void refreshDms();
    Q_INVOKABLE void openDm(const QString &userId);
    Q_INVOKABLE void addReaction(const QString &channelId, const QString &messageId, const QString &emoji);
    Q_INVOKABLE void removeReaction(const QString &channelId, const QString &messageId, const QString &emoji);
    Q_INVOKABLE void sendFile(const QString &channelId, const QString &filePath, const QString &content);
    Q_INVOKABLE void login(const QString &token, bool useBot);
    Q_INVOKABLE QString markdownToHtml(const QString &text) const;
    Q_INVOKABLE QString emojiUrl(const QString &id, bool animated) const;
    Q_INVOKABLE QString avatarUrl(const QJsonObject &user, int size = 64) const;
    Q_INVOKABLE void triggerTyping(const QString &channelId);

    QString status() const { return m_status; }
    QString typingText() const { return m_typingText; }
    QString currentGuildId() const { return m_currentGuildId; }
    QString currentChannelId() const { return m_currentChannelId; }
    QVariantList guilds() const { return m_guilds; }
    QVariantList channels() const { return m_channels; }
    QVariantList messages() const { return m_messages; }
    QVariantList activeMessages() const { return m_activeMessages; }
    QVariantList dmChannels() const { return m_dmChannels; }
    QVariantList members() const { return m_members; }
    QJsonObject user() const { return m_user; }
    bool loggedIn() const { return !m_user.value("id").toString().isEmpty(); }

    signals:
    void statusChanged();
    void currentGuildIdChanged();
    void currentChannelIdChanged();
    void guildsChanged();
    void channelsChanged();
    void messagesChanged();
    void activeMessagesChanged();
    void dmChannelsChanged();
    void membersChanged();
    void userChanged();
    void typingTextChanged();
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
    enum RestOp { None, Me, MyGuilds, GuildChannels, ChannelMessages, ChannelMessagesMore, SendMessage, ReactionAdd, ReactionRemove, SendFile, DmChannels, OpenDm, GuildMembers };
    enum RestMethod { Get, Post, Put, Delete };
    void restGet(const QString &path);
    void restPost(const QString &path, const QJsonObject &body);
    void restPut(const QString &path);
    void restDelete(const QString &path);
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
    int m_intents = 513;
    QString m_status = "Connecting...";
    QString m_currentGuildId;
    QString m_currentChannelId;
    RestOp m_restOp = None;
    bool m_gatewayStarted = false;
    QNetworkAccessManager m_nam;
    QSslSocket m_sock;
    QTimer m_hbTimer;
    QTimer m_retryTimer;
    QTimer m_typingTimer;
    QByteArray m_buffer;
    bool m_inHttp = false;
    QString m_handshakePath;
    QString m_handshakeHost;
    qint64 m_lastHeartbeat = 0;
    qint64 m_lastHeartbeatAck = 0;
    QJsonObject m_user;
    QString m_selfId;
    QVariantList m_guilds;
    QVariantList m_channels;
    QVariantList m_messages;
    QMap<QString, QJsonArray> m_messageCache;
    QVariantList m_activeMessages;
    QVariantList m_dmChannels;
    QVariantList m_members;
    QHash<QString, QString> m_presence;
    QString m_sessionId;
    qint64 m_lastSeq = 0;
    bool m_resumable = false;
    bool m_wantResume = false;

    QString m_typingText;
    QMap<QString, QHash<QString, qint64>> m_typing;      // channelId → userId → lastSeen ms
    QHash<QString, QString> m_userNames;                 // userId → display name

    // Rate-limit retry (one pending request).
    QString m_retryPath;
    QJsonObject m_retryBody;
    bool m_retryIsPost = false;
    RestMethod m_retryMethod = Get;
    RestOp m_retryOp = None;

    QString m_lastRestPath;
    QJsonObject m_lastRestBody;
    bool m_lastRestIsPost = false;
    RestMethod m_lastRestMethod = Get;

    QVariantList activeMessagesFrom(const QString &channelId) const;
    static quint64 snowflake(const QString &id);
    static QString reactionKey(const QJsonObject &emoji);
    void normalizeMessages(QJsonArray &arr) const;
    void updateReaction(const QString &channelId, const QString &messageId, const QJsonObject &emoji, int delta, bool self);
    void sendResume();
    void doRetry();

    void recomputeTyping();
    void pruneTyping();
};
