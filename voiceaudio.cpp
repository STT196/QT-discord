#include "voiceaudio.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QLibrary>
#include <QMediaDevices>

#include "dave.h"

#include <QVector>
#include <QList>
#include <cstring>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kFrameSamples = 960;
constexpr int kFrameBytes = kFrameSamples * kChannels * 2;
constexpr int kPrebufferFrames = 6;
constexpr int kNonceSize = 24;
constexpr int kTagSize = 16;
constexpr int kOpusPayloadType = 120;

struct OpusDecoder;
struct OpusEncoder;

typedef OpusDecoder *(*opus_decoder_create_fn)(int, int, int *);
typedef int (*opus_decode_fn)(OpusDecoder *, const unsigned char *, int, short *, int, int);
typedef void (*opus_decoder_destroy_fn)(OpusDecoder *);
typedef OpusEncoder *(*opus_encoder_create_fn)(int, int, int, int *);
typedef int (*opus_encode_fn)(OpusEncoder *, const short *, int, unsigned char *, int);
typedef void (*opus_encoder_destroy_fn)(OpusEncoder *);

typedef int (*sodium_init_fn)(void);
typedef int (*aead_encrypt_fn)(unsigned char *, unsigned long long *,
                               const unsigned char *, unsigned long long,
                               const unsigned char *, unsigned long long,
                               unsigned char *, const unsigned char *,
                               const unsigned char *);
typedef int (*aead_decrypt_fn)(unsigned char *, unsigned long long *,
                               unsigned char *, const unsigned char *, unsigned long long,
                               const unsigned char *, unsigned long long,
                               const unsigned char *, const unsigned char *);

const QByteArray kSilence = QByteArray::fromHex("f8fffe");

int headerLengthFor(const QByteArray &p) {
    if (p.size() < 12)
        return 0;
    return 12 + ((((quint8)p[0] >> 4) & 1) ? 4 : 0);
}

} // namespace

struct VoiceAudio::Dlls {
    QLibrary daveLib;
    QLibrary sodiumLib;
    QLibrary opusLib;

    decltype(&daveMaxSupportedProtocolVersion) maxProtocolVersion = nullptr;
    decltype(&daveFree) freeBytes = nullptr;
    decltype(&daveSessionCreate) sessionCreate = nullptr;
    decltype(&daveSessionDestroy) sessionDestroy = nullptr;
    decltype(&daveSessionInit) sessionInit = nullptr;
    decltype(&daveSessionGetMarshalledKeyPackage) sessionGetKeyPackage = nullptr;
    decltype(&daveSessionSetExternalSender) sessionSetExternalSender = nullptr;
    decltype(&daveSessionProcessProposals) sessionProcessProposals = nullptr;
    decltype(&daveSessionProcessCommit) sessionProcessCommit = nullptr;
    decltype(&daveSessionProcessWelcome) sessionProcessWelcome = nullptr;
    decltype(&daveSessionGetKeyRatchet) sessionGetKeyRatchet = nullptr;
    decltype(&daveSessionGetLastEpochAuthenticator) sessionEpochAuth = nullptr;
    decltype(&daveKeyRatchetDestroy) keyRatchetDestroy = nullptr;
    decltype(&daveCommitResultIsFailed) commitIsFailed = nullptr;
    decltype(&daveCommitResultIsIgnored) commitIsIgnored = nullptr;
    decltype(&daveCommitResultDestroy) commitDestroy = nullptr;
    decltype(&daveWelcomeResultDestroy) welcomeDestroy = nullptr;
    decltype(&daveEncryptorCreate) encryptorCreate = nullptr;
    decltype(&daveEncryptorDestroy) encryptorDestroy = nullptr;
    decltype(&daveEncryptorSetKeyRatchet) encryptorSetKeyRatchet = nullptr;
    decltype(&daveEncryptorSetPassthroughMode) encryptorSetPassthrough = nullptr;
    decltype(&daveEncryptorAssignSsrcToCodec) encryptorAssignSsrc = nullptr;
    decltype(&daveEncryptorGetMaxCiphertextByteSize) encryptorMaxCiphertext = nullptr;
    decltype(&daveEncryptorEncrypt) encryptorEncrypt = nullptr;
    decltype(&daveDecryptorCreate) decryptorCreate = nullptr;
    decltype(&daveDecryptorDestroy) decryptorDestroy = nullptr;
    decltype(&daveDecryptorTransitionToKeyRatchet) decryptorToRatchet = nullptr;
    decltype(&daveDecryptorTransitionToPassthroughMode) decryptorToPassthrough = nullptr;
    decltype(&daveDecryptorGetMaxPlaintextByteSize) decryptorMaxPlaintext = nullptr;
    decltype(&daveDecryptorDecrypt) decryptorDecrypt = nullptr;

    sodium_init_fn sodiumInit = nullptr;
    aead_encrypt_fn aeadEncrypt = nullptr;
    aead_decrypt_fn aeadDecrypt = nullptr;

    opus_decoder_create_fn decoderCreate = nullptr;
    opus_decode_fn decode = nullptr;
    opus_decoder_destroy_fn decoderDestroy = nullptr;
    opus_encoder_create_fn encoderCreate = nullptr;
    opus_encode_fn encode = nullptr;
    opus_encoder_destroy_fn encoderDestroy = nullptr;
};

static bool openLibrary(QLibrary &lib, const QString &fileName) {
    const QString local =
        QCoreApplication::applicationDirPath() + QLatin1Char('/') + fileName;
    lib.setFileName(QFileInfo::exists(local) ? local : fileName);
    return lib.load();
}

