#pragma once

#include <QObject>
#include <QSslSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QSet>
#include <QStringList>

class VoiceAudio;

class VoiceClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit VoiceClient(QObject *parent = nullptr);

    void connectVoice(const QString &endpoint, const QString &token,
                      const QString &serverId, const QString &userId,
                      const QString &sessionId, const QString &channelId);
    void leave();
    void setMicEnabled(bool enabled);

    bool isConnected() const { return m_connected; }
    QString status() const { return m_status; }

    static bool selfTest();

signals:
    void statusChanged();
    void connectedChanged();
    void readyReceived(int ssrc);
    void sessionDescriptionReceived(const QString &mode);
    void errorOccurred(const QString &message);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onHeartbeat();
    void onDiscoveryRead();
    void onConnectTimeout();
    void onDiscoveryTimeout();
    void onSocketError();
    void onSslErrors(const QList<QSslError> &errors);

private:
    void setStatus(const QString &s);
    void fail(const QString &msg);
    void cleanup();

    void processHttp();
    void processFrames();
    void handleMessage(const QByteArray &json);
    void handleBinaryOp(int op, const QByteArray &payload);
    void sendJson(const QJsonObject &obj);
    void sendWs(int opcode, const QByteArray &payload);
    void sendSpeaking(bool speaking);

    void sendIdentify();
    void startDiscovery();
    void sendSelectProtocol();

    QSslSocket m_sock;
    QUdpSocket m_udp;
    VoiceAudio *m_audio = nullptr;
    QTimer m_hbTimer;
    QElapsedTimer m_hbClock;
    QTimer m_discoveryTimer;
    QTimer m_connectTimer;

    QString m_token, m_serverId, m_userId, m_sessionId, m_channelId;
    QString m_host, m_path;
    quint16 m_port = 0;
    QByteArray m_buffer;
    bool m_inHttp = false;
    bool m_leaving = false;
    bool m_connected = false;
    QString m_status;

    int m_heartbeatMs = 0;
    quint64 m_lastSeq = 0;
    quint32 m_ssrc = 0;
    QStringList m_modes;
    QString m_discoveryIp, m_udpIp;
    quint16 m_discoveryPort = 0, m_udpPort = 0;
    QByteArray m_secret;
    QSet<QString> m_clients;
    bool m_micWanted = false;
};
