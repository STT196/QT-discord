#include "discordclient.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QUuid>
#include <QUrl>
#include <QDateTime>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <algorithm>
#include <vector>
#include <climits>
#include <cstdio>
#include <QtMath>
#include <QBuffer>
#include <QAudioSink>
#include <QAudioFormat>

static void log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fprintf(stderr, "[discord] %s\n", buf);
}

static int channelPosition(const QJsonObject &o) {
    QJsonValue p = o.value("position");
    if (p.isUndefined() || p.isNull())
        return INT_MAX;
    return p.toInt();
}

static QJsonArray sortChannels(const QJsonArray &arr) {
    std::vector<QJsonValue> vec;
    vec.reserve(arr.size());
    for (const QJsonValue &v : arr)
        vec.push_back(v);
    std::sort(vec.begin(), vec.end(),
              [](const QJsonValue &a, const QJsonValue &b) {
                  return channelPosition(a.toObject()) < channelPosition(b.toObject());
              });
    QJsonArray out;
    for (const QJsonValue &v : vec)
        out.append(v);
    return out;
}

// Build the member-list entry shape used by REST + gateway dispatches.
static QVariantMap memberMap(const QJsonObject &d, const QHash<QString, QString> &presence) {
    QJsonObject user = d.value("user").toObject();
    QString id = user.value("id").toString();
    QVariantMap m;
    m["id"] = id;
    m["username"] = user.value("username").toString();
    m["global_name"] = user.value("global_name").toString();
    m["avatar"] = user.value("avatar").toString();
    m["nick"] = d.value("nick").toString();
    m["roles"] = d.value("roles").toArray().toVariantList();
    m["status"] = presence.value(id, QStringLiteral("offline"));
    return m;
}

void DiscordClient::rememberUser(const QJsonObject &user) {
    const QString id = user.value("id").toString();
    if (id.isEmpty())
        return;
    QString name = user.value("global_name").toString();
    if (name.isEmpty())
        name = user.value("username").toString();
    if (!name.isEmpty())
        m_userNames[id] = name;
}

void DiscordClient::rememberMessageUsers(const QJsonArray &messages) {
    for (const QJsonValue &v : messages) {
        const QJsonObject msg = v.toObject();
        rememberUser(msg.value("author").toObject());
        const QJsonArray mentions = msg.value("mentions").toArray();
        for (const QJsonValue &mv : mentions)
            rememberUser(mv.toObject());
    }
}

static int memberIndex(const QVariantList &list, const QString &id) {
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toMap().value("id").toString() == id)
            return i;
    return -1;
}

static QVariantMap voiceParticipantMap(const QJsonObject &d) {
    QJsonObject member = d.value("member").toObject();
    QJsonObject user = member.value("user").toObject();
    QString id = d.value("user_id").toString();
    if (id.isEmpty())
        id = user.value("id").toString();
    QString name = member.value("nick").toString();
    if (name.isEmpty())
        name = user.value("global_name").toString();
    if (name.isEmpty())
        name = user.value("username").toString();
    if (name.isEmpty())
        name = id;
    QString avatar = user.value("avatar").toString();
    QVariantMap m;
    m["userId"] = id;
    m["guildId"] = d.value("guild_id").toString();
    m["channelId"] = d.value("channel_id").toString();
    m["name"] = name;
    m["avatar"] = avatar;
    m["avatarUrl"] = avatar.isEmpty()
            ? QStringLiteral("https://cdn.discordapp.com/embed/avatars/0.png")
            : QString("https://cdn.discordapp.com/avatars/%1/%2.png?size=64").arg(id, avatar);
    m["selfMute"] = d.value("self_mute").toBool();
    m["selfDeaf"] = d.value("self_deaf").toBool();
    m["mute"] = d.value("mute").toBool() || d.value("self_mute").toBool();
    m["deaf"] = d.value("deaf").toBool() || d.value("self_deaf").toBool();
    return m;
}

DiscordClient::DiscordClient(QObject *parent) : QObject(parent) {
    connect(&m_nam, &QNetworkAccessManager::finished, this, &DiscordClient::onRestReply);
    connect(&m_sock, &QSslSocket::connected, this, &DiscordClient::onConnected);
    connect(&m_sock, &QSslSocket::disconnected, this, &DiscordClient::onDisconnected);
    connect(&m_sock, &QSslSocket::readyRead, this, &DiscordClient::onReadyRead);
    connect(&m_sock, SIGNAL(errorOccurred(QAbstractSocket::SocketError)), this,
            SLOT(onSocketError(QAbstractSocket::SocketError)));
    connect(&m_sock, &QSslSocket::sslErrors, this, &DiscordClient::onSslErrors);
    connect(&m_hbTimer, &QTimer::timeout, this, &DiscordClient::onHeartbeat);
    connect(&m_retryTimer, &QTimer::timeout, this, &DiscordClient::doRetry);
    m_retryTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &DiscordClient::doReconnect);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_typingTimer, &QTimer::timeout, this, &DiscordClient::pruneTyping);
    m_typingTimer.setSingleShot(true);
    connect(&m_voice, &VoiceClient::statusChanged, this, &DiscordClient::voiceStatusChanged);
    connect(&m_voice, &VoiceClient::connectedChanged, this, &DiscordClient::voiceChanged);
    connect(&m_voice, &VoiceClient::connectedChanged, this, [this]() {
        if (m_voice.isConnected())
            playTone(800, 160);   // voice connected chime
    });
    connect(&m_voice, &VoiceClient::errorOccurred, this, &DiscordClient::voiceError);

    m_theme = QSettings("QtDiscord", "QtDiscord").value("theme", "dark").toString();
    if (m_theme != "light" && m_theme != "dark")
        m_theme = "dark";

    m_soundboardOutput = new QAudioOutput(this);
    m_soundboardPlayer = new QMediaPlayer(this);
    m_soundboardPlayer->setAudioOutput(m_soundboardOutput);
    m_soundboardOutput->setVolume(0.8);
}

void DiscordClient::login(const QString &token, bool useBot) {
    m_token = token;
    m_useBotPrefix = useBot;
    m_autoSelecting = false;
    m_textChannels.clear();
    m_textIndex = 0;
    if (!m_unread.isEmpty()) { m_unread.clear(); emit unreadChanged(); }
    if (!m_pins.isEmpty()) { m_pins.clear(); emit pinsChanged(); }
    m_restOp = Me;
    setStatus("Logging in...");
    restGet("/users/@me");
}

void DiscordClient::setToken(const QString &token) { m_token = token; }

void DiscordClient::setUseBotPrefix(bool useBot) { m_useBotPrefix = useBot; }

void DiscordClient::setIntents(int intents) { m_intents = intents; }

void DiscordClient::loadMoreMessages() {
    if (m_currentChannelId.isEmpty())
        return;
    auto it = m_messageCache.constFind(m_currentChannelId);
    if (it == m_messageCache.constEnd() || it.value().isEmpty())
        return;
    QString before = it.value().last().toObject().value("id").toString();
    m_restOp = ChannelMessagesMore;
    restGet(QString("/channels/%1/messages?before=%2&limit=50").arg(m_currentChannelId, before));
}

void DiscordClient::fetchPins(const QString &channelId) {
    if (channelId.isEmpty())
        return;
    m_restOp = ChannelPins;
    restGet(QString("/channels/%1/pins").arg(channelId));
}

void DiscordClient::recomputeTyping() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList names;
    const QHash<QString, qint64> typing = m_typing.value(m_currentChannelId);
    for (auto it = typing.constBegin(); it != typing.constEnd(); ++it) {
        if (now - it.value() <= 8000)
            names.append(m_userNames.value(it.key(), QStringLiteral("someone")));
    }
    QString text;
    if (names.isEmpty()) {
        text = QString();
    } else if (names.size() == 1) {
        text = QString("%1 is typing…").arg(names.first());
    } else if (names.size() == 2) {
        text = QString("%1 and %2 are typing…").arg(names.first(), names.last());
    } else {
        text = QStringLiteral("several people are typing…");
    }
    if (m_typingText != text) {
        m_typingText = text;
        emit typingTextChanged();
    }
}

void DiscordClient::pruneTyping() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_typing.begin(); it != m_typing.end();) {
        for (auto uit = it.value().begin(); uit != it.value().end();) {
            if (now - uit.value() > 8000)
                uit = it.value().erase(uit);
            else
                ++uit;
        }
        if (it.value().isEmpty())
            it = m_typing.erase(it);
        else
            ++it;
    }
    recomputeTyping();
}