VoiceAudio::VoiceAudio(QObject *parent) : QObject(parent) {}

VoiceAudio::~VoiceAudio() {
    stop();
    resetDaveSession();
    delete m_dlls;
}

bool VoiceAudio::loadLibraries() {
    if (m_loaded)
        return true;
    auto *d = new Dlls;
    m_dlls = d;

    bool ok = openLibrary(d->daveLib, QStringLiteral("libdave.dll"))
              && openLibrary(d->sodiumLib, QStringLiteral("libsodium.dll"))
              && openLibrary(d->opusLib, QStringLiteral("opus.dll"));
    if (!ok) {
        qWarning("[voice] native voice DLLs missing next to the executable");
        delete m_dlls;
        m_dlls = nullptr;
        return false;
    }

#define RESOLVE(field, name)                                                   \
    do {                                                                       \
        field = (decltype(field))(d->daveLib.resolve(name));                    \
        if (!field) {                                                          \
            qWarning("[voice] missing libdave export %s", name);                \
            ok = false;                                                        \
        }                                                                      \
    } while (0)

    RESOLVE(d->maxProtocolVersion, "daveMaxSupportedProtocolVersion");
    RESOLVE(d->freeBytes, "daveFree");
    RESOLVE(d->sessionCreate, "daveSessionCreate");
    RESOLVE(d->sessionDestroy, "daveSessionDestroy");
    RESOLVE(d->sessionInit, "daveSessionInit");
    RESOLVE(d->sessionGetKeyPackage, "daveSessionGetMarshalledKeyPackage");
    RESOLVE(d->sessionSetExternalSender, "daveSessionSetExternalSender");
    RESOLVE(d->sessionProcessProposals, "daveSessionProcessProposals");
    RESOLVE(d->sessionProcessCommit, "daveSessionProcessCommit");
    RESOLVE(d->sessionProcessWelcome, "daveSessionProcessWelcome");
    RESOLVE(d->sessionGetKeyRatchet, "daveSessionGetKeyRatchet");
    RESOLVE(d->sessionEpochAuth, "daveSessionGetLastEpochAuthenticator");
    RESOLVE(d->keyRatchetDestroy, "daveKeyRatchetDestroy");
    RESOLVE(d->commitIsFailed, "daveCommitResultIsFailed");
    RESOLVE(d->commitIsIgnored, "daveCommitResultIsIgnored");
    RESOLVE(d->commitDestroy, "daveCommitResultDestroy");
    RESOLVE(d->welcomeDestroy, "daveWelcomeResultDestroy");
    RESOLVE(d->encryptorCreate, "daveEncryptorCreate");
    RESOLVE(d->encryptorDestroy, "daveEncryptorDestroy");
    RESOLVE(d->encryptorSetKeyRatchet, "daveEncryptorSetKeyRatchet");
    RESOLVE(d->encryptorSetPassthrough, "daveEncryptorSetPassthroughMode");
    RESOLVE(d->encryptorAssignSsrc, "daveEncryptorAssignSsrcToCodec");
    RESOLVE(d->encryptorMaxCiphertext, "daveEncryptorGetMaxCiphertextByteSize");
    RESOLVE(d->encryptorEncrypt, "daveEncryptorEncrypt");
    RESOLVE(d->decryptorCreate, "daveDecryptorCreate");
    RESOLVE(d->decryptorDestroy, "daveDecryptorDestroy");
    RESOLVE(d->decryptorToRatchet, "daveDecryptorTransitionToKeyRatchet");
    RESOLVE(d->decryptorToPassthrough, "daveDecryptorTransitionToPassthroughMode");
    RESOLVE(d->decryptorMaxPlaintext, "daveDecryptorGetMaxPlaintextByteSize");
    RESOLVE(d->decryptorDecrypt, "daveDecryptorDecrypt");
#undef RESOLVE

    d->sodiumInit = (sodium_init_fn)d->sodiumLib.resolve("sodium_init");
    d->aeadEncrypt = (aead_encrypt_fn)d->sodiumLib.resolve(
        "crypto_aead_xchacha20poly1305_ietf_encrypt");
    d->aeadDecrypt = (aead_decrypt_fn)d->sodiumLib.resolve(
        "crypto_aead_xchacha20poly1305_ietf_decrypt");
    d->decoderCreate = (opus_decoder_create_fn)d->opusLib.resolve("opus_decoder_create");
    d->decode = (opus_decode_fn)d->opusLib.resolve("opus_decode");
    d->decoderDestroy =
        (opus_decoder_destroy_fn)d->opusLib.resolve("opus_decoder_destroy");
    d->encoderCreate = (opus_encoder_create_fn)d->opusLib.resolve("opus_encoder_create");
    d->encode = (opus_encode_fn)d->opusLib.resolve("opus_encode");
    d->encoderDestroy =
        (opus_encoder_destroy_fn)d->opusLib.resolve("opus_encoder_destroy");

    if (d->sodiumInit && d->sodiumInit() != 0)
        ok = false;
    if (!d->aeadEncrypt || !d->aeadDecrypt || !d->decoderCreate || !d->decode
        || !d->decoderDestroy || !d->encoderCreate || !d->encode || !d->encoderDestroy)
        ok = false;

    if (!ok) {
        delete m_dlls;
        m_dlls = nullptr;
        return false;
    }
    m_loaded = true;
    qWarning("[voice] native audio ready (DAVE v%d)",
             (int)d->maxProtocolVersion());
    return true;
}

