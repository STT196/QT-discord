#include "voiceclient.h"
#include "voiceaudio.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QHostAddress>
#include <QRandomGenerator>

#include <cstring>

static QByteArray encodeWsFrame(int opcode, const QByteArray &payload);

static bool parseVoiceBinary(const QByteArray &p, int *sequence, int *opcode, QByteArray *json) {
    if (p.size() < 3)
        return false;
    *sequence = (((quint8)p[0] << 8) | (quint8)p[1]);
    *opcode = (quint8)p[2];
    *json = p.mid(3);
    return true;
}

static QString preferredMode(const QStringList &modes) {
    static const QStringList prefs = {
        QStringLiteral("aead_xchacha20_poly1305_rtpsize"),
        QStringLiteral("aead_xchacha20_poly1305"),
        QStringLiteral("xsalsa20_poly1305")
    };
    for (const QString &p : prefs) {
        if (modes.contains(p))
            return p;
    }
    return QString();
}

static QByteArray makeDiscoveryPacket(quint32 ssrc) {
    QByteArray pkt(74, '\0');
    quint8 *p = (quint8 *)pkt.data();
    p[0] = 0x00; p[1] = 0x01;
    p[2] = 0x00; p[3] = 0x46;
    p[4] = (quint8)((ssrc >> 24) & 0xFF);
    p[5] = (quint8)((ssrc >> 16) & 0xFF);
    p[6] = (quint8)((ssrc >> 8) & 0xFF);
    p[7] = (quint8)(ssrc & 0xFF);
    return pkt;
}

static bool parseDiscoveryReply(const QByteArray &pkt, QString *ip, quint16 *port) {
    if (pkt.size() < 74)
        return false;
    const quint8 *p = (const quint8 *)pkt.constData();
    if (p[0] != 0x00 || p[1] != 0x02)
        return false;
    char addr[65] = {0};
    memcpy(addr, pkt.constData() + 8, 64);
    *ip = QString::fromLatin1(addr).trimmed();
    *port = (quint16)((quint8)pkt[72] << 8 | (quint8)pkt[73]);
    return true;
}

VoiceClient::VoiceClient(QObject *parent) : QObject(parent) {
    m_audio = new VoiceAudio(this);
    m_audio->loadLibraries();
    connect(m_audio, &VoiceAudio::daveKeyPackageReady, this,
            [this](const QByteArray &kp) {
                QByteArray op(1, char(26));
                sendWs(0x2, op + kp);
            });
    connect(m_audio, &VoiceAudio::daveInvalidateTransition, this, [this](int id) {
        QJsonObject d;
        d[QStringLiteral("transition_id")] = id;
        QJsonObject o;
        o[QStringLiteral("op")] = 31;
        o[QStringLiteral("d")] = d;
        sendJson(o);
    });
    connect(m_audio, &VoiceAudio::rtpPacketReady, this, [this](const QByteArray &pkt) {
        if (m_udpPort)
            m_udp.writeDatagram(pkt, QHostAddress(m_udpIp), m_udpPort);
    });
    connect(&m_sock, &QSslSocket::encrypted, this, &VoiceClient::onConnected);
    connect(&m_sock, &QSslSocket::disconnected, this, &VoiceClient::onDisconnected);
    connect(&m_sock, &QSslSocket::readyRead, this, &VoiceClient::onReadyRead);
    connect(&m_sock, SIGNAL(errorOccurred(QAbstractSocket::SocketError)), this,
            SLOT(onSocketError()));
    connect(&m_sock, &QSslSocket::sslErrors, this, &VoiceClient::onSslErrors);
    connect(&m_udp, &QUdpSocket::readyRead, this, &VoiceClient::onDiscoveryRead);
    connect(&m_hbTimer, &QTimer::timeout, this, &VoiceClient::onHeartbeat);
    connect(&m_discoveryTimer, &QTimer::timeout, this, &VoiceClient::onDiscoveryTimeout);
    connect(&m_connectTimer, &QTimer::timeout, this, &VoiceClient::onConnectTimeout);
    m_discoveryTimer.setSingleShot(true);
    m_connectTimer.setSingleShot(true);
}