void DiscordClient::doRetry() {
    m_restOp = m_retryOp;
    switch (m_retryMethod) {
    case Post:
        restPost(m_retryPath, m_retryBody);
        break;
    case Put:
        restPut(m_retryPath);
        break;
    case Delete:
        restDelete(m_retryPath);
        break;
    default:
        restGet(m_retryPath);
        break;
    }
    m_retryPath.clear();
    m_retryBody = QJsonObject();
    m_retryIsPost = false;
    m_retryMethod = Get;
    m_retryOp = None;
    setStatus("Retrying rate-limited request...");
}

void DiscordClient::connectGateway() {
    m_fatalClose = false;
    QUrl gw("wss://gateway.discord.gg/?v=10&encoding=json");
    m_handshakeHost = gw.host();
    m_handshakePath = gw.path();
    if (!gw.query().isEmpty())
        m_handshakePath += "?" + gw.query();
    m_sock.connectToHostEncrypted(m_handshakeHost, gw.port(443));
    m_inHttp = true;
    m_buffer.clear();
    setStatus("Connecting gateway...");
}

void DiscordClient::doReconnect() {
    connectGateway();
}

void DiscordClient::scheduleReconnect() {
    if (!m_gatewayStarted)
        return;
    if (m_reconnectTimer.isActive())
        return;
    int delay = 1000 << qMin(m_reconnectAttempt, 4);
    if (delay > 30000)
        delay = 30000;
    ++m_reconnectAttempt;
    log("Scheduling reconnect in %d ms (attempt %d)", delay, m_reconnectAttempt);
    m_reconnectTimer.start(delay);
}

void DiscordClient::selectGuild(const QString &guildId) {
    if (guildId == m_currentGuildId && !m_channels.isEmpty())
        return;
    m_currentGuildId = guildId;
    m_currentChannelId.clear();
    m_members.clear();
    emit membersChanged();
    if (!m_typingText.isEmpty()) {
        m_typingText.clear();
        emit typingTextChanged();
    }
    emit currentGuildIdChanged();
    emit currentChannelIdChanged();
    m_messages.clear();
    emit messagesChanged();
    m_activeMessages.clear();
    emit activeMessagesChanged();
    m_channels.clear();
    emit channelsChanged();
    fetchGuildChannels(guildId);
}

void DiscordClient::fetchGuildChannels(const QString &guildId) {
    m_restOp = GuildChannels;
    restGet(QString("/guilds/%1/channels").arg(guildId));
}

void DiscordClient::fetchChannelAt(int index) {
    if (index < 0 || index >= m_textChannels.size()) {
        m_autoSelecting = false;
        setStatus("Connected. (no readable text channel)");
        return;
    }
    m_textIndex = index;
    m_currentChannelId = m_textChannels[index];
    emit currentChannelIdChanged();
    m_restOp = ChannelMessages;
    restGet(QString("/channels/%1/messages?limit=25").arg(m_currentChannelId));
}

void DiscordClient::refreshMembers(const QString &guildId) {
    m_restOp = GuildMembers;
    restGet(QString("/guilds/%1/members?limit=100").arg(guildId));
}

void DiscordClient::stampRest(QNetworkReply *reply, RestOp op, const QString &path,
                              const QJsonObject &body, bool isPost, RestMethod method) {
    reply->setProperty("restOp", (int)op);
    reply->setProperty("restPath", path);
    reply->setProperty("restBody", QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setProperty("restIsPost", isPost);
    reply->setProperty("restMethod", (int)method);
}

void DiscordClient::restGet(const QString &path) {
    QUrl url(QString("https://discord.com/api%1").arg(path));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");
    req.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_nam.get(req);
    stampRest(reply, m_restOp, path, QJsonObject(), false, Get);
}

void DiscordClient::restPost(const QString &path, const QJsonObject &body) {
    QUrl url(QString("https://discord.com/api%1").arg(path));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");
    req.setRawHeader("Content-Type", "application/json");
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_nam.post(req, data);
    stampRest(reply, m_restOp, path, body, true, Post);
}

void DiscordClient::restPut(const QString &path) {
    QUrl url(QString("https://discord.com/api%1").arg(path));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");
    req.setRawHeader("Content-Type", "application/json");
    QNetworkReply *reply = m_nam.put(req, QByteArray());
    stampRest(reply, m_restOp, path, QJsonObject(), false, Put);
}

void DiscordClient::restDelete(const QString &path) {
    QUrl url(QString("https://discord.com/api%1").arg(path));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");
    QNetworkReply *reply = m_nam.deleteResource(req);
    stampRest(reply, m_restOp, path, QJsonObject(), false, Delete);
}

void DiscordClient::restPatch(const QString &path, const QJsonObject &body) {
    QUrl url(QString("https://discord.com/api%1").arg(path));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");
    req.setRawHeader("Content-Type", "application/json");
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_nam.sendCustomRequest(req, "PATCH", data);
    stampRest(reply, m_restOp, path, body, true, Patch);
}

void DiscordClient::sendMessage(const QString &channelId, const QString &content) {
    if (content.trimmed().isEmpty())
        return;
    m_restOp = SendMessage;
    restPost(QString("/channels/%1/messages").arg(channelId), QJsonObject{{"content", content}});
}

void DiscordClient::sendFile(const QString &channelId, const QString &filePath, const QString &content) {
    if (filePath.isEmpty()) {
        log("sendFile: empty file path");
        return;
    }
    QFile *file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        log("sendFile: cannot open %s", qPrintable(filePath));
        delete file;
        return;
    }
    m_restOp = SendFile;
    QUrl url(QString("https://discord.com/api/channels/%1/messages").arg(channelId));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QJsonObject payload;
    if (!content.isEmpty())
        payload["content"] = content;
    QJsonArray attachments;
    QJsonObject att;
    att["id"] = 0;
    att["filename"] = QFileInfo(filePath).fileName();
    attachments.append(att);
    payload["attachments"] = attachments;

    QHttpPart jsonPart;
    jsonPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"payload_json\"").toLatin1());
    jsonPart.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    jsonPart.setBody(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    multiPart->append(jsonPart);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QString("form-data; name=\"files[0]\"; filename=\"%1\"").arg(QFileInfo(filePath).fileName()).toLatin1());
    QMimeDatabase mimeDb;
    QString mimeType = mimeDb.mimeTypeForFile(filePath).name();
    if (mimeType.isEmpty())
        mimeType = QStringLiteral("application/octet-stream");
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType.toLatin1());
    filePart.setBodyDevice(file);
    multiPart->append(filePart);

    QNetworkReply *reply = m_nam.post(req, multiPart);
    multiPart->setParent(reply);
    file->setParent(reply);
    stampRest(reply, SendFile, QString("/channels/%1/messages").arg(channelId),
              QJsonObject(), true, Post);
}

void DiscordClient::selectChannel(const QString &channelId) {
    m_autoSelecting = false;
    if (channelId == m_currentChannelId)
        return;
    if (m_unread.remove(channelId) > 0)
        emit unreadChanged();
    m_currentChannelId = channelId;
    emit currentChannelIdChanged();
    updateCurrentChannelIsDm();
    if (!m_pins.isEmpty()) {
        m_pins.clear();
        emit pinsChanged();
    }
    recomputeTyping();
    if (!m_messageCache.contains(channelId)) {
        m_restOp = ChannelMessages;
        restGet(QString("/channels/%1/messages?limit=25").arg(channelId));
    } else {
        m_activeMessages = activeMessagesFrom(channelId);
        emit activeMessagesChanged();
    }
}

void DiscordClient::updateCurrentChannelIsDm() {
    bool isDm = false;
    for (const QVariant &v : m_dmChannels) {
        QVariantMap m = v.toMap();
        if (m.value("id").toString() == m_currentChannelId) {
            isDm = true;
            break;
        }
    }
    if (m_currentChannelIsDm != isDm) {
        m_currentChannelIsDm = isDm;
        emit currentChannelIsDmChanged();
    }
}

void DiscordClient::refreshDms() {
    m_restOp = DmChannels;
    restGet("/users/@me/channels");
}

void DiscordClient::openDm(const QString &userId) {
    m_restOp = OpenDm;
    restPost("/users/@me/channels", QJsonObject{{"recipient_id", userId}});
}