void VoiceAudio::configure(const QString &selfUserId, quint64 channelId,
                           const QByteArray &secret, quint32 ssrc, const QString &mode) {
    m_selfUserId = selfUserId;
    m_channelId = channelId;
    m_secret = secret;
    m_ssrc = ssrc;
    m_mode = mode;
    m_seq = 0;
    m_timestamp = 0;
    m_nonceCounter = 0;
    m_decryptLogged = false;
    m_pending.clear();
    if (m_encryptor && ssrc)
        m_dlls->encryptorAssignSsrc((DAVEEncryptorHandle)m_encryptor, ssrc,
                                    DAVE_CODEC_OPUS);
}

void VoiceAudio::start() {
    if (!m_loaded || m_running)
        return;
    m_running = true;
    m_tickGap.invalidate();
    m_statClock.invalidate();
    m_stats = Stats();
    ensureSink();
    if (!m_playTimer) {
        m_playTimer = new QTimer(this);
        m_playTimer->setTimerType(Qt::PreciseTimer);
        m_playTimer->setInterval(5);
        connect(m_playTimer, &QTimer::timeout, this, &VoiceAudio::onPlayTick);
    }
    m_playTimer->start();
    if (m_micWanted)
        ensureSource();
    qWarning("[voice] audio start loaded=%d running=%d sink=%d source=%d",
             m_loaded, m_running, m_sink != nullptr, m_source != nullptr);
}

void VoiceAudio::stop() {
    m_running = false;
    m_tickGap.invalidate();
    m_statClock.invalidate();
    m_stats = Stats();
    if (m_playTimer)
        m_playTimer->stop();
    m_jitter.clear();
    m_unmapped.clear();
    if (m_sourceIo) {
        m_sourceIo = nullptr;
    }
    if (m_source) {
        m_source->stop();
        delete m_source;
        m_source = nullptr;
    }
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
        m_sinkIo = nullptr;
    }
    m_micBuf.clear();
    if (m_encoder) {
        if (m_dlls && m_dlls->encoderDestroy)
            m_dlls->encoderDestroy((OpusEncoder *)m_encoder);
        m_encoder = nullptr;
    }
    for (void *dec : m_decoders) {
        if (m_dlls && m_dlls->decoderDestroy)
            m_dlls->decoderDestroy((OpusDecoder *)dec);
    }
    m_decoders.clear();
    m_ssrcUsers.clear();
}

void VoiceAudio::setMicEnabled(bool enabled) {
    m_micWanted = enabled;
    if (!m_loaded || !m_running) {
        if (!enabled && m_source) {
            m_source->stop();
            delete m_source;
            m_source = nullptr;
            m_sourceIo = nullptr;
        }
        return;
    }
    if (enabled)
        ensureSource();
    else if (m_source) {
        m_source->stop();
        delete m_source;
        m_source = nullptr;
        m_sourceIo = nullptr;
        m_micBuf.clear();
        if (m_encoder) {
            m_dlls->encoderDestroy((OpusEncoder *)m_encoder);
            m_encoder = nullptr;
        }
    }
}

void VoiceAudio::ensureSink() {
    if (m_sink)
        return;
    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(kChannels);
    fmt.setSampleFormat(QAudioFormat::Int16);
    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) {
        qWarning("[voice] no audio output device");
        return;
    }
    if (!dev.isFormatSupported(fmt))
        qWarning("[voice] 48k stereo output not reported as supported");
    m_sink = new QAudioSink(dev, fmt, this);
    // Audio runs on the GUI thread, so leave room for UI/network stalls.
    m_sink->setBufferSize(kFrameBytes * 16);
    m_sinkIo = m_sink->start();
    if (!m_sinkIo)
        qWarning("[voice] failed to start audio output");
}

void VoiceAudio::ensureSource() {
    if (m_source || !m_loaded)
        return;
    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(kChannels);
    fmt.setSampleFormat(QAudioFormat::Int16);
    QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull()) {
        qWarning("[voice] no audio input device");
        return;
    }
    if (!dev.isFormatSupported(fmt)) {
        qWarning("[voice] 48k stereo input not supported; microphone disabled");
        return;
    }
    m_source = new QAudioSource(dev, fmt, this);
    m_source->setBufferSize(kFrameBytes * 12);
    m_sourceIo = m_source->start();
    if (!m_sourceIo) {
        qWarning("[voice] failed to start audio input");
        return;
    }
    int err = 0;
    m_encoder = m_dlls->encoderCreate(kSampleRate, kChannels, 2048, &err);
    if (!m_encoder) {
        qWarning("[voice] opus encoder create failed (%d)", err);
        m_sourceIo = nullptr;
        delete m_source;
        m_source = nullptr;
        return;
    }
    connect(m_sourceIo, &QIODevice::readyRead, this, &VoiceAudio::onMicRead,
            Qt::UniqueConnection);
}

void VoiceAudio::onMicRead() {
    if (!m_sourceIo || !m_loaded || !m_encoder)
        return;
    m_micBuf.append(m_sourceIo->readAll());
    while (m_micBuf.size() >= kFrameBytes) {
        const QByteArray frame = m_micBuf.left(kFrameBytes);
        m_micBuf.remove(0, kFrameBytes);
        unsigned char encoded[4000];
        int n = m_dlls->encode((OpusEncoder *)m_encoder, (const short *)frame.constData(),
                               kFrameSamples, encoded, sizeof(encoded));
        if (n > 0)
            sendOpus(QByteArray((const char *)encoded, n));
    }
}

