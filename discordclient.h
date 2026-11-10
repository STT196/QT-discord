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
#include <QStringList>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QMimeDatabase>
#include <QVariantMap>
#include <QMediaPlayer>
#include <QAudioOutput>
#include "voiceclient.h"
#include <QAudioSink>

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
    Q_PROPERTY(QVariantList pins READ pins NOTIFY pinsChanged)
    Q_PROPERTY(QVariantList dmChannels READ dmChannels NOTIFY dmChannelsChanged)
    Q_PROPERTY(bool currentChannelIsDm READ currentChannelIsDm NOTIFY currentChannelIsDmChanged)
    Q_PROPERTY(QVariantList members READ members NOTIFY membersChanged)
    Q_PROPERTY(QJsonObject user READ user NOTIFY userChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY userChanged)
    Q_PROPERTY(QString typingText READ typingText NOTIFY typingTextChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool isBot READ isBot)
    Q_PROPERTY(QStringList unreadChannelIds READ unreadChannelIds NOTIFY unreadChanged)
    Q_PROPERTY(bool inVoice READ inVoice NOTIFY voiceChanged)
    Q_PROPERTY(bool voiceConnected READ voiceConnected NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceChannelId READ voiceChannelId NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceGuildId READ voiceGuildId NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceStatus READ voiceStatus NOTIFY voiceStatusChanged)
    Q_PROPERTY(bool selfMute READ selfMute NOTIFY selfMuteChanged)
    Q_PROPERTY(bool selfDeaf READ selfDeaf NOTIFY selfDeafChanged)
    Q_PROPERTY(QVariantList voiceParticipants READ voiceParticipants NOTIFY voiceParticipantsChanged)
    Q_PROPERTY(QVariantList regions READ regions NOTIFY regionsChanged)
    Q_PROPERTY(int voiceLatency READ voiceLatency NOTIFY voiceLatencyChanged)

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
    Q_INVOKABLE void fetchPins(const QString &channelId);
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
    Q_INVOKABLE void joinVoice(const QString &guildId, const QString &channelId);
    Q_INVOKABLE void leaveVoice();
    Q_INVOKABLE void setSelfMute(bool mute);
    Q_INVOKABLE void setSelfDeaf(bool deaf);
    Q_INVOKABLE void editVoiceMember(const QString &userId, bool mute, bool deaf,
                                     const QString &channelId, const QString &nick);
    Q_INVOKABLE void startDmCall(const QString &channelId);
    Q_INVOKABLE void stopDmRinging(const QString &channelId);
    Q_INVOKABLE void fetchVoiceRegions();
    Q_INVOKABLE void setVoiceRegion(const QString &regionId);
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE void playSoundboardSound(const QString &soundId);
    Q_INVOKABLE void measureVoiceLatency();
    void setTheme(const QString &t);

    QString status() const { return m_status; }
    QString typingText() const { return m_typingText; }
    QString theme() const { return m_theme; }
    bool isBot() const { return m_useBotPrefix; }
    QString currentGuildId() const { return m_currentGuildId; }
    QString currentChannelId() const { return m_currentChannelId; }
    QVariantList guilds() const { return m_guilds; }
    QVariantList channels() const { return m_channels; }
    QVariantList messages() const { return m_messages; }
    QVariantList activeMessages() const { return m_activeMessages; }
    QVariantList pins() const { return m_pins; }
    QVariantList dmChannels() const { return m_dmChannels; }
    bool currentChannelIsDm() const { return m_currentChannelIsDm; }
    QVariantList members() const { return m_members; }
    QJsonObject user() const { return m_user; }
    bool loggedIn() const { return !m_user.value("id").toString().isEmpty(); }
    QStringList unreadChannelIds() const;
    bool inVoice() const { return !m_voiceChannelId.isEmpty(); }
    bool voiceConnected() const { return m_voice.isConnected(); }
    QString voiceChannelId() const { return m_voiceChannelId; }
    QString voiceGuildId() const { return m_voiceGuildId; }
    QString voiceStatus() const { return m_voice.status(); }
    bool selfMute() const { return m_selfMute; }
    bool selfDeaf() const { return m_selfDeaf; }
    QVariantList voiceParticipants() const { return m_voiceParticipants; }
    QVariantList regions() const { return m_regions; }
    int voiceLatency() const { return m_voiceLatency; }
    QVariantList soundboardSounds() const { return m_soundboardSounds; }

    signals:
    void statusChanged();
    void unreadChanged();
    void currentGuildIdChanged();
    void currentChannelIdChanged();
    void guildsChanged();
    void channelsChanged();
    void messagesChanged();
    void activeMessagesChanged();
    void pinsChanged();
    void dmChannelsChanged();
    void currentChannelIsDmChanged();
    void membersChanged();
    void userChanged();
    void typingTextChanged();
    void themeChanged();
    void messageReceived(const QString &author, const QString &content, const QString &channelId);
    void errorOccurred(const QString &msg);
    void voiceChanged();
    void voiceStatusChanged();
    void selfMuteChanged();
    void selfDeafChanged();
    void voiceParticipantsChanged();
    void regionsChanged();
    void voiceLatencyChanged();
    void voiceError(const QString &msg);
    void dmCallCreated(const QString &channelId, const QStringList &ringingUsers);
    void dmCallUpdated(const QString &channelId);
    void dmCallDeleted(const QString &channelId);