void DiscordClient::setTheme(const QString &t) {
    if (t != "light" && t != "dark")
        return;
    if (m_theme == t)
        return;
    m_theme = t;
    QSettings("QtDiscord", "QtDiscord").setValue("theme", t);
    emit themeChanged();
}

void DiscordClient::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

QStringList DiscordClient::unreadChannelIds() const {
    QStringList out;
    out.reserve(m_unread.size());
    for (const QString &id : m_unread)
        out.append(id);
    return out;
}

void DiscordClient::setStatus(const QString &s) {
    if (m_status == s)
        return;
    m_status = s;
    emit statusChanged();
}

QVariantList DiscordClient::activeMessagesFrom(const QString &channelId) const {
    QVariantList out;
    auto it = m_messageCache.constFind(channelId);
    if (it == m_messageCache.constEnd())
        return out;
    for (int i = it.value().size() - 1; i >= 0; --i)
        out.append(it.value()[i].toObject().toVariantMap());
    return out;
}

quint64 DiscordClient::snowflake(const QString &id) {
    return id.toULongLong();
}

void DiscordClient::normalizeMessages(QJsonArray &arr) const {
    // Dedup by message id (keep first occurrence).
    QStringList seen;
    for (int i = 0; i < arr.size(); ) {
        const QString id = arr[i].toObject().value("id").toString();
        if (seen.contains(id))
            arr.removeAt(i);
        else {
            seen.append(id);
            ++i;
        }
    }
    // Stable sort by snowflake id descending (newest first).
    std::vector<QJsonValue> vec;
    vec.reserve(arr.size());
    for (const QJsonValue &v : arr)
        vec.push_back(v);
    std::stable_sort(vec.begin(), vec.end(),
                     [](const QJsonValue &a, const QJsonValue &b) {
                         return snowflake(a.toObject().value("id").toString())
                              > snowflake(b.toObject().value("id").toString());
                     });
    arr = QJsonArray();
    for (const QJsonValue &v : vec)
        arr.append(v);
}

void DiscordClient::onRestReply(QNetworkReply *reply) {
    const QByteArray data = reply->readAll();
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const RestOp op = static_cast<RestOp>(reply->property("restOp").toInt());
    const QString path = reply->property("restPath").toString();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        log("REST op=%d err=%d code=%d %s", (int)op, (int)reply->error(), http,
            qPrintable(reply->errorString()));
        if (op == ChannelMessages && m_autoSelecting) {
            log("auto-select: skipping unreadable channel %s (code %d)",
                qPrintable(m_currentChannelId), http);
            fetchChannelAt(m_textIndex + 1);
            return;
        }
        if (op == DmCall) {
            log("ring POST failed code=%d %s", http, qPrintable(reply->errorString()));
            setStatus(QStringLiteral("Call joined; ring notification failed (HTTP %1).").arg(http));
            m_restOp = None;
            return;
        }
        setStatus(QString("REST error: %1").arg(reply->errorString()));
        emit errorOccurred(reply->errorString());
        return;
    }
    if (http < 200 || http >= 300) {
        log("REST op=%d http=%d", (int)op, http);
        if (http == 429) {
        if (op == SendFile) {
            log("REST op=%d http=429 (multipart, no retry)", (int)op);
            setStatus("File upload rate limited.");
            return;
        }
            int retryAfter = 5000;
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull())
                retryAfter = doc.object().value("retry_after").toInt(retryAfter);
            if (retryAfter < 1000)
                retryAfter = 1000;
            m_retryPath = path;
            m_retryBody = QJsonDocument::fromJson(reply->property("restBody").toByteArray()).object();
            m_retryIsPost = reply->property("restIsPost").toBool();
            m_retryMethod = static_cast<RestMethod>(reply->property("restMethod").toInt());
            m_retryOp = op;
            setStatus(QString("Rate limited, retrying in %1 ms").arg(retryAfter));
            m_retryTimer.start(retryAfter);
            return;
        }
        if (op == ChannelMessages && m_autoSelecting) {
            fetchChannelAt(m_textIndex + 1);
            return;
        }
        setStatus(QString("REST HTTP %1").arg(http));
        emit errorOccurred(QString("HTTP %1").arg(http));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        log("REST op=%d invalid json", (int)op);
        return;
    }

    switch (op) {
    case Me: {
        m_user = doc.object();
        m_selfId = m_user.value("id").toString();
        rememberUser(m_user);
        emit userChanged();
        log("logged in as %s (id=%s)", qPrintable(m_user.value("username").toString()), qPrintable(m_selfId));
        setStatus("Fetching guilds...");
        m_restOp = MyGuilds;
        restGet("/users/@me/guilds");
        // Start the gateway now that auth succeeded — independent of REST bootstrap,
        // so realtime events flow even if a later channel/message fetch fails.
        if (!m_gatewayStarted) {
            m_gatewayStarted = true;
            connectGateway();
        }
        break;
    }
    case MyGuilds: {
        QJsonArray arr = doc.array();
        QVariantList list;
        for (const QJsonValue &v : arr)
            list.append(v.toObject().toVariantMap());
        m_guilds = list;
        emit guildsChanged();
        log("guilds=%d", arr.size());
        if (arr.isEmpty()) {
            setStatus("No guilds accessible.");
            emit errorOccurred("No guilds accessible by this bot token.");
            return;
        }
        m_currentGuildId = arr[0].toObject().value("id").toString();
        emit currentGuildIdChanged();
        fetchGuildChannels(m_currentGuildId);
        break;
    }
    case GuildChannels: {
        QJsonArray arr = doc.array();
        QHash<QString, QJsonObject> categories;
        QHash<QString, QJsonArray> children;
        QJsonArray uncategorized;
        for (const QJsonValue &v : arr) {
            QJsonObject o = v.toObject();
            int type = o.value("type").toInt();
            if (type != 0 && type != 2 && type != 4)
                continue;
            if (type == 4) {
                categories.insert(o.value("id").toString(), o);
            } else {
                QString pid = o.value("parent_id").toString();
                if (pid.isEmpty())
                    uncategorized.append(o);
                else
                    children[pid].append(o);
            }
        }
        QList<QJsonObject> catList = categories.values();
        std::sort(catList.begin(), catList.end(),
                  [](const QJsonObject &a, const QJsonObject &b) {
                      return channelPosition(a) < channelPosition(b);
                  });
        QVariantList list;
        for (const QJsonObject &cat : catList) {
            list.append(cat.toVariantMap());
            QString cid = cat.value("id").toString();
            QJsonArray ch = sortChannels(children.value(cid));
            for (const QJsonValue &cv : ch)
                list.append(cv.toObject().toVariantMap());
        }
        QJsonArray uncat = uncategorized;
        for (auto it = children.constBegin(); it != children.constEnd(); ++it) {
            if (!categories.contains(it.key())) {
                for (const QJsonValue &cv : it.value())
                    uncat.append(cv);
            }
        }
        uncat = sortChannels(uncat);
        for (const QJsonValue &cv : uncat)
            list.append(cv.toObject().toVariantMap());
        m_channels = list;
        emit channelsChanged();
        log("channels=%d", list.size());
        m_textChannels.clear();
        for (const QVariant &v : list) {
            QVariantMap m = v.toMap();
            if (m.value("type").toInt() == 0)
                m_textChannels.append(m.value("id").toString());
        }
        if (m_textChannels.isEmpty()) {
            setStatus("No text channels.");
            break;
        }
        m_autoSelecting = true;
        fetchChannelAt(0);
        break;
    }
    case ChannelMessages: {
        QJsonArray arr = doc.array();
        rememberMessageUsers(arr);
        normalizeMessages(arr);
        QString cid = m_currentChannelId;
        const int cs = path.indexOf("/channels/");
        if (cs >= 0) {
            const int from = cs + 10;
            const int to = path.indexOf('/', from);
            if (to > from)
                cid = path.mid(from, to - from);
        }
        m_messageCache[cid] = arr;
        log("messages=%d in %s", arr.size(), qPrintable(cid));
        if (cid != m_currentChannelId)
            break;
        m_messages = arr.toVariantList();
        emit messagesChanged();
        m_activeMessages = activeMessagesFrom(cid);
        emit activeMessagesChanged();
        setStatus("Connected.");
        m_autoSelecting = false;
        if (!m_gatewayStarted) {
            m_gatewayStarted = true;
            connectGateway();
        }
        break;
    }
    case ChannelMessagesMore: {
        QJsonArray arr = doc.array();
        rememberMessageUsers(arr);
        QJsonArray existing = m_messageCache.value(m_currentChannelId);
        for (int i = arr.size() - 1; i >= 0; --i)
            existing.prepend(arr[i]);
        normalizeMessages(existing);
        m_messageCache[m_currentChannelId] = existing;
        m_messages = existing.toVariantList();
        emit messagesChanged();
        m_activeMessages = activeMessagesFrom(m_currentChannelId);
        emit activeMessagesChanged();
        log("loaded %d older messages", arr.size());
        m_restOp = None;
        break;
    }
    case DmChannels: {
        QJsonArray arr = doc.array();
        QVariantList list;
        for (const QJsonValue &v : arr) {
            QJsonObject o = v.toObject();
            int type = o.value("type").toInt();
            if (type != 1 && type != 3)
                continue;
            QVariantMap m;
            m["id"] = o.value("id").toString();
            m["type"] = type;
            QString name = o.value("name").toString();
            if (name.isEmpty()) {
                QJsonArray recipients = o.value("recipients").toArray();
                QStringList names;
                for (const QJsonValue &rv : recipients) {
                    QJsonObject ru = rv.toObject();
                    rememberUser(ru);
                    QString rn = ru.value("global_name").toString();
                    if (rn.isEmpty())
                        rn = ru.value("username").toString();
                    if (!rn.isEmpty())
                        names.append(rn);
                }
                if (type == 1 && !names.isEmpty())
                    name = names.first();
                else if (type == 3)
                    name = names.join(QStringLiteral(", "));
            }
            m["name"] = name;
            m["recipients"] = o.value("recipients").toArray().toVariantList();
            list.append(m);
        }
        m_dmChannels = list;
        emit dmChannelsChanged();
        updateCurrentChannelIsDm();
        m_restOp = None;
        log("dms=%d", list.size());
        break;
    }
    case OpenDm: {
        QJsonObject created = doc.object();
        log("dm opened id=%s", qPrintable(created.value("id").toString()));
        setStatus("DM opened.");
        m_restOp = None;
        refreshDms();
        break;
    }
    case DmCall: {
        log("dm call started channel=%s", qPrintable(m_pendingVoiceChannel));
        setStatus("Call connected.");
        m_restOp = None;
        break;
    }
    case FetchRegions: {
        QVariantList list;
        QJsonArray arr = doc.array();
        for (const QJsonValue &v : arr) {
            QJsonObject o = v.toObject();
            QVariantMap m;
            m["id"] = o.value("id").toString();
            m["name"] = o.value("name").toString();
            m["optimal"] = o.value("optimal").toBool();
            m["deprecated"] = o.value("deprecated").toBool();
            m["custom"] = o.value("custom").toBool();
            m["vip"] = o.value("vip").toBool();
            list.append(m);
        }
        m_regions = list;
        emit regionsChanged();
        log("voice regions=%d", list.size());
        m_restOp = None;
        break;
    }
    case GuildMembers: {
        QJsonArray arr = doc.array();
        QVariantList list;
        for (const QJsonValue &v : arr)
            list.append(memberMap(v.toObject(), m_presence));
        m_members = list;
        emit membersChanged();
        log("members=%d", arr.size());
        m_restOp = None;
        break;
    }
    case SendMessage: {
        QJsonObject created = doc.object();
        log("message sent id=%s", qPrintable(created.value("id").toString()));
        setStatus("Message sent.");
        m_restOp = None;
        break;
    }
    case ReactionAdd: {
        log("reaction added");
        setStatus("Reaction added.");
        m_restOp = None;
        break;
    }
    case ReactionRemove: {
        log("reaction removed");
        setStatus("Reaction removed.");
        m_restOp = None;
        break;
    }
    case SendFile: {
        log("file upload complete");
        setStatus("File uploaded.");
        m_restOp = None;
        break;
    }
    case ChannelPins: {
        QJsonArray arr = doc.array();
        normalizeMessages(arr);
        m_pins = arr.toVariantList();
        emit pinsChanged();
        m_restOp = None;
        break;
    }
    default:
        break;
    }
}