void VoiceAudio::sendOpus(const QByteArray &opus) {
    if (!m_running || !m_loaded)
        return;
    QByteArray payload = daveEncrypt(opus);
    QByteArray header(12, '\0');
    header[0] = char(0x80);
    header[1] = char(kOpusPayloadType);
    header[2] = char((m_seq >> 8) & 0xFF);
    header[3] = char(m_seq & 0xFF);
    header[4] = char((m_timestamp >> 24) & 0xFF);
    header[5] = char((m_timestamp >> 16) & 0xFF);
    header[6] = char((m_timestamp >> 8) & 0xFF);
    header[7] = char(m_timestamp & 0xFF);
    header[8] = char((m_ssrc >> 24) & 0xFF);
    header[9] = char((m_ssrc >> 16) & 0xFF);
    header[10] = char((m_ssrc >> 8) & 0xFF);
    header[11] = char(m_ssrc & 0xFF);
    ++m_seq;
    m_timestamp += kFrameSamples;

    if (m_secret.isEmpty() || m_dlls->aeadEncrypt == nullptr) {
        emit rtpPacketReady(header + payload);
        return;
    }
    ++m_nonceCounter;
    unsigned char nonce[kNonceSize];
    memset(nonce, 0, sizeof(nonce));
    nonce[0] = (unsigned char)((m_nonceCounter >> 24) & 0xFF);
    nonce[1] = (unsigned char)((m_nonceCounter >> 16) & 0xFF);
    nonce[2] = (unsigned char)((m_nonceCounter >> 8) & 0xFF);
    nonce[3] = (unsigned char)(m_nonceCounter & 0xFF);

    QByteArray cipher(payload.size() + kTagSize, '\0');
    unsigned long long written = 0;
    if (m_dlls->aeadEncrypt((unsigned char *)cipher.data(), &written,
                            (const unsigned char *)payload.constData(), payload.size(),
                            (const unsigned char *)header.constData(), header.size(),
                            nullptr, nonce,
                            (const unsigned char *)m_secret.constData()) != 0) {
        qWarning("[voice] transport encrypt failed");
        return;
    }
    cipher.resize((int)written);
    QByteArray tail((const char *)nonce, 4);
    emit rtpPacketReady(header + cipher + tail);
}

void VoiceAudio::handleRtpPacket(const QByteArray &packet) {
    if (!m_running || !m_loaded || packet.size() < 13)
        return;
    const quint8 *p = (const quint8 *)packet.constData();
    ++m_stats.rx;
    if ((p[0] >> 6) != 2 || (p[1] & 0x7F) != kOpusPayloadType) {
        ++m_stats.badHeader;
        return;
    }

    const quint32 ssrc = ((quint32)p[8] << 24) | ((quint32)p[9] << 16)
                         | ((quint32)p[10] << 8) | p[11];
    if (ssrc == m_ssrc) {
        ++m_stats.own;
        return;
    }
    const QString userId = m_ssrcUsers.value(ssrc);
    if (userId == m_selfUserId && !userId.isEmpty()) {
        ++m_stats.own;
        return;
    }
    if (userId.isEmpty()) {
        ++m_stats.unmapped;
        if (!m_unmapped.contains(ssrc))
            qWarning("[voice] audio from unmapped ssrc=%u (no op 5 yet)", ssrc);
        // Audio can arrive before the Speaking (op 5) event maps the SSRC.
        // Hold a short backlog and replay it once the user is known.
        QList<QByteArray> &pending = m_unmapped[ssrc];
        if (pending.size() < 25)
            pending.append(packet);
        return;
    }

    const quint16 rtpSeq = (quint16)((p[2] << 8) | p[3]);
    const int headerLen = headerLengthFor(packet);
    QByteArray opus = decryptTransport(packet, headerLen);
    if (opus.isEmpty()) {
        ++m_stats.transport;
        return;
    }
    // Silence frames are not DAVE-encrypted. Keep them in the stream so the
    // sequence stays contiguous and is not mistaken for packet loss.
    if (opus != kSilence) {
        opus = daveDecrypt(userId, opus);
        if (opus.isEmpty()) {
            ++m_stats.dave;  // decrypt failure: playout will conceal the gap
            return;
        }
    }
    enqueueFrame(userId, rtpSeq, opus);
}

QByteArray VoiceAudio::decryptTransport(const QByteArray &packet, int headerLength) {
    if (m_secret.isEmpty() || !m_dlls->aeadDecrypt || headerLength < 12)
        return QByteArray();
    const int body = packet.size() - headerLength - 4;
    if (body <= kTagSize)
        return QByteArray();

    unsigned char nonce[kNonceSize];
    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, packet.constData() + packet.size() - 4, 4);

    const int cipherLength = body - kTagSize;
    const char *cipher = packet.constData() + headerLength;
    QByteArray out(cipherLength, '\0');
    unsigned long long written = 0;
    const int rc = m_dlls->aeadDecrypt((unsigned char *)out.data(), &written,
                            nullptr,
                            (const unsigned char *)cipher,
                            (unsigned long long)body,
                            (const unsigned char *)packet.constData(), headerLength,
                            nonce, (const unsigned char *)m_secret.constData());
    if (rc != 0) {
        if (!m_decryptLogged) {
            m_decryptLogged = true;
            qWarning("[voice] transport decrypt failed (mode %s)", qPrintable(m_mode));
        }
        return QByteArray();
    }

    out.resize((int)written);
    if (written > 0 && ((quint8)packet[0] & 0x20)) {
        const int pad = (quint8)out.at(out.size() - 1);
        if (pad > 0 && pad < out.size())
            out.chop(pad);
    }
    return out;
}

