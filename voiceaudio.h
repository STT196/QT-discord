#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QStringList>

class QAudioSink;
class QAudioSource;
class QIODevice;

// Native voice media path: libsodium transport crypto, libdave E2EE and Opus
// encode/decode with Qt Multimedia for capture and playback. DLLs are loaded at
// runtime from the executable directory; when they are absent the client keeps
// working as a signalling-only connection.
class VoiceAudio : public QObject {
    Q_OBJECT

public:
    explicit VoiceAudio(QObject *parent = nullptr);
    ~VoiceAudio() override;

    bool loadLibraries();
    bool isLoaded() const { return m_loaded; }

    void configure(const QString &selfUserId, quint64 channelId,
                   const QByteArray &secret, quint32 ssrc, const QString &mode);
    void start();
    void stop();
    void setMicEnabled(bool enabled);

    // DAVE / MLS signalling. Payloads are the raw voice opcode payloads.
    void setDaveProtocolVersion(int version);
    void prepareDaveEpoch(int epoch, int version);
    bool prepareDaveTransition(int transitionId, int version);
    void executeDaveTransition(int transitionId);
    bool processDaveCommit(const QByteArray &payload, int *transitionId);
    bool processDaveWelcome(const QByteArray &payload, const QStringList &clients,
                            int *transitionId);
    void setDaveExternalSender(const QByteArray &payload);
    QByteArray processDaveProposals(const QByteArray &payload, const QStringList &clients);

    void handleRtpPacket(const QByteArray &packet);
    void setSpeakingUser(const QString &userId, quint32 ssrc);
    void removeUser(const QString &userId);

    // Header/nonce checks for --voice-selftest. No DLLs required.
    static bool selfTest();
    // Transport encrypt/decrypt roundtrip with the real DLLs.
    bool loopbackTest();

signals:
    void daveKeyPackageReady(const QByteArray &keyPackage);
    void daveInvalidateTransition(int transitionId);
    void rtpPacketReady(const QByteArray &packet);

private:
    struct Dlls;

    void ensureSink();
    void ensureSource();
    void onMicRead();
    void resetDaveKeys();
    void resetDaveSession();
    QByteArray decryptTransport(const QByteArray &packet, int headerLength);
    QByteArray daveDecrypt(const QString &userId, const QByteArray &opus);
    QByteArray daveEncrypt(const QByteArray &opus);
    void decodeAndPlay(const QString &userId, const QByteArray &opus);
    void sendOpus(const QByteArray &opus);

    Dlls *m_dlls = nullptr;
    bool m_loaded = false;
    bool m_running = false;
    bool m_micWanted = false;

    QString m_selfUserId;
    quint64 m_channelId = 0;
    QByteArray m_secret;
    quint32 m_ssrc = 0;
    QString m_mode;
    quint16 m_seq = 0;
    quint32 m_timestamp = 0;
    quint32 m_nonceCounter = 0;

    QAudioSink *m_sink = nullptr;
    QIODevice *m_sinkIo = nullptr;
    QAudioSource *m_source = nullptr;
    QIODevice *m_sourceIo = nullptr;
    QByteArray m_micBuf;

    void *m_session = nullptr;
    void *m_encryptor = nullptr;
    void *m_encoder = nullptr;
    void *m_selfRatchet = nullptr;
    QHash<QString, void *> m_decoders;
    QHash<QString, void *> m_daveDecs;
    QHash<QString, void *> m_ratchets;
    QHash<quint32, QString> m_ssrcUsers;

    int m_daveVersion = 0;
    bool m_daveReady = false;
    bool m_decryptLogged = false;
    int m_lastTransition = -1;
    int m_failures = 0;
    QHash<int, int> m_pending;
};