QString DiscordClient::reactionKey(const QJsonObject &emoji) {
    QString id = emoji.value("id").toString();
    if (!id.isEmpty())
        return id;
    return emoji.value("name").toString();
}

void DiscordClient::updateReaction(const QString &channelId, const QString &messageId, const QJsonObject &emoji, int delta, bool self) {
    auto it = m_messageCache.find(channelId);
    if (it == m_messageCache.end())
        return;
    QJsonArray arr = it.value();
    QString key = reactionKey(emoji);
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject msg = arr[i].toObject();
        if (msg.value("id").toString() != messageId)
            continue;
        QJsonArray reactions = msg.value("reactions").toArray();
        for (int j = 0; j < reactions.size(); ++j) {
            QJsonObject entry = reactions[j].toObject();
            if (reactionKey(entry.value("emoji").toObject()) != key)
                continue;
            int count = entry.value("count").toInt() + delta;
            entry["count"] = count;
            entry["me"] = self;
            if (count <= 0)
                reactions.removeAt(j);
            else
                reactions[j] = entry;
            break;
        }
        msg["reactions"] = reactions;
        if (reactions.isEmpty())
            msg.remove("reactions");
        arr[i] = msg;
        m_messageCache[channelId] = arr;
        break;
    }
}

void DiscordClient::addReaction(const QString &channelId, const QString &messageId, const QString &emoji) {
    m_restOp = ReactionAdd;
    restPut(QString("/channels/%1/messages/%2/reactions/%3/@me")
            .arg(channelId, messageId, QString::fromLatin1(QUrl::toPercentEncoding(emoji))));
}

void DiscordClient::triggerTyping(const QString &channelId) {
    if (channelId.isEmpty())
        return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 last = m_lastTyping.value(channelId, 0);
    if (now - last < 5000)
        return;
    m_lastTyping[channelId] = now;
    restPost(QString("/channels/%1/typing").arg(channelId), QJsonObject());
}

void DiscordClient::removeReaction(const QString &channelId, const QString &messageId, const QString &emoji) {
    m_restOp = ReactionRemove;
    restDelete(QString("/channels/%1/messages/%2/reactions/%3/@me")
               .arg(channelId, messageId, QString::fromLatin1(QUrl::toPercentEncoding(emoji))));
}

void DiscordClient::onConnected() {
    log("WS connected");
    QByteArray key = QUuid::createUuid().toString().toLatin1();
    key.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    QByteArray hash = QCryptographicHash::hash(key, QCryptographicHash::Sha1);
    QByteArray b64 = hash.toBase64();

    QByteArray req;
    req += "GET " + m_handshakePath.toLatin1() + " HTTP/1.1\r\n";
    req += "Host: " + m_handshakeHost.toLatin1() + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + b64 + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "User-Agent: QtDiscord/0.1\r\n";
    req += "\r\n";
    m_sock.write(req);
    m_inHttp = true;
}

void DiscordClient::onDisconnected() {
    log("WS disconnected");
    m_hbTimer.stop();
    m_wantResume = m_resumable;
    setStatus("Disconnected");
    emit errorOccurred("WebSocket disconnected");
    if (!m_fatalClose)
        scheduleReconnect();
}

void DiscordClient::onReadyRead() {
    m_buffer.append(m_sock.readAll());
    if (m_inHttp) {
        processHttp();
    } else {
        processFrames();
    }
}

void DiscordClient::processHttp() {
    int idx = m_buffer.indexOf("\r\n\r\n");
    if (idx < 0)
        return;
    QByteArray head = m_buffer.mid(0, idx);
    m_buffer.remove(0, idx + 4);
    QStringList lines = QString::fromLatin1(head).split("\r\n");
    QString status = lines.isEmpty() ? QString() : lines.first();
    log("WS handshake: %s", qPrintable(status));
    if (!status.contains("101")) {
        setStatus("WebSocket handshake failed");
        emit errorOccurred(QString("WS handshake failed: %1").arg(status));
        m_sock.close();
        return;
    }
    m_inHttp = false;
    log("WebSocket established");
    if (m_wantResume)
        sendResume();
    else
        identify();
    m_hbTimer.start(1000);
}