QByteArray VoiceAudio::daveDecrypt(const QString &userId, const QByteArray &opus) {
    if (m_daveVersion == 0 || !m_session)
        return opus;
    void *dec = m_daveDecs.value(userId);
    if (!dec)
        dec = ensureDaveDecryptor(userId);
    if (!dec)
        return opus;
    QByteArray out = decryptDaveFrame(dec, opus);
    if (!out.isEmpty())
        return out;
    // A stale or missing key ratchet (decryptor created before the MLS group
    // existed, or before a transition) is the usual cause. Re-derive from the
    // current state and retry once; libdave ignores duplicate key domains.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_retryAt.value(userId, 0) + 500 > now)
        return QByteArray();
    m_retryAt.insert(userId, now);
    ensureDaveDecryptor(userId);
    return decryptDaveFrame(dec, opus);
}

QByteArray VoiceAudio::decryptDaveFrame(void *dec, const QByteArray &opus) {
    size_t cap = m_dlls->decryptorMaxPlaintext((DAVEDecryptorHandle)dec,
                                              DAVE_MEDIA_TYPE_AUDIO, opus.size());
    if (cap < (size_t)opus.size() + 64)
        cap = opus.size() + 64;
    QByteArray out((int)cap, '\0');
    size_t written = 0;
    DAVEDecryptorResultCode rc = m_dlls->decryptorDecrypt(
        (DAVEDecryptorHandle)dec, DAVE_MEDIA_TYPE_AUDIO,
        (const uint8_t *)opus.constData(), opus.size(),
        (uint8_t *)out.data(), cap, &written);
    if (rc != DAVE_DECRYPTOR_RESULT_CODE_SUCCESS)
        return QByteArray();
    out.resize((int)written);
    return out;
}

QByteArray VoiceAudio::daveEncrypt(const QByteArray &opus) {
    if (!m_encryptorReady || m_daveVersion == 0 || !m_encryptor)
        return opus;
    const size_t cap = m_dlls->encryptorMaxCiphertext((DAVEEncryptorHandle)m_encryptor,
                                                     DAVE_MEDIA_TYPE_AUDIO, opus.size());
    QByteArray out((int)cap, '\0');
    size_t written = 0;
    const DAVEEncryptorResultCode rc = m_dlls->encryptorEncrypt(
        (DAVEEncryptorHandle)m_encryptor, DAVE_MEDIA_TYPE_AUDIO, m_ssrc,
        (const uint8_t *)opus.constData(), opus.size(),
        (uint8_t *)out.data(), cap, &written);
    if (rc != DAVE_ENCRYPTOR_RESULT_CODE_SUCCESS)
        return opus;
    out.resize((int)written);
    return out;
}

void *VoiceAudio::decoderFor(const QString &userId) {
    void *dec = m_decoders.value(userId);
    if (dec)
        return dec;
    int err = 0;
    dec = m_dlls->decoderCreate(kSampleRate, kChannels, &err);
    if (dec)
        m_decoders.insert(userId, dec);
    return dec;
}

void VoiceAudio::enqueueFrame(const QString &userId, quint16 rtpSeq, const QByteArray &opus) {
    Jitter &j = m_jitter[userId];
    qint64 ext;
    if (!j.have) {
        j.have = true;
        ext = rtpSeq;
        j.hi = ext;
    } else {
        ext = j.hi + (qint16)(rtpSeq - (quint16)j.hi);
        if (ext > j.hi)
            j.hi = ext;
    }
    if (j.started && ext < j.next) {
        ++m_stats.late;  // arrived too late to play
        return;
    }
    j.q.insert(ext, opus);
    while (j.q.size() > 15) {  // cap latency at ~300 ms
        j.q.erase(j.q.begin());
        if (j.started && !j.q.isEmpty())
            j.next = j.q.firstKey();
    }
}

VoiceAudio::PlayoutAction VoiceAudio::nextPlayoutAction(Jitter &j) {
    if (!j.started) {
        if (j.q.size() < kPrebufferFrames)
            return PlayoutAction::Wait;
        j.started = true;
        j.next = j.q.firstKey();
    }
    if (j.q.contains(j.next))
        return PlayoutAction::Decode;
    if (j.q.isEmpty())
        return PlayoutAction::Wait;
    if (j.q.firstKey() - j.next > 50) {
        j.next = j.q.firstKey();
        return PlayoutAction::Resync;
    }
    return PlayoutAction::Conceal;
}