private slots:
    void onRestReply(QNetworkReply *reply);
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError);
    void onSslErrors(const QList<QSslError> &errors);
    void onHeartbeat();
    void doReconnect();

private:
    enum RestOp { None, Me, MyGuilds, GuildChannels, ChannelMessages, ChannelMessagesMore, SendMessage, ReactionAdd, ReactionRemove, SendFile, DmChannels, OpenDm, GuildMembers, ChannelPins, DmCall, MemberEdit, FetchRegions };
    enum RestMethod { Get, Post, Put, Patch, Delete };
    void restGet(const QString &path);
    void restPost(const QString &path, const QJsonObject &body);
    void restPut(const QString &path);
    void restPatch(const QString &path, const QJsonObject &body);
    void restDelete(const QString &path);
    void stampRest(QNetworkReply *reply, RestOp op, const QString &path,
                   const QJsonObject &body, bool isPost, RestMethod method);
    void fetchGuildChannels(const QString &guildId);
    void fetchChannelAt(int index);
    void sendTextFrame(const QString &text);
    void sendOp(int op, const QJsonObject &d);
    void identify();
    void handleFrame(int opcode, bool fin, const QByteArray &payload);
    void processHttp();
    void processFrames();
    void scheduleReconnect();

    void setStatus(const QString &s);

    void updateCurrentChannelIsDm();

    QString m_token;
    bool m_useBotPrefix = true;
    int m_intents = 641;
    QStringList m_textChannels;
    int m_textIndex = 0;
    bool m_autoSelecting = false;
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
    QTimer m_reconnectTimer;
    int m_reconnectAttempt = 0;
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
    QVariantList m_pins;
    QVariantList m_dmChannels;
    bool m_currentChannelIsDm = false;
    QVariantList m_members;
    QHash<QString, QString> m_presence;
    QString m_sessionId;
    qint64 m_lastSeq = 0;
    bool m_resumable = false;
    bool m_wantResume = false;
    bool m_fatalClose = false;

    QString m_typingText;
    QString m_theme = "dark";
    QHash<QString, qint64> m_lastTyping;   // channelId → last typing-indicator send ms
    QSet<QString> m_unread;
    QMap<QString, QHash<QString, qint64>> m_typing;      // channelId → userId → lastSeen ms
    QHash<QString, QString> m_userNames;                 // userId → display name

    // Rate-limit retry (one pending request).
    QString m_retryPath;
    QJsonObject m_retryBody;
    bool m_retryIsPost = false;
    RestMethod m_retryMethod = Get;
    RestOp m_retryOp = None;

    QVariantList activeMessagesFrom(const QString &channelId) const;
    static quint64 snowflake(const QString &id);
    static QString reactionKey(const QJsonObject &emoji);
    void normalizeMessages(QJsonArray &arr) const;
    void updateReaction(const QString &channelId, const QString &messageId, const QJsonObject &emoji, int delta, bool self);
    void sendResume();
    void doRetry();

    void recomputeTyping();
    void pruneTyping();

    void rememberUser(const QJsonObject &user);
    void rememberMessageUsers(const QJsonArray &messages);

    VoiceClient m_voice;
    QString m_voiceChannelId;
    QString m_voiceGuildId;
    QString m_pendingVoiceGuild;
    QString m_pendingVoiceChannel;
    QVariantList m_voiceParticipants;
    QHash<QString, QVariantMap> m_voiceStates; // userId -> voice state
    QVariantList m_regions;
    QVariantList m_soundboardSounds;
    int m_voiceLatency = 0;
    qint64 m_voiceLatencyStart = 0;
    QMediaPlayer *m_soundboardPlayer = nullptr;
    QAudioOutput *m_soundboardOutput = nullptr;
    bool m_selfMute = true;
    bool m_selfDeaf = true;
    void sendVoiceState();
    void applyVoiceState(const QJsonObject &d);
    void storeVoiceState(const QJsonObject &d);
    void rebuildVoiceParticipants();
    void playTone(int frequencyHz, int durationMs);
};