void DiscordClient::processFrames() {
    while (m_buffer.size() >= 2) {
        quint8 b0 = (quint8)m_buffer[0];
        quint8 b1 = (quint8)m_buffer[1];
        int opcode = b0 & 0x0F;
        bool masked = b1 & 0x80;
        quint64 len = b1 & 0x7F;
        int offset = 2;
        if (len == 126) {
            if (m_buffer.size() < 4)
                return;
            len = ((quint64)(quint8)m_buffer[2] << 8) | (quint8)m_buffer[3];
            offset = 4;
        } else if (len == 127) {
            if (m_buffer.size() < 10)
                return;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | (quint8)m_buffer[2 + i];
            offset = 10;
        }
        QByteArray maskKey;
        if (masked) {
            if (m_buffer.size() < offset + 4)
                return;
            maskKey = m_buffer.mid(offset, 4);
            offset += 4;
        }
        if (m_buffer.size() < offset + len)
            return;

        QByteArray payload = m_buffer.mid(offset, len);
        m_buffer.remove(0, offset + len);
        if (masked) {
            for (quint64 i = 0; i < len; ++i)
                payload[i] ^= maskKey[i % 4];
        }

        handleFrame(opcode, true, payload);
    }
}

void DiscordClient::handleFrame(int opcode, bool /*fin*/, const QByteArray &payload) {
    if (opcode == 0x8) {
        m_hbTimer.stop();
        quint16 code = 0;
        if (payload.size() >= 2)
            code = (quint16)((quint8)payload[0] << 8 | (quint8)payload[1]);
        static const quint16 fatal[] = { 4002, 4004, 4010, 4011, 4012, 4013, 4014 };
        bool fatalClose = false;
        for (quint16 c : fatal) {
            if (c == code) {
                fatalClose = true;
                break;
            }
        }
        if (fatalClose) {
            m_fatalClose = true;
            log("Fatal close code %u; not reconnecting", (unsigned)code);
            setStatus("Session invalid");
            emit errorOccurred(QString("WebSocket closed: %1").arg(code));
            return;
        }
        log("Close code %u; resuming", (unsigned)code);
        m_wantResume = m_resumable;
        m_sock.close();
        return;
    }
    if (opcode == 0x9) { // ping
        sendOp(0xA, QJsonObject());
        return;
    }
    if (opcode == 0xA) // pong
        return;
    if (opcode != 0x1)
        return;

    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isNull())
        return;
    QJsonObject obj = doc.object();
    int op = obj.value("op").toInt();
    QString t = obj.value("t").toString();
    QJsonValue s = obj.value("s");
    QJsonValue d = obj.value("d");

    // Track last sequence for resume.
    if (!s.isNull() && s.isDouble())
        m_lastSeq = s.toInt();

    if (op == 10) {
        int interval = d.toObject().value("heartbeat_interval").toInt();
        log("Hello; heartbeat %d ms", interval);
        m_hbTimer.start(interval);
        m_lastHeartbeat = QDateTime::currentMSecsSinceEpoch();
        m_lastHeartbeatAck = QDateTime::currentMSecsSinceEpoch();
        return;
    }
    if (op == 11) {
        m_lastHeartbeatAck = QDateTime::currentMSecsSinceEpoch();
        if (m_voiceLatencyStart > 0) {
            int lat = (int)(m_lastHeartbeatAck - m_voiceLatencyStart);
            if (lat != m_voiceLatency) {
                m_voiceLatency = lat;
                emit voiceLatencyChanged();
            }
        }
        return;
    }
    if (op == 1) {
        sendOp(1, QJsonObject());
        return;
    }
    if (op == 9) {
        setStatus("Invalid session");
        emit errorOccurred("Invalid session");
        m_wantResume = false;
        m_sock.close();
        return;
    }
    if (op == 7) {
        log("Reconnect requested");
        m_wantResume = m_resumable;
        m_sock.close();
        return;
    }
    if (op != 0)
        return;

    if (t == "READY") {
        m_sessionId = d.toObject().value("session_id").toString();
        m_resumable = !m_sessionId.isEmpty();
        m_wantResume = false;
        m_reconnectAttempt = 0;
        log("READY; session=%s resumable=%d", qPrintable(m_sessionId), m_resumable ? 1 : 0);
        return;
    }
    if (t == "RESUMED") {
        m_reconnectAttempt = 0;
        log("RESUMED");
        setStatus("Resumed.");
        return;
    }
    if (t == "MESSAGE_CREATE") {
        QJsonObject msg = d.toObject();
        QString cid = msg.value("channel_id").toString();
        log("MESSAGE_CREATE channel=%s", qPrintable(cid));
        QJsonObject author = msg.value("author").toObject();
        rememberUser(author);
        const QJsonArray mentions = msg.value("mentions").toArray();
        for (const QJsonValue &mv : mentions)
            rememberUser(mv.toObject());
        QString authorId = author.value("id").toString();
        if (authorId != m_selfId) {
            if (cid == m_currentChannelId) {
                if (m_unread.remove(cid) > 0)
                    emit unreadChanged();
            } else if (!m_unread.contains(cid)) {
                m_unread.insert(cid);
                emit unreadChanged();
            }
            QString an = author.value("global_name").toString();
            if (an.isEmpty())
                an = author.value("username").toString();
            emit messageReceived(an, msg.value("content").toString(), cid);
        }
        if (cid == m_currentChannelId) {
            QJsonArray arr = m_messageCache.value(m_currentChannelId);
            arr.prepend(msg);
            normalizeMessages(arr);
            m_messageCache[m_currentChannelId] = arr;
            m_messages = arr.toVariantList();
            emit messagesChanged();
            m_activeMessages = activeMessagesFrom(m_currentChannelId);
            emit activeMessagesChanged();
        }
        return;
    }
    if (t == "MESSAGE_DELETE") {
        QString mid = d.toObject().value("id").toString();
        QString cid = d.toObject().value("channel_id").toString();
        log("MESSAGE_DELETE id=%s channel=%s", qPrintable(mid), qPrintable(cid));
        if (cid == m_currentChannelId) {
            QJsonArray arr = m_messageCache.value(m_currentChannelId);
            for (int i = 0; i < arr.size(); ++i) {
                if (arr[i].toObject().value("id").toString() == mid) {
                    arr.removeAt(i);
                    break;
                }
            }
            m_messageCache[m_currentChannelId] = arr;
            m_messages = arr.toVariantList();
            emit messagesChanged();
            m_activeMessages = activeMessagesFrom(m_currentChannelId);
            emit activeMessagesChanged();
        }
        return;
    }
    if (t == "MESSAGE_UPDATE") {
        QJsonObject msg = d.toObject();
        QString mid = msg.value("id").toString();
        QString cid = msg.value("channel_id").toString();
        log("MESSAGE_UPDATE id=%s channel=%s", qPrintable(mid), qPrintable(cid));
        if (cid == m_currentChannelId) {
            QJsonArray arr = m_messageCache.value(m_currentChannelId);
            for (int i = 0; i < arr.size(); ++i) {
                if (arr[i].toObject().value("id").toString() == mid) {
                    arr[i] = msg;
                    break;
                }
            }
            normalizeMessages(arr);
            m_messageCache[m_currentChannelId] = arr;
            m_messages = arr.toVariantList();
            emit messagesChanged();
            m_activeMessages = activeMessagesFrom(m_currentChannelId);
            emit activeMessagesChanged();
        }
        return;
    }
    if (t == "MESSAGE_REACTION_ADD") {
        QJsonObject ev = d.toObject();
        QString cid = ev.value("channel_id").toString();
        QString mid = ev.value("message_id").toString();
        QJsonObject emoji = ev.value("emoji").toObject();
        QString uid = ev.value("user_id").toString();
        log("MESSAGE_REACTION_ADD channel=%s message=%s", qPrintable(cid), qPrintable(mid));
        if (cid == m_currentChannelId) {
            updateReaction(cid, mid, emoji, +1, uid == m_selfId);
            QJsonArray arr = m_messageCache.value(m_currentChannelId);
            m_messages = arr.toVariantList();
            emit messagesChanged();
            m_activeMessages = activeMessagesFrom(m_currentChannelId);
            emit activeMessagesChanged();
        }
        return;
    }
    if (t == "MESSAGE_REACTION_REMOVE") {
        QJsonObject ev = d.toObject();
        QString cid = ev.value("channel_id").toString();
        QString mid = ev.value("message_id").toString();
        QJsonObject emoji = ev.value("emoji").toObject();
        QString uid = ev.value("user_id").toString();
        log("MESSAGE_REACTION_REMOVE channel=%s message=%s", qPrintable(cid), qPrintable(mid));
        if (cid == m_currentChannelId) {
            updateReaction(cid, mid, emoji, -1, uid == m_selfId);
            QJsonArray arr = m_messageCache.value(m_currentChannelId);
            m_messages = arr.toVariantList();
            emit messagesChanged();
            m_activeMessages = activeMessagesFrom(m_currentChannelId);
            emit activeMessagesChanged();
        }
        return;
    }
    if (t == "MESSAGE_REACTION_REMOVE_ALL") {
        QJsonObject ev = d.toObject();
        QString cid = ev.value("channel_id").toString();
        QString mid = ev.value("message_id").toString();
        log("MESSAGE_REACTION_REMOVE_ALL channel=%s message=%s", qPrintable(cid), qPrintable(mid));
        if (cid == m_currentChannelId) {
            QJsonArray arr = m_messageCache.value(m_currentChannelId);
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject msg = arr[i].toObject();
                if (msg.value("id").toString() != mid)
                    continue;
                msg.remove("reactions");
                arr[i] = msg;
                break;
            }
            m_messageCache[m_currentChannelId] = arr;
            m_messages = arr.toVariantList();
            emit messagesChanged();
            m_activeMessages = activeMessagesFrom(m_currentChannelId);
            emit activeMessagesChanged();
        }
        return;
    }
    if (t == "TYPING_START") {
        QJsonObject ev = d.toObject();
        QString channelId = ev.value("channel_id").toString();
        QString userId = ev.value("user_id").toString();
        QString name;
        QJsonObject user = ev.value("user").toObject();
        if (!user.isEmpty()) {
            name = user.value("global_name").toString();
            if (name.isEmpty())
                name = user.value("username").toString();
        }
        if (name.isEmpty()) {
            QJsonObject member = ev.value("member").toObject();
            QJsonObject muser = member.value("user").toObject();
            if (!muser.isEmpty()) {
                name = muser.value("global_name").toString();
                if (name.isEmpty())
                    name = muser.value("username").toString();
            }
        }
        if (name.isEmpty())
            name = QStringLiteral("someone");
        m_typing[channelId][userId] = QDateTime::currentMSecsSinceEpoch();
        m_userNames[userId] = name;
        if (channelId == m_currentChannelId)
            recomputeTyping();
        m_typingTimer.start(8500);
        log("TYPING_START channel=%s user=%s (%s)", qPrintable(channelId), qPrintable(userId), qPrintable(name));
        return;
    }
    if (t == "GUILD_MEMBER_ADD") {
        QJsonObject md = d.toObject();
        QJsonObject user = md.value("user").toObject();
        QString id = user.value("id").toString();
        if (memberIndex(m_members, id) < 0)
            m_members.append(memberMap(md, m_presence));
        emit membersChanged();
        log("GUILD_MEMBER_ADD id=%s", qPrintable(id));
        return;
    }
    if (t == "GUILD_MEMBER_UPDATE") {
        QJsonObject md = d.toObject();
        QJsonObject user = md.value("user").toObject();
        QString id = user.value("id").toString();
        int i = memberIndex(m_members, id);
        if (i >= 0) {
            QVariantMap m = m_members[i].toMap();
            m["nick"] = md.value("nick").toString();
            m["roles"] = md.value("roles").toArray().toVariantList();
            m_members[i] = m;
            emit membersChanged();
        }
        log("GUILD_MEMBER_UPDATE id=%s", qPrintable(id));
        return;
    }
    if (t == "GUILD_MEMBER_REMOVE") {
        QJsonObject md = d.toObject();
        QJsonObject user = md.value("user").toObject();
        QString id = user.value("id").toString();
        int i = memberIndex(m_members, id);
        if (i >= 0) {
            m_members.removeAt(i);
            emit membersChanged();
        }
        log("GUILD_MEMBER_REMOVE id=%s", qPrintable(id));
        return;
    }
    if (t == "PRESENCE_UPDATE") {
        QJsonObject md = d.toObject();
        QJsonObject user = md.value("user").toObject();
        QString id = user.value("id").toString();
        QString status = md.value("status").toString();
        m_presence[id] = status;
        int i = memberIndex(m_members, id);
        if (i >= 0) {
            QVariantMap m = m_members[i].toMap();
            m["status"] = status;
            m_members[i] = m;
            emit membersChanged();
        }
        log("PRESENCE_UPDATE id=%s status=%s", qPrintable(id), qPrintable(status));
        return;
    }
    if (t == "MESSAGE_REACTION_REMOVE_EMOJI") {
        QJsonObject ev = d.toObject();
        QString cid = ev.value("channel_id").toString();
        QString mid = ev.value("message_id").toString();
        QJsonObject emoji = ev.value("emoji").toObject();
        log("MESSAGE_REACTION_REMOVE_EMOJI channel=%s message=%s", qPrintable(cid), qPrintable(mid));
        if (cid == m_currentChannelId) {
            QString key = reactionKey(emoji);
            QJsonArray arr = m_messageCache.value(m_currentChannelId);
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject msg = arr[i].toObject();
                if (msg.value("id").toString() != mid)
                    continue;
                QJsonArray reactions = msg.value("reactions").toArray();
                for (int j = 0; j < reactions.size(); ++j) {
                    if (reactionKey(reactions[j].toObject().value("emoji").toObject()) != key)
                        continue;
                    reactions.removeAt(j);
                    break;
                }
                msg["reactions"] = reactions;
                if (reactions.isEmpty())
                    msg.remove("reactions");
                arr[i] = msg;
                break;
            }
            m_messageCache[m_currentChannelId] = arr;
            m_messages = arr.toVariantList();
            emit messagesChanged();
            m_activeMessages = activeMessagesFrom(m_currentChannelId);
            emit activeMessagesChanged();
        }
        return;
    }
    if (t == "GUILD_CREATE") {
        QJsonObject ev = d.toObject();
        QString gid = ev.value("id").toString();
        const QJsonArray vs = ev.value("voice_states").toArray();
        for (const QJsonValue &v : vs) {
            QJsonObject st = v.toObject();
            if (!st.contains(QStringLiteral("guild_id")))
                st[QStringLiteral("guild_id")] = gid;
            storeVoiceState(st);
        }
        rebuildVoiceParticipants();
        log("GUILD_CREATE guild=%s voice_states=%d", qPrintable(gid), vs.size());
        return;
    }
    if (t == "GUILD_DELETE") {
        QJsonObject ev = d.toObject();
        QString gid = ev.value("id").toString();
        for (auto it = m_voiceStates.begin(); it != m_voiceStates.end();) {
            if (it.value().value("guildId").toString() == gid)
                it = m_voiceStates.erase(it);
            else
                ++it;
        }
        rebuildVoiceParticipants();
        log("GUILD_DELETE guild=%s", qPrintable(gid));
        return;
    }
    if (t == "CALL_CREATE") {
        QJsonObject ev = d.toObject();
        QString cid = ev.value("channel_id").toString();
        QStringList ringingUsers = ev.value("ongoing_rings").toObject().keys();
        log("CALL_CREATE channel=%s ringing=%d", qPrintable(cid), ringingUsers.size());
        const QJsonArray states = ev.value("voice_states").toArray();
        for (const QJsonValue &v : states)
            storeVoiceState(v.toObject());
        if (ringingUsers.contains(m_selfId)) {
            m_pendingVoiceGuild.clear();
            m_pendingVoiceChannel = cid;
            playTone(880, 180);
        }
        emit dmCallCreated(cid, ringingUsers);
        return;
    }
    if (t == "CALL_UPDATE") {
        QJsonObject ev = d.toObject();
        QString cid = ev.value("channel_id").toString();
        log("CALL_UPDATE channel=%s", qPrintable(cid));
        emit dmCallUpdated(cid);
        return;
    }
    if (t == "CALL_DELETE") {
        QJsonObject ev = d.toObject();
        QString cid = ev.value("channel_id").toString();
        log("CALL_DELETE channel=%s", qPrintable(cid));
        emit dmCallDeleted(cid);
        if (m_pendingVoiceChannel == cid)
            m_pendingVoiceChannel.clear();
        if (m_voiceChannelId == cid) {
            m_voiceChannelId.clear();
            m_voiceGuildId.clear();
            m_voice.leave();
            emit voiceChanged();
        }
        return;
    }
    if (t == "VOICE_STATE_UPDATE") {
        applyVoiceState(d.toObject());
        return;
    }
    if (t == "VOICE_SERVER_UPDATE") {
        QJsonObject ev = d.toObject();
        QString gid = ev.value("guild_id").toString();
        QString endpoint = ev.value("endpoint").toString();
        QString token = ev.value("token").toString();
        log("VOICE_SERVER_UPDATE guild=%s endpoint=%s", qPrintable(gid), qPrintable(endpoint));
        if (endpoint.isEmpty() || token.isEmpty()) {
            emit voiceError(QStringLiteral("Voice server update incomplete"));
            return;
        }
        QString serverId = gid.isEmpty() ? m_pendingVoiceChannel : gid;
        if (serverId.isEmpty()) {
            emit voiceError(QStringLiteral("Voice server update incomplete"));
            return;
        }
        if (!gid.isEmpty() && !m_pendingVoiceGuild.isEmpty() && gid != m_pendingVoiceGuild) {
            log("Ignoring stale voice server update for guild %s", qPrintable(gid));
            return;
        }
        if (m_sessionId.isEmpty()) {
            emit voiceError(QStringLiteral("Voice session not ready"));
            return;
        }

        const QString voiceServerId = gid.isEmpty() ? m_pendingVoiceChannel : gid;
        const QString voiceChannelId = m_pendingVoiceChannel.isEmpty() ? (gid.isEmpty() ? QString() : gid)
                                                                      : m_pendingVoiceChannel;

        m_voice.setMicEnabled(!m_selfMute);
        m_voice.connectVoice(endpoint, token, voiceServerId, m_selfId, m_sessionId,
                             voiceChannelId);
        return;
    }
}