void VoiceAudio::onPlayTick() {
    if (!m_running || !m_sink || !m_sinkIo || !m_loaded)
        return;
    if (m_tickGap.isValid()) {
        const qint64 gap = m_tickGap.restart();
        if (gap > 40)
            qWarning("[voice] play tick late by %lld ms (audio thread starved)", (long long)gap);
    } else {
        m_tickGap.start();
    }
    if (!m_statClock.isValid())
        m_statClock.start();
    if (m_statClock.elapsed() >= 1000) {
        if (m_stats.rx)
            qWarning("[voice] rx=%d played=%d plc=%d late=%d short_write=%d | drop: hdr=%d own=%d unmapped=%d transport=%d dave=%d",
                      m_stats.rx, m_stats.played, m_stats.plc, m_stats.late,
                      m_stats.shortWrite,
                      m_stats.badHeader, m_stats.own, m_stats.unmapped,
                      m_stats.transport, m_stats.dave);
        m_stats = Stats();
        m_statClock.restart();
    }
    for (int round = 0; round < 4; ++round) {
        const int free = m_sink->bytesFree();
        if (free < kFrameBytes)
            return;
        if (m_sink->bufferSize() - free >= kFrameBytes * 6)
            return;  // enough queued; stay ~120 ms ahead to absorb GUI stalls

        int acc[kFrameSamples * kChannels] = {0};
        bool any = false;
        for (auto it = m_jitter.begin(); it != m_jitter.end(); ++it) {
            Jitter &j = it.value();
            const PlayoutAction action = nextPlayoutAction(j);
            if (action == PlayoutAction::Wait || action == PlayoutAction::Resync)
                continue;
            void *dec = decoderFor(it.key());
            if (!dec)
                continue;
            short pcm[kFrameSamples * kChannels];
            int n = 0;
            if (action == PlayoutAction::Decode) {
                auto f = j.q.find(j.next);
                const QByteArray &opus = f.value();
                n = m_dlls->decode((OpusDecoder *)dec,
                                   (const unsigned char *)opus.constData(),
                                   opus.size(), pcm, kFrameSamples, 0);
                j.q.erase(f);
            } else {
                // A newer packet proves this sequence was lost.
                ++m_stats.plc;
                n = m_dlls->decode((OpusDecoder *)dec, nullptr, 0, pcm, kFrameSamples, 0);
            }
            ++j.next;
            if (n <= 0)
                continue;
            if (n > kFrameSamples)
                n = kFrameSamples;
            for (int i = 0; i < n * kChannels; ++i)
                acc[i] += pcm[i];
            any = true;
        }
        if (!any) {
            if (m_sink->bufferSize() - m_sink->bytesFree() < kFrameBytes) {
                for (Jitter &j : m_jitter) {
                    if (j.q.isEmpty())
                        j.started = false;
                }
            }
            return;
        }
        short out[kFrameSamples * kChannels];
        for (int i = 0; i < kFrameSamples * kChannels; ++i)
            out[i] = (short)qBound(-32768, acc[i], 32767);
        if (m_sinkIo->write((const char *)out, kFrameBytes) != kFrameBytes)
            ++m_stats.shortWrite;
        ++m_stats.played;
    }
}

void VoiceAudio::setSpeakingUser(const QString &userId, quint32 ssrc) {
    if (userId.isEmpty() || !ssrc)
        return;
    m_ssrcUsers.insert(ssrc, userId);
    const QList<QByteArray> backlog = m_unmapped.take(ssrc);
    for (const QByteArray &pkt : backlog)
        handleRtpPacket(pkt);
}

void VoiceAudio::removeUser(const QString &userId) {
    for (auto it = m_ssrcUsers.begin(); it != m_ssrcUsers.end();) {
        if (it.value() == userId)
            it = m_ssrcUsers.erase(it);
        else
            ++it;
    }
    m_jitter.remove(userId);
    if (void *dec = m_decoders.take(userId)) {
        if (m_dlls && m_dlls->decoderDestroy)
            m_dlls->decoderDestroy((OpusDecoder *)dec);
    }
    if (void *dec = m_daveDecs.take(userId)) {
        if (m_dlls && m_dlls->decryptorDestroy)
            m_dlls->decryptorDestroy((DAVEDecryptorHandle)dec);
    }
}

void *VoiceAudio::ensureDaveDecryptor(const QString &userId) {
    if (!m_dlls || !m_session || userId.isEmpty() || userId == m_selfUserId)
        return nullptr;
    void *dec = m_daveDecs.value(userId);
    const bool created = !dec;
    if (!dec) {
        dec = m_dlls->decryptorCreate();
        if (!dec)
            return nullptr;
        m_daveDecs.insert(userId, dec);
    }
    const QByteArray uid = userId.toUtf8();
    void *ratchet = m_dlls->sessionGetKeyRatchet((DAVESessionHandle)m_session,
                                                 uid.constData());
    if (!ratchet) {
        if (created)
            qWarning("[voice] dave decryptor for %s has no key ratchet yet",
                     qPrintable(userId));
        m_dlls->decryptorToPassthrough((DAVEDecryptorHandle)dec, true);
        return dec;
    }
    // The C API copies the ratchet, so the handle can go away immediately. Old
    // cryptor managers stay installed in the decryptor until they expire, which
    // is what keeps frames from the previous epoch decryptable.
    m_dlls->decryptorToRatchet((DAVEDecryptorHandle)dec,
                               (DAVEKeyRatchetHandle)ratchet);
    m_dlls->decryptorToPassthrough((DAVEDecryptorHandle)dec, false);
    m_dlls->keyRatchetDestroy((DAVEKeyRatchetHandle)ratchet);
    return dec;
}

void VoiceAudio::prepareDaveDecryptors(const QStringList &users) {
    if (m_daveVersion == 0 || !m_session)
        return;
    for (const QString &u : users) {
        if (!u.isEmpty() && u != m_selfUserId)
            ensureDaveDecryptor(u);
    }
}

