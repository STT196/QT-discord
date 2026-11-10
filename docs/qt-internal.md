# Internal Qt API

## VoiceClient

```cpp
void connectVoice(endpoint, token, serverId, userId, sessionId, channelId);
void leave();
void setMicEnabled(bool enabled);
bool isConnected() const;
QString status() const;
static bool selfTest();
```

Properties: `connected`, `status`.
Signals: `statusChanged()`, `connectedChanged()`, `readyReceived(int ssrc)`, `sessionDescriptionReceived(QString mode)`, `errorOccurred(QString message)`.

For DMs pass the DM channel ID as both `serverId` and `channelId`.

## VoiceAudio

```cpp
bool loadLibraries();   // opus.dll, libsodium.dll, libdave.dll from exe dir
bool isLoaded() const;
void configure(selfUserId, channelId, secret, ssrc, mode);
void start(); void stop();
void setMicEnabled(bool);
void handleRtpPacket(const QByteArray &);
void setSpeakingUser(userId, ssrc);
void removeUser(userId);
static bool selfTest();
bool loopbackTest();
```

### DAVE

```cpp
void setDaveProtocolVersion(int);
void prepareDaveEpoch(int epoch, int version);
bool prepareDaveTransition(int id, int version);
void executeDaveTransition(int id);
bool processDaveCommit(const QByteArray &, int *id);
bool processDaveWelcome(const QByteArray &, const QStringList &clients, int *id);
void setDaveExternalSender(const QByteArray &);
QByteArray processDaveProposals(const QByteArray &, const QStringList &clients);
```

Signals: `daveKeyPackageReady(QByteArray)`, `daveInvalidateTransition(int)`, `rtpPacketReady(QByteArray)`.

Without the DLLs the client runs signalling-only and sends `max_dave_protocol_version: 0`.

## DiscordClient

Not documented: `discordclient.cpp` was not provided.
