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
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QMimeDatabase>
#include <algorithm>
#include <vector>
#include <cstdio>

static void log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fprintf(stderr, "[discord] %s\n", buf);
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

    // Bootstrap: who am I?
    restGet("/users/@me");
    m_restOp = Me;
    setStatus("Logging in...");
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

void DiscordClient::doRetry() {
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
    QUrl gw("wss://gateway.discord.gg/?v=10&encoding=json");
    m_handshakeHost = gw.host();
    m_handshakePath = gw.path();
    if (!gw.query().isEmpty())
        m_handshakePath += "?" + gw.query();
    m_sock.connectToHostEncrypted(m_handshakeHost, gw.port(443));
    m_inHttp = true;
    setStatus("Connecting gateway...");
}

void DiscordClient::selectGuild(const QString &guildId) {
    if (guildId == m_currentGuildId)
        return;
    m_currentGuildId = guildId;
    m_currentChannelId.clear();
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

void DiscordClient::restGet(const QString &path) {
    QUrl url(QString("https://discord.com/api%1").arg(path));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");
    req.setRawHeader("Accept", "application/json");
    m_lastRestPath = path;
    m_lastRestBody = QJsonObject();
    m_lastRestIsPost = false;
    m_lastRestMethod = Get;
    m_nam.get(req);
}

void DiscordClient::restPost(const QString &path, const QJsonObject &body) {
    QUrl url(QString("https://discord.com/api%1").arg(path));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");
    req.setRawHeader("Content-Type", "application/json");
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    m_lastRestPath = path;
    m_lastRestBody = body;
    m_lastRestIsPost = true;
    m_lastRestMethod = Post;
    m_nam.post(req, data);
}

void DiscordClient::restPut(const QString &path) {
    QUrl url(QString("https://discord.com/api%1").arg(path));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");
    req.setRawHeader("Content-Type", "application/json");
    m_lastRestPath = path;
    m_lastRestBody = QJsonObject();
    m_lastRestIsPost = false;
    m_lastRestMethod = Put;
    m_nam.put(req, QByteArray());
}

void DiscordClient::restDelete(const QString &path) {
    QUrl url(QString("https://discord.com/api%1").arg(path));
    QNetworkRequest req(url);
    QString authHeader = m_useBotPrefix ? QString("Bot %1").arg(m_token) : m_token;
    req.setRawHeader("Authorization", authHeader.toLatin1());
    req.setRawHeader("User-Agent", "QtDiscord/0.1");
    m_lastRestPath = path;
    m_lastRestBody = QJsonObject();
    m_lastRestIsPost = false;
    m_lastRestMethod = Delete;
    m_nam.deleteResource(req);
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
        file->deleteLater();
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
}

void DiscordClient::selectChannel(const QString &channelId) {
    if (channelId == m_currentChannelId)
        return;
    m_currentChannelId = channelId;
    emit currentChannelIdChanged();
    m_activeMessages = activeMessagesFrom(channelId);
    emit activeMessagesChanged();
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
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        log("REST op=%d err=%d code=%d %s", (int)m_restOp, (int)reply->error(), http,
            qPrintable(reply->errorString()));
        setStatus(QString("REST error: %1").arg(reply->errorString()));
        emit errorOccurred(reply->errorString());
        return;
    }
    if (http < 200 || http >= 300) {
        log("REST op=%d http=%d", (int)m_restOp, http);
        if (http == 429) {
        if (m_restOp == SendFile) {
            log("REST op=%d http=429 (multipart, no retry)", (int)m_restOp);
            setStatus("File upload rate limited.");
            m_restOp = None;
            return;
        }
            int retryAfter = 5000;
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull())
                retryAfter = doc.object().value("retry_after").toInt(retryAfter);
            if (retryAfter < 1000)
                retryAfter = 1000;
            m_retryPath = m_lastRestPath;
            m_retryBody = m_lastRestBody;
            m_retryIsPost = m_lastRestIsPost;
            m_retryMethod = m_lastRestMethod;
            m_retryOp = m_restOp;
            m_restOp = None;
            setStatus(QString("Rate limited, retrying in %1 ms").arg(retryAfter));
            m_retryTimer.start(retryAfter);
            return;
        }
        setStatus(QString("REST HTTP %1").arg(http));
        emit errorOccurred(QString("HTTP %1").arg(http));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        log("REST op=%d invalid json", (int)m_restOp);
        return;
    }

    switch (m_restOp) {
    case Me: {
        m_user = doc.object();
        m_selfId = m_user.value("id").toString();
        emit userChanged();
        log("logged in as %s (id=%s)", qPrintable(m_user.value("username").toString()), qPrintable(m_selfId));
        setStatus("Fetching guilds...");
        m_restOp = MyGuilds;
        restGet("/users/@me/guilds");
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
        QVariantList list;
        for (const QJsonValue &v : arr) {
            QJsonObject o = v.toObject();
            if (o.value("type").toInt() == 0) // text channel only
                list.append(o.toVariantMap());
        }
        m_channels = list;
        emit channelsChanged();
        log("channels=%d", list.size());
        if (list.isEmpty()) {
            setStatus("No text channels.");
            emit errorOccurred("No text channels in guild.");
            return;
        }
        QString cid = list[0].toMap().value("id").toString();
        m_currentChannelId = cid;
        emit currentChannelIdChanged();
        m_restOp = ChannelMessages;
        restGet(QString("/channels/%1/messages?limit=25").arg(cid));
        break;
    }
    case ChannelMessages: {
        QJsonArray arr = doc.array();
        normalizeMessages(arr);
        m_messageCache[m_currentChannelId] = arr;
        m_messages = arr.toVariantList();
        emit messagesChanged();
        m_activeMessages = activeMessagesFrom(m_currentChannelId);
        emit activeMessagesChanged();
        log("messages=%d in %s", arr.size(), qPrintable(m_currentChannelId));
        setStatus("Connected.");
        m_restOp = None;
        connectGateway();
        break;
    }
    case ChannelMessagesMore: {
        QJsonArray arr = doc.array();
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
        QCoreApplication::quit();
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
        return;
    }
    if (op == 11)
        return;
    if (op == 1) {
        sendOp(1, QJsonObject());
        return;
    }
    if (op == 9) {
        setStatus("Invalid session");
        emit errorOccurred("Invalid session");
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
        log("READY; session=%s resumable=%d", qPrintable(m_sessionId), m_resumable ? 1 : 0);
        return;
    }
    if (t == "RESUMED") {
        log("RESUMED");
        setStatus("Resumed.");
        return;
    }
    if (t == "MESSAGE_CREATE") {
        QJsonObject msg = d.toObject();
        QString cid = msg.value("channel_id").toString();
        log("MESSAGE_CREATE channel=%s", qPrintable(cid));
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