void VoiceAudio::installDaveEncryptorRatchet() {
    if (!m_dlls || !m_session || !m_encryptor || m_selfUserId.isEmpty())
        return;
    const QByteArray uid = m_selfUserId.toUtf8();
    void *ratchet = m_dlls->sessionGetKeyRatchet((DAVESessionHandle)m_session,
                                                 uid.constData());
    if (!ratchet)
        return;
    m_dlls->encryptorSetKeyRatchet((DAVEEncryptorHandle)m_encryptor,
                                   (DAVEKeyRatchetHandle)ratchet);
    m_dlls->encryptorSetPassthrough((DAVEEncryptorHandle)m_encryptor, false);
    m_dlls->keyRatchetDestroy((DAVEKeyRatchetHandle)ratchet);
    m_encryptorReady = true;
    logDaveEpoch("execute");
    qWarning("[voice] DAVE epoch active (encrypting)");
}

void VoiceAudio::logDaveEpoch(const char *where) {
    if (!m_dlls || !m_session || !m_dlls->sessionEpochAuth)
        return;
    uint8_t *auth = nullptr;
    size_t len = 0;
    m_dlls->sessionEpochAuth((DAVESessionHandle)m_session, &auth, &len);
    if (!auth)
        return;
    const QByteArray bytes((const char *)auth, (int)len);
    m_dlls->freeBytes(auth);
    qWarning("[voice] dave epoch %s auth=%s", where,
             bytes.toHex().left(8).constData());
}

void VoiceAudio::resetDaveSession() {
    m_encryptorReady = false;
    for (void *dec : m_daveDecs) {
        if (m_dlls && m_dlls->decryptorDestroy)
            m_dlls->decryptorDestroy((DAVEDecryptorHandle)dec);
    }
    m_daveDecs.clear();
    m_pendingExternalSender.clear();
    if (m_encryptor) {
        m_dlls->encryptorDestroy((DAVEEncryptorHandle)m_encryptor);
        m_encryptor = nullptr;
    }
    if (m_session) {
        m_dlls->sessionDestroy((DAVESessionHandle)m_session);
        m_session = nullptr;
    }
}

void VoiceAudio::setDaveProtocolVersion(int version) {
    m_daveVersion = version;
    if (!m_loaded || m_selfUserId.isEmpty() || m_channelId == 0)
        return;
    if (version <= 0) {
        resetDaveSession();
        return;
    }
    if (!m_session) {
        m_session = m_dlls->sessionCreate(nullptr, nullptr, nullptr, nullptr);
        if (!m_session) {
            qWarning("[voice] daveSessionCreate failed");
            return;
        }
        m_encryptor = m_dlls->encryptorCreate();
        if (!m_encryptor) {
            qWarning("[voice] daveEncryptorCreate failed");
            return;
        }
        if (m_ssrc)
            m_dlls->encryptorAssignSsrc((DAVEEncryptorHandle)m_encryptor, m_ssrc,
                                        DAVE_CODEC_OPUS);
        m_dlls->encryptorSetPassthrough((DAVEEncryptorHandle)m_encryptor, true);
    }
    const QByteArray uid = m_selfUserId.toUtf8();
    if (!m_pendingExternalSender.isEmpty()) {
        m_dlls->sessionSetExternalSender((DAVESessionHandle)m_session,
                                         (const uint8_t *)m_pendingExternalSender.constData(),
                                         m_pendingExternalSender.size());
        m_pendingExternalSender.clear();
    }
    m_dlls->sessionInit((DAVESessionHandle)m_session, (uint16_t)version,
                        m_channelId, uid.constData());
    m_encryptorReady = false;
    logDaveEpoch("init");

    size_t len = 0;
    uint8_t *kp = nullptr;
    m_dlls->sessionGetKeyPackage((DAVESessionHandle)m_session, &kp, &len);
    if (kp && len > 0) {
        emit daveKeyPackageReady(QByteArray((const char *)kp, (int)len));
        m_dlls->freeBytes(kp);
    }
}

void VoiceAudio::prepareDaveEpoch(int epoch, int version) {
    if (epoch == 1)
        setDaveProtocolVersion(version);
}

bool VoiceAudio::prepareDaveTransition(int transitionId, int version,
                                       const QStringList &clients) {
    prepareDaveDecryptors(clients);
    logDaveEpoch("prepare");
    if (version == 0) {
        m_encryptorReady = false;
        if (m_encryptor)
            m_dlls->encryptorSetPassthrough((DAVEEncryptorHandle)m_encryptor, true);
    }
    if (transitionId == 0) {
        m_daveVersion = version;
        if (version > 0)
            installDaveEncryptorRatchet();
        return false;
    }
    m_pending.insert(transitionId, version);
    return true;
}

void VoiceAudio::executeDaveTransition(int transitionId) {
    if (!m_pending.contains(transitionId))
        return;
    const int version = m_pending.take(transitionId);
    m_daveVersion = version;
    if (version == 0) {
        m_encryptorReady = false;
        if (m_encryptor)
            m_dlls->encryptorSetPassthrough((DAVEEncryptorHandle)m_encryptor, true);
        return;
    }
    installDaveEncryptorRatchet();
}

void VoiceAudio::setDaveExternalSender(const QByteArray &payload) {
    if (payload.isEmpty())
        return;
    if (!m_session) {
        m_pendingExternalSender = payload;
        return;
    }
    m_dlls->sessionSetExternalSender((DAVESessionHandle)m_session,
                                     (const uint8_t *)payload.constData(),
                                     payload.size());
}