void DiscordClient::joinVoice(const QString &guildId, const QString &channelId) {
    if (channelId.isEmpty()) {
        leaveVoice();
        return;
    }
    m_pendingVoiceGuild = guildId;
    m_pendingVoiceChannel = channelId;
    if (m_voiceChannelId != channelId || m_voiceGuildId != guildId) {
        m_voiceChannelId = channelId;
        m_voiceGuildId = guildId;
        emit voiceChanged();
    }
    sendVoiceState();
    setStatus(QStringLiteral("Joining voice..."));
}

void DiscordClient::leaveVoice() {
    QJsonObject d;
    if (!m_voiceGuildId.isEmpty())
        d[QStringLiteral("guild_id")] = m_voiceGuildId;
    d[QStringLiteral("channel_id")] = QJsonValue(QJsonValue::Null);
    d[QStringLiteral("self_mute")] = m_selfMute;
    d[QStringLiteral("self_deaf")] = m_selfDeaf;
    sendOp(4, d);
    m_voice.leave();
    m_voiceChannelId.clear();
    m_voiceGuildId.clear();
    m_pendingVoiceGuild.clear();
    m_pendingVoiceChannel.clear();
    m_voiceParticipants.clear();
    emit voiceParticipantsChanged();
    emit voiceChanged();
    setStatus(QStringLiteral("Left voice."));
}