void VoiceClient::connectVoice(const QString &endpoint, const QString &token,
                               const QString &serverId, const QString &userId,
                               const QString &sessionId, const QString &channelId) {
    leave();
    m_leaving = false;
    m_token = token;
    m_serverId = serverId;
    m_userId = userId;
    m_sessionId = sessionId;
    m_channelId = channelId.isEmpty() ? serverId : channelId;

    QString ep = endpoint.trimmed();
    if (ep.startsWith(QStringLiteral("wss://")))
        ep = ep.mid(6);
    else if (ep.startsWith(QStringLiteral("ws://")))
        ep = ep.mid(5);
    int slash = ep.indexOf('/');
    if (slash >= 0)
        ep = ep.left(slash);
    int colon = ep.indexOf(':');
    int port = 443;
    if (colon >= 0) {
        m_host = ep.left(colon);
        bool ok = false;
        int p = ep.mid(colon + 1).toInt(&ok);
        if (ok && p > 0)
            port = p;
    } else {
        m_host = ep;
    }
    m_port = port;
    m_path = QStringLiteral("/?v=8");

    qWarning("[voice] connectVoice host=%s port=%d path=%s session=%s",
             qPrintable(m_host), m_port, qPrintable(m_path), qPrintable(m_sessionId));

    m_buffer.clear();
    m_inHttp = true;
    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }
    setStatus(QStringLiteral("Connecting voice..."));
    m_connectTimer.start(20000);
    m_sock.connectToHostEncrypted(m_host, m_port);
}

void VoiceClient::leave() {
    m_leaving = true;
    cleanup();
    setStatus(QStringLiteral("Disconnected"));
    m_leaving = false;
}

void VoiceClient::setMicEnabled(bool enabled) {
    m_micWanted = enabled;
    if (m_audio)
        m_audio->setMicEnabled(enabled);
    if (m_connected)
        sendSpeaking(enabled);
}

void VoiceClient::setStatus(const QString &s) {
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

void VoiceClient::fail(const QString &msg) {
    if (m_leaving)
        return;
    m_leaving = true;
    emit errorOccurred(msg);
    setStatus(msg);
    cleanup();
    m_leaving = false;
}

void VoiceClient::cleanup() {
    m_hbTimer.stop();
    m_discoveryTimer.stop();
    m_connectTimer.stop();
    m_udp.close();
    if (m_audio)
        m_audio->stop();
    if (m_sock.state() != QAbstractSocket::UnconnectedState)
        m_sock.close();
    m_buffer.clear();
    m_inHttp = false;
    m_modes.clear();
    m_secret.clear();
    m_lastSeq = 0;
    m_ssrc = 0;
    m_clients.clear();
    m_discoveryIp.clear();
    m_discoveryPort = 0;
    m_udpIp.clear();
    m_udpPort = 0;
    m_connected = false;
}

void VoiceClient::onConnected() {
    qWarning("[voice] socket connected to %s:%d, encrypted=%d",
             qPrintable(m_host), m_sock.peerPort(), m_sock.isEncrypted());

    QByteArray raw(16, '\0');
    for (int i = 0; i < 16; ++i)
        raw[i] = (char)QRandomGenerator::system()->bounded(256);
    const QByteArray key = raw.toBase64();

    QByteArray req;
    req += "GET " + m_path.toLatin1() + " HTTP/1.1\r\n";
    req += "Host: " + m_host.toLatin1() + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "\r\n";

    m_sock.write(req);
    qWarning("[voice] sent handshake:\n%s", req.constData());
}

void VoiceClient::onDisconnected() {
    if (m_leaving)
        return;
    m_hbTimer.stop();
    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }
    setStatus(QStringLiteral("Voice disconnected"));
    emit errorOccurred(QStringLiteral("Voice connection closed"));
    cleanup();
}

void VoiceClient::onReadyRead() {
    m_buffer.append(m_sock.readAll());
    if (m_inHttp)
        processHttp();
    else
        processFrames();
}