bool VoiceAudio::processDaveCommit(const QByteArray &payload,
                                   const QStringList &clients, int *transitionId) {
    *transitionId = 0;
    if (payload.size() <= 2)
        return false;
    *transitionId = ((quint8)payload[0] << 8) | (quint8)payload[1];
    if (!m_session)
        return false;
    DAVECommitResultHandle result = m_dlls->sessionProcessCommit(
        (DAVESessionHandle)m_session, (const uint8_t *)payload.constData() + 2,
        payload.size() - 2);
    if (!result) {
        emit daveInvalidateTransition(*transitionId);
        return false;
    }
    const bool failed = m_dlls->commitIsFailed(result);
    const bool ignored = m_dlls->commitIsIgnored(result);
    m_dlls->commitDestroy(result);
    if (failed) {
        emit daveInvalidateTransition(*transitionId);
        setDaveProtocolVersion(m_daveVersion);
        return false;
    }
    if (!ignored)
        prepareDaveTransition(*transitionId, m_daveVersion, clients);
    return true;
}

bool VoiceAudio::processDaveWelcome(const QByteArray &payload,
                                    const QStringList &clients, int *transitionId) {
    *transitionId = 0;
    if (payload.size() <= 2)
        return false;
    *transitionId = ((quint8)payload[0] << 8) | (quint8)payload[1];
    if (!m_session)
        return false;
    QList<QByteArray> ids;
    for (const QString &c : clients)
        ids << c.toUtf8();
    if (!ids.contains(m_selfUserId.toUtf8()))
        ids << m_selfUserId.toUtf8();
    QVector<const char *> ptrs;
    ptrs.reserve(ids.size());
    for (const QByteArray &id : ids)
        ptrs << id.constData();
    DAVEWelcomeResultHandle result = m_dlls->sessionProcessWelcome(
        (DAVESessionHandle)m_session, (const uint8_t *)payload.constData() + 2,
        payload.size() - 2, ptrs.data(), (size_t)ptrs.size());
    if (!result) {
        emit daveInvalidateTransition(*transitionId);
        return false;
    }
    m_dlls->welcomeDestroy(result);
    prepareDaveTransition(*transitionId, m_daveVersion, clients);
    return true;
}

QByteArray VoiceAudio::processDaveProposals(const QByteArray &payload,
                                            const QStringList &clients) {
    if (!m_session || payload.isEmpty())
        return QByteArray();
    QList<QByteArray> ids;
    for (const QString &c : clients)
        ids << c.toUtf8();
    QVector<const char *> ptrs;
    ptrs.reserve(ids.size());
    for (const QByteArray &id : ids)
        ptrs << id.constData();

    size_t len = 0;
    uint8_t *out = nullptr;
    m_dlls->sessionProcessProposals((DAVESessionHandle)m_session,
                                    (const uint8_t *)payload.constData(),
                                    payload.size(), ptrs.data(),
                                    (size_t)ptrs.size(), &out, &len);
    QByteArray result;
    if (out && len > 0) {
        result = QByteArray((const char *)out, (int)len);
        m_dlls->freeBytes(out);
    } else if (out) {
        m_dlls->freeBytes(out);
    }
    return result;
}

bool VoiceAudio::loopbackTest() {
    if (!loadLibraries())
        return false;
    m_secret = QByteArray(32, '\x11');
    m_ssrc = 0x11223344u;
    m_running = true;
    const QByteArray opus = QByteArray::fromHex("f8fffe") + QByteArray("hello-opus");
    QByteArray packet;
    auto conn = connect(this, &VoiceAudio::rtpPacketReady, this,
                        [&packet](const QByteArray &p) { packet = p; });
    sendOpus(opus);
    disconnect(conn);
    m_running = false;

    const QByteArray back = decryptTransport(packet, headerLengthFor(packet));
    const bool ok = back == opus;
    if (!ok)
        qWarning("[voice-audio-selftest] transport roundtrip mismatch (in=%d pkt=%d out=%d)",
                 opus.size(), packet.size(), back.size());
    return ok;
}

bool VoiceAudio::selfTest() {
    bool ok = true;
    auto fail = [&ok](const char *message) {
        qWarning("[voice-selftest] %s", message);
        ok = false;
    };
    QByteArray plain(12, '\0');
    plain[0] = 0x80;
    if (headerLengthFor(plain) != 12) {
        qWarning("[voice-selftest] rtp header length (no extension) mismatch");
        ok = false;
    }
    plain[0] = char(0x90);
    if (headerLengthFor(plain) != 16) {
        qWarning("[voice-selftest] rtp header length (extension) mismatch");
        ok = false;
    }
    if (kSilence.toHex() != QByteArray("f8fffe")) {
        qWarning("[voice-selftest] silence frame mismatch");
        ok = false;
    }

    Jitter j;
    j.have = true;
    j.q.insert(100, QByteArray("a"));
    j.q.insert(101, QByteArray("b"));
    if (nextPlayoutAction(j) != PlayoutAction::Wait || j.started || j.next != 0)
        fail("started below prebuffer target");

    j.q.clear();
    for (qint64 seq = 100; seq < 106; ++seq)
        j.q.insert(seq, QByteArray("x"));
    if (nextPlayoutAction(j) != PlayoutAction::Decode || !j.started || j.next != 100)
        fail("did not start at prebuffer target");

    j.q.clear();
    const qint64 before = j.next;
    if (nextPlayoutAction(j) != PlayoutAction::Wait || j.next != before)
        fail("empty queue advanced sequence");

    j.q.insert(before + 1, QByteArray("future"));
    if (nextPlayoutAction(j) != PlayoutAction::Conceal || j.next != before)
        fail("proven gap did not request PLC");
    return ok;
}