void DiscordClient::fetchVoiceRegions() {
    m_restOp = FetchRegions;
    restGet("/voice/regions");
}

void DiscordClient::setVoiceRegion(const QString &regionId) {
    if (m_currentChannelId.isEmpty())
        return;
    QJsonObject d;
    d["rtc_region"] = regionId;
    restPatch(QString("/channels/%1").arg(m_currentChannelId), d);
}

void DiscordClient::measureVoiceLatency() {
    if (!m_voice.isConnected()) {
        m_voiceLatency = -1;
        emit voiceLatencyChanged();
        return;
    }
    // Force a heartbeat now to measure round-trip
    m_voiceLatencyStart = QDateTime::currentMSecsSinceEpoch();
    sendOp(1, QJsonObject());
    m_lastHeartbeat = m_voiceLatencyStart;
}

void DiscordClient::playSoundboardSound(const QString &soundId) {
    if (!m_soundboardPlayer || soundId.isEmpty())
        return;
    m_soundboardPlayer->setSource(QUrl::fromLocalFile(soundId));
    m_soundboardPlayer->play();
}

void DiscordClient::editVoiceMember(const QString &userId, bool mute, bool deaf,
                                    const QString &channelId, const QString &nick) {
    if (m_voiceGuildId.isEmpty() || userId.isEmpty())
        return;
    QJsonObject d;
    if (mute) d["mute"] = true;
    if (deaf) d["deaf"] = true;
    if (channelId.isEmpty())
        d["channel_id"] = QJsonValue::Null;
    else
        d["channel_id"] = channelId;
    if (!nick.isEmpty())
        d["nick"] = nick;
    m_restOp = MemberEdit;
    restPatch(QString("/guilds/%1/members/%2").arg(m_voiceGuildId, userId), d);
}

void DiscordClient::setSelfMute(bool mute) {
    if (m_selfMute == mute)
        return;
    m_selfMute = mute;
    emit selfMuteChanged();
    m_voice.setMicEnabled(!mute);
    playTone(400, 120);   // mute tone
    if (inVoice())
        sendVoiceState();
}

void DiscordClient::setSelfDeaf(bool deaf) {
    if (m_selfDeaf == deaf)
        return;
    m_selfDeaf = deaf;
    emit selfDeafChanged();
    playTone(300, 150);   // deaf tone
    if (inVoice())
        sendVoiceState();
}

void DiscordClient::startDmCall(const QString &channelId)
{
    if (channelId.isEmpty())
        return;
    if (m_useBotPrefix) {
        setStatus(QStringLiteral("Calls require a user token — log in with \"Use user token\" checked"));
        return;
    }
    m_pendingVoiceGuild.clear();
    m_pendingVoiceChannel = channelId;
    m_voiceGuildId.clear();
    m_voiceChannelId = channelId;
    m_voice.setStatus(QStringLiteral("Call ringing..."));
    emit voiceChanged();
    sendVoiceState();
    m_restOp = DmCall;
    restPost(QString("/channels/%1/call/ring").arg(channelId), QJsonObject());
    setStatus(QStringLiteral("Call ringing..."));
    playTone(660, 200);
    QTimer::singleShot(350, this, [this, channelId]() {
        if (m_pendingVoiceChannel == channelId && m_voiceChannelId == channelId)
            playTone(880, 200);
    });
}

void DiscordClient::stopDmRinging(const QString &channelId) {
    if (channelId.isEmpty())
        return;
    m_pendingVoiceChannel.clear();
    if (m_voiceChannelId == channelId) {
        m_voiceChannelId.clear();
        m_voiceGuildId.clear();
        m_voice.leave();
        emit voiceChanged();
    }
    m_restOp = DmCall;
    restPost(QString("/channels/%1/call/stop-ringing").arg(channelId), QJsonObject());
    setStatus(QStringLiteral("Stopped ringing."));
}

void DiscordClient::playTone(int frequencyHz, int durationMs)
{
    if (frequencyHz <= 0 || durationMs <= 0)
        return;
    const int sampleRate = 44100;
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    int frames = sampleRate * durationMs / 1000;
    QByteArray pcm;
    pcm.resize(frames * 2);
    short *samples = reinterpret_cast<short *>(pcm.data());
    const double pi = 3.14159265358979323846;
    double phase = 0.0;
    double step = 2.0 * pi * frequencyHz / sampleRate;
    const double volume = 0.12;
    for (int i = 0; i < frames; ++i) {
        samples[i] = static_cast<short>(volume * 32767.0 * qSin(phase));
        phase += step;
    }
    QAudioSink *sink = new QAudioSink(format, this);
    QBuffer *buf = new QBuffer(this);
    buf->setData(pcm);
    buf->open(QIODevice::ReadOnly);
    QObject::connect(sink, &QAudioSink::stateChanged, this, [sink, buf](QtAudio::State state) {
        if (state == QtAudio::IdleState || state == QtAudio::StoppedState) {
            sink->stop();
            sink->deleteLater();
            buf->deleteLater();
        }
    });
    sink->start(buf);
}
void DiscordClient::sendVoiceState()
{
    QString cid = m_pendingVoiceChannel.isEmpty() ? m_voiceChannelId : m_pendingVoiceChannel;
    if (cid.isEmpty())
        return;
    QString gid = m_pendingVoiceGuild.isEmpty() ? m_voiceGuildId : m_pendingVoiceGuild;
    QJsonObject d;
    d[QStringLiteral("channel_id")] = cid;
    d[QStringLiteral("self_mute")] = m_selfMute;
    d[QStringLiteral("self_deaf")] = m_selfDeaf;
    if (!gid.isEmpty())
        d[QStringLiteral("guild_id")] = gid;
    sendOp(4, d);
}