void VoiceClient::processHttp() {
    int idx = m_buffer.indexOf("\r\n\r\n");
    if (idx < 0)
        return;
    QByteArray head = m_buffer.mid(0, idx);
    QByteArray body = m_buffer.mid(idx + 4);
    QStringList lines = QString::fromLatin1(head).split("\r\n");
    QString status = lines.isEmpty() ? QString() : lines.first();
    qWarning("[voice] http status: %s", qPrintable(status));
    qWarning("[voice] http headers:\n%s", qPrintable(head));

    if (!status.contains(QStringLiteral(" 101"))) {
        if (!body.isEmpty())
            qWarning("[voice] http body (%d bytes): %s", (int)body.size(), qPrintable(body));
        m_buffer.clear();
        fail(QStringLiteral("Voice handshake failed: %1").arg(status));
        return;
    }
    m_buffer = body;
    m_inHttp = false;
    m_connectTimer.stop();
    sendIdentify();
    if (!m_buffer.isEmpty())
        processFrames();
}

void VoiceClient::sendIdentify() {
    QJsonObject d;
    d[QStringLiteral("server_id")] = m_serverId;
    d[QStringLiteral("user_id")] = m_userId;
    d[QStringLiteral("session_id")] = m_sessionId;
    d[QStringLiteral("token")] = m_token;
    d[QStringLiteral("max_dave_protocol_version")] =
        (m_audio && m_audio->isLoaded()) ? 1 : 0;
    QJsonObject o;
    o[QStringLiteral("op")] = 0;
    o[QStringLiteral("d")] = d;
    sendJson(o);
    qWarning("[voice] identify sent max_dave=%d", (m_audio && m_audio->isLoaded()) ? 1 : 0);
    setStatus(QStringLiteral("Voice identifying..."));
}

void VoiceClient::startDiscovery() {
    if (m_discoveryPort == 0) {
        fail(QStringLiteral("Voice ready missing UDP port"));
        return;
    }
    m_udp.close();
    m_udp.bind(QHostAddress::Any, 0);
    QByteArray pkt = makeDiscoveryPacket(m_ssrc);
    qint64 sent = m_udp.writeDatagram(pkt, QHostAddress(m_discoveryIp), m_discoveryPort);
    if (sent < 0) {
        fail(QStringLiteral("Voice UDP discovery send failed"));
        return;
    }
    m_discoveryTimer.start(5000);
}

void VoiceClient::sendSelectProtocol() {
    QString mode = preferredMode(m_modes);
    if (mode.isEmpty())
        mode = m_modes.value(0);
    if (mode.isEmpty()) {
        fail(QStringLiteral("Voice server offered no encryption mode"));
        return;
    }
    QJsonObject data;
    data[QStringLiteral("address")] = m_udpIp;
    data[QStringLiteral("port")] = (int)m_udpPort;
    data[QStringLiteral("mode")] = mode;
    QJsonObject d;
    d[QStringLiteral("protocol")] = QStringLiteral("udp");
    d[QStringLiteral("data")] = data;
    QJsonObject o;
    o[QStringLiteral("op")] = 1;
    o[QStringLiteral("d")] = d;
    sendJson(o);
    setStatus(QStringLiteral("Voice selecting protocol..."));
}

void VoiceClient::sendJson(const QJsonObject &obj) {
    sendWs(0x1, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void VoiceClient::sendWs(int opcode, const QByteArray &payload) {
    m_sock.write(encodeWsFrame(opcode, payload));
}

void VoiceClient::sendSpeaking(bool speaking) {
    QJsonObject d;
    d[QStringLiteral("speaking")] = speaking ? 1 : 0;
    d[QStringLiteral("delay")] = 0;
    d[QStringLiteral("ssrc")] = (double)m_ssrc;
    QJsonObject o;
    o[QStringLiteral("op")] = 5;
    o[QStringLiteral("d")] = d;
    sendJson(o);
}

void VoiceClient::onHeartbeat() {
    const qint64 elapsed = m_hbClock.isValid() ? m_hbClock.restart() : 0;
    QJsonObject d;
    d[QStringLiteral("t")] = (double)elapsed;
    d[QStringLiteral("seq_ack")] = m_lastSeq ? QJsonValue((double)m_lastSeq) : QJsonValue(-1.0);
    QJsonObject o;
    o[QStringLiteral("op")] = 3;
    o[QStringLiteral("d")] = d;
    sendJson(o);
}

void VoiceClient::onSocketError() {
    fail(QStringLiteral("Voice socket error: %1").arg(m_sock.errorString()));
}

void VoiceClient::onDiscoveryRead() {
    while (m_udp.hasPendingDatagrams()) {
        QByteArray pkt;
        pkt.resize((int)m_udp.pendingDatagramSize());
        m_udp.readDatagram(pkt.data(), pkt.size());
        QString ip;
        quint16 port = 0;
        if (parseDiscoveryReply(pkt, &ip, &port)) {
            m_udpIp = ip;
            m_udpPort = port;
            m_discoveryTimer.stop();
            qWarning("[voice] discovery reply ip=%s port=%u", qPrintable(ip), port);
            sendSelectProtocol();
            return;
        }
        if (m_audio)
            m_audio->handleRtpPacket(pkt);
    }
}

void VoiceClient::onSslErrors(const QList<QSslError> &errors) {
    for (const QSslError &e : errors)
        qWarning("[voice] SSL error: %s", qPrintable(e.errorString()));
    m_sock.ignoreSslErrors(errors);
}

void VoiceClient::onConnectTimeout() {
    qWarning("[voice] connect timed out");
    fail(QStringLiteral("Voice connect timed out"));
}

void VoiceClient::onDiscoveryTimeout() {
    fail(QStringLiteral("Voice UDP discovery timed out"));
}

static QByteArray encodeWsFrame(int opcode, const QByteArray &payload) {
    QByteArray out;
    out.append((char)(0x80 | opcode));
    quint64 len = payload.size();
    if (len < 126) {
        out.append((char)(0x80 | len));
    } else if (len <= 0xFFFF) {
        out.append((char)(0x80 | 126));
        out.append((char)((len >> 8) & 0xFF));
        out.append((char)(len & 0xFF));
    } else {
        out.append((char)(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            out.append((char)((len >> (i * 8)) & 0xFF));
    }
    quint8 mask[4];
    const quint32 r = QRandomGenerator::system()->generate();
    memcpy(mask, &r, 4);
    out.append(reinterpret_cast<const char *>(mask), 4);
    const int base = out.size();
    out.append(payload);
    for (int i = 0; i < payload.size(); ++i)
        out[base + i] = (char)((quint8)out[base + i] ^ mask[i % 4]);
    return out;
}

static bool decodeOneFrame(const QByteArray &buf, int *opcode, QByteArray *payload, int *consumed) {
    if (buf.size() < 2)
        return false;
    const quint8 *p = (const quint8 *)buf.constData();
    bool fin = (p[0] & 0x80) != 0;
    *opcode = p[0] & 0x0F;
    bool masked = (p[1] & 0x80) != 0;
    quint64 len = p[1] & 0x7F;
    int off = 2;
    if (len == 126) {
        if (buf.size() < 4)
            return false;
        len = ((quint64)p[2] << 8) | p[3];
        off = 4;
    } else if (len == 127) {
        if (buf.size() < 10)
            return false;
        len = 0;
        for (int i = 2; i < 10; ++i)
            len = (len << 8) | p[i];
        off = 10;
    }
    QByteArray mask;
    if (masked) {
        if (buf.size() < off + 4)
            return false;
        mask = buf.mid(off, 4);
        off += 4;
    }
    if (buf.size() < off + len)
        return false;
    QByteArray data = buf.mid(off, len);
    if (masked) {
        for (int i = 0; i < data.size(); ++i)
            data[i] ^= mask[i % 4];
    }
    *payload = data;
    *consumed = off + (int)len;
    return true;
}

void VoiceClient::processFrames() {
    for (;;) {
        int opcode = 0;
        QByteArray payload;
        int consumed = 0;
        if (!decodeOneFrame(m_buffer, &opcode, &payload, &consumed))
            return;
        m_buffer.remove(0, consumed);

        if (opcode == 0x8) {
            fail(QStringLiteral("Voice connection closed"));
            return;
        }
        if (opcode == 0x9) {
            sendWs(0xA, payload);
            continue;
        }
        if (opcode == 0xA)
            continue;
        if (opcode != 0x1 && opcode != 0x2)
            continue;

        QByteArray json = payload;
        if (opcode == 0x2) {
            int seq = 0;
            int voiceOp = 0;
            QByteArray j;
            if (!parseVoiceBinary(payload, &seq, &voiceOp, &j))
                continue;
            m_lastSeq = (quint64)seq;
            if (voiceOp >= 25 && voiceOp <= 31) {
                handleBinaryOp(voiceOp, j);
                continue;
            }
            json = j;
        }
        handleMessage(json);
    }
}

void VoiceClient::handleMessage(const QByteArray &json) {
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return;
    QJsonObject o = doc.object();
    int op = o.value(QStringLiteral("op")).toInt(-1);
    QJsonObject d = o.value(QStringLiteral("d")).toObject();

    switch (op) {
    case 8: {
        m_heartbeatMs = d.value(QStringLiteral("heartbeat_interval")).toInt();
        if (m_heartbeatMs <= 0)
            m_heartbeatMs = 41250;
        m_hbTimer.start(m_heartbeatMs);
        m_hbClock.start();
        onHeartbeat();
        if (o.contains("seq"))
            m_lastSeq = o.value("seq").toInt();
        qWarning("[voice] WS hello received, hb=%dms", m_heartbeatMs);
        setStatus(QStringLiteral("Voice hello"));
        break;
    }
    case 2: {
        m_ssrc = (quint32)d.value(QStringLiteral("ssrc")).toDouble();
        m_modes.clear();
        const QJsonArray modes = d.value(QStringLiteral("modes")).toArray();
        for (const QJsonValue &v : modes)
            m_modes << v.toString();
        m_discoveryIp = d.value(QStringLiteral("ip")).toString();
        m_discoveryPort = (quint16)d.value(QStringLiteral("port")).toInt();
        qWarning("[voice] op2 ready ssrc=%u ip=%s port=%u modes=%d",
                 m_ssrc, qPrintable(m_discoveryIp), m_discoveryPort, m_modes.size());
        emit readyReceived((int)m_ssrc);
        setStatus(QStringLiteral("Voice discovering UDP..."));
        startDiscovery();
        break;
    }
    case 4: {
        m_secret.clear();
        const QJsonArray key = d.value(QStringLiteral("secret_key")).toArray();
        for (const QJsonValue &v : key)
            m_secret.append(char(v.toInt()));
        m_connected = true;
        m_discoveryTimer.stop();
        const QString mode = d.value(QStringLiteral("mode")).toString();
        qWarning("[voice] op4 session desc secret=%d mode=%s ssrc=%u",
                 m_secret.size(), qPrintable(mode), m_ssrc);
        if (m_audio) {
            m_audio->configure(m_userId, m_channelId.toULongLong(), m_secret, m_ssrc, mode);
            m_audio->start();
            m_audio->setDaveProtocolVersion(
                d.value(QStringLiteral("dave_protocol_version")).toInt());
            m_audio->setMicEnabled(m_micWanted);
        }
        sendSpeaking(m_micWanted);
        emit connectedChanged();
        emit sessionDescriptionReceived(mode);
        setStatus(QStringLiteral("Voice connected"));
        break;
    }
    case 5: {
        if (m_audio) {
            const QString uid = d.value(QStringLiteral("user_id")).toString();
            const quint32 ssrc = (quint32)d.value(QStringLiteral("ssrc")).toDouble();
            qWarning("[voice] op5 speaking user=%s ssrc=%u", qPrintable(uid), ssrc);
            m_audio->setSpeakingUser(uid, ssrc);
        }
        break;
    }
    case 11: {
        const QJsonArray ids = d.value(QStringLiteral("user_ids")).toArray();
        for (const QJsonValue &v : ids)
            m_clients.insert(v.toString());
        break;
    }
    case 13: {
        const QString id = d.value(QStringLiteral("user_id")).toString();
        m_clients.remove(id);
        if (m_audio)
            m_audio->removeUser(id);
        break;
    }
    case 21: {
        const int id = d.value(QStringLiteral("transition_id")).toInt();
        const int version = d.value(QStringLiteral("protocol_version")).toInt();
        if (m_audio && m_audio->prepareDaveTransition(id, version)) {
            QJsonObject r;
            r[QStringLiteral("transition_id")] = id;
            QJsonObject o;
            o[QStringLiteral("op")] = 23;
            o[QStringLiteral("d")] = r;
            sendJson(o);
        }
        break;
    }
    case 22:
        if (m_audio)
            m_audio->executeDaveTransition(d.value(QStringLiteral("transition_id")).toInt());
        break;
    case 24:
        if (m_audio)
            m_audio->prepareDaveEpoch(d.value(QStringLiteral("epoch")).toInt(),
                                      d.value(QStringLiteral("protocol_version")).toInt());
        break;
    default:
        break;
    }
}

void VoiceClient::handleBinaryOp(int op, const QByteArray &payload) {
    if (!m_audio)
        return;
    switch (op) {
    case 25:
        m_audio->setDaveExternalSender(payload);
        break;
    case 27: {
        const QByteArray reply = m_audio->processDaveProposals(payload, m_clients.values());
        if (!reply.isEmpty()) {
            QByteArray frame(1, char(28));
            sendWs(0x2, frame + reply);
        }
        break;
    }
    case 29: {
        int id = 0;
        if (m_audio->processDaveCommit(payload, &id) && id != 0) {
            QJsonObject d;
            d[QStringLiteral("transition_id")] = id;
            QJsonObject o;
            o[QStringLiteral("op")] = 23;
            o[QStringLiteral("d")] = d;
            sendJson(o);
        }
        break;
    }
    case 30: {
        int id = 0;
        if (m_audio->processDaveWelcome(payload, m_clients.values(), &id) && id != 0) {
            QJsonObject d;
            d[QStringLiteral("transition_id")] = id;
            QJsonObject o;
            o[QStringLiteral("op")] = 23;
            o[QStringLiteral("d")] = d;
            sendJson(o);
        }
        break;
    }
    default:
        break;
    }
}

bool VoiceClient::selfTest() {
    bool ok = true;

    QByteArray disc = makeDiscoveryPacket(0x11223344u);
    if (disc.size() != 74 || (quint8)disc[0] != 0x00 || (quint8)disc[1] != 0x01
            || (quint8)disc[2] != 0x00 || (quint8)disc[3] != 0x46
            || (quint8)disc[4] != 0x11 || (quint8)disc[5] != 0x22
            || (quint8)disc[6] != 0x33 || (quint8)disc[7] != 0x44) {
        qWarning("[voice-selftest] discovery packet layout mismatch");
        ok = false;
    }

    QByteArray reply(74, '\0');
    reply[0] = 0x00;
    reply[1] = 0x02;
    const QByteArray addr("1.2.3.4");
    for (int i = 0; i < addr.size(); ++i)
        reply[8 + i] = addr[i];
    reply[72] = char((50000 >> 8) & 0xFF);
    reply[73] = char(50000 & 0xFF);
    QString ip;
    quint16 port = 0;
    if (!parseDiscoveryReply(reply, &ip, &port) || ip != QStringLiteral("1.2.3.4") || port != 50000) {
        qWarning("[voice-selftest] discovery reply parse mismatch");
        ok = false;
    }

    QStringList modes{QStringLiteral("xsalsa20_poly1305"),
                      QStringLiteral("aead_xchacha20_poly1305_rtpsize")};
    if (preferredMode(modes) != QStringLiteral("aead_xchacha20_poly1305_rtpsize")) {
        qWarning("[voice-selftest] preferred mode mismatch");
        ok = false;
    }

    if (!VoiceAudio::selfTest())
        ok = false;

    if (ok)
        qWarning("[voice-selftest] ok");
    return ok;
}