void DiscordClient::storeVoiceState(const QJsonObject &d) {
    QString uid = d.value("user_id").toString();
    QString cid = d.value("channel_id").toString();
    QString gid = d.value("guild_id").toString();
    if (uid.isEmpty())
        return;
    if (cid.isEmpty())
        m_voiceStates.remove(uid);
    else
        m_voiceStates[uid] = voiceParticipantMap(d);
    int i = memberIndex(m_members, uid);
    if (i >= 0) {
        QVariantMap m = m_members[i].toMap();
        if (cid.isEmpty())
            m.remove("voice_channel");
        else
            m["voice_channel"] = cid;
        m["voice_muted"] = d.value("mute").toBool() || d.value("self_mute").toBool();
        m["voice_deaf"] = d.value("deaf").toBool() || d.value("self_deaf").toBool();
        m_members[i] = m;
        emit membersChanged();
    } else {
        emit membersChanged();
    }
}

void DiscordClient::applyVoiceState(const QJsonObject &d) {
    QString uid = d.value("user_id").toString();
    storeVoiceState(d);
    if (uid == m_selfId) {
        QString cid = d.value("channel_id").toString();
        QString gid = d.value("guild_id").toString();
        if (cid.isEmpty()) {
            if (!m_voiceChannelId.isEmpty()) {
                m_voiceChannelId.clear();
                m_voiceGuildId.clear();
                m_pendingVoiceGuild.clear();
                m_pendingVoiceChannel.clear();
                m_voice.leave();
                emit voiceChanged();
            }
        } else if (m_voiceChannelId != cid || m_voiceGuildId != gid) {
            m_voiceChannelId = cid;
            m_voiceGuildId = gid;
            emit voiceChanged();
        }
        m_selfMute = d.value("self_mute").toBool();
        m_selfDeaf = d.value("self_deaf").toBool();
        emit selfMuteChanged();
        emit selfDeafChanged();
    }
    rebuildVoiceParticipants();
}

void DiscordClient::rebuildVoiceParticipants() {
    QVariantList list;
    const QString guild = !m_voiceGuildId.isEmpty() ? m_voiceGuildId : m_currentGuildId;
    const bool dmCall = m_voiceGuildId.isEmpty() && !m_voiceChannelId.isEmpty();
    for (auto it = m_voiceStates.constBegin(); it != m_voiceStates.constEnd(); ++it) {
        if (dmCall) {
            if (it.value().value("channelId").toString() != m_voiceChannelId)
                continue;
        } else if (!guild.isEmpty() && it.value().value("guildId").toString() != guild) {
            continue;
        }
        list.append(it.value());
    }
    m_voiceParticipants = list;
    emit voiceParticipantsChanged();
}

void DiscordClient::onHeartbeat() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    // If no ACK for 2 intervals, treat connection as dead and reconnect.
    if (m_lastHeartbeat > 0 && now - m_lastHeartbeatAck > m_hbTimer.interval() * 2) {
        log("No heartbeat ACK; reconnecting");
        m_wantResume = m_resumable;
        m_sock.close();
        return;
    }
    m_voiceLatencyStart = now;
    sendOp(1, QJsonObject());
    m_lastHeartbeat = now;
    log("Heartbeat sent");
}

void DiscordClient::onSocketError(QAbstractSocket::SocketError e) {
    log("Socket error: %d %s", (int)e, qPrintable(m_sock.errorString()));
    setStatus(m_sock.errorString());
    emit errorOccurred(m_sock.errorString());
}

void DiscordClient::onSslErrors(const QList<QSslError> &errors) {
    for (const QSslError &e : errors)
        log("SSL error: %s", qPrintable(e.errorString()));
    m_sock.ignoreSslErrors(errors);
}

void DiscordClient::sendTextFrame(const QString &text) {
    QByteArray payload = text.toUtf8();
    QByteArray frame;
    frame.append(char(0x81));
    quint64 len = payload.size();
    if (len < 126) {
        frame.append(char(0x80 | len));
    } else if (len <= 0xFFFF) {
        frame.append(char(0x80 | 126));
        frame.append(char((len >> 8) & 0xFF));
        frame.append(char(len & 0xFF));
    } else {
        frame.append(char(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.append(char((len >> (i * 8)) & 0xFF));
    }
    quint32 maskKey = 0xA5A5A5A5;
    frame.append(char((maskKey >> 24) & 0xFF));
    frame.append(char((maskKey >> 16) & 0xFF));
    frame.append(char((maskKey >> 8) & 0xFF));
    frame.append(char(maskKey & 0xFF));
    for (quint64 i = 0; i < len; ++i)
        frame.append(char(payload[i] ^ char((maskKey >> ((3 - (i % 4)) * 8)) & 0xFF)));
    m_sock.write(frame);
}

void DiscordClient::sendOp(int op, const QJsonObject &d) {
    QJsonObject msg;
    msg["op"] = op;
    msg["d"] = d;
    sendTextFrame(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void DiscordClient::identify() {
    QJsonObject d;
    d["token"] = m_token;
    d["intents"] = m_intents;
    QJsonObject props;
    props["os"] = "Qt";
    props["browser"] = "QtDiscord";
    props["device"] = "QtDiscord";
    d["properties"] = props;
    sendOp(2, d);
    log("IDENTIFY sent");
}

void DiscordClient::sendResume() {
    QJsonObject d;
    d["token"] = m_token;
    d["session_id"] = m_sessionId;
    d["seq"] = m_lastSeq;
    sendOp(6, d);
    log("RESUME sent seq=%lld", m_lastSeq);
}

QString DiscordClient::markdownToHtml(const QString &text) const {
    if (text.isEmpty())
        return QString();

    QString s = text;
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");

    QRegularExpression re(QStringLiteral("&lt;@!?(\\d+)&gt;"));
    QRegularExpressionMatchIterator it = re.globalMatch(s);
    QString out;
    int last = 0;
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        const QString id = m.captured(1);
        const QString name = m_userNames.value(id);
        out += s.mid(last, m.capturedStart() - last);
        if (!name.isEmpty())
            out += QStringLiteral("<span style=\"background-color:#3b3d44;border-radius:3px;padding:0 2px\">@") + name.toHtmlEscaped() + QStringLiteral("</span>");
        else
            out += m.captured();
        last = m.capturedEnd();
    }
    out += s.mid(last);
    s = out;

    s.replace(QRegularExpression("```([\\s\\S]*?)```"), "<pre>\\1</pre>");
    s.replace(QRegularExpression("`([^`]+)`"), "<code>\\1</code>");
    s.replace(QRegularExpression("\\*\\*([^*]+)\\*\\*"), "<b>\\1</b>");
    s.replace(QRegularExpression("__([^_]+)__"), "<u>\\1</u>");
    s.replace(QRegularExpression("\\*([^*]+)\\*"), "<i>\\1</i>");
    s.replace(QRegularExpression("_([^_]+)_"), "<i>\\1</i>");
    s.replace(QRegularExpression("~~([^~]+)~~"), "<s>\\1</s>");
    s.replace(QRegularExpression("\\|\\|([^|]+)\\|\\|"),
              "<span style=\"background-color:#202225;color:#202225\">\\1</span>");
    s.replace(QRegularExpression("(https?://[^\\s<]+)"), "<a href=\"\\1\">\\1</a>");
    return s;
}

QString DiscordClient::emojiUrl(const QString &id, bool animated) const {
    return QString("https://cdn.discordapp.com/emojis/%1.%2").arg(id, animated ? "gif" : "png");
}

QString DiscordClient::avatarUrl(const QJsonObject &user, int size) const {
    const QString id = user.value("id").toString();
    const QString hash = user.value("avatar").toString();
    if (hash.isEmpty())
        return QStringLiteral("https://cdn.discordapp.com/embed/avatars/0.png");
    return QString("https://cdn.discordapp.com/avatars/%1/%2.png?size=%3").arg(id, hash).arg(size);
}
