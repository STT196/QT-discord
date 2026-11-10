# Voice & DM Call API Reference

Reference for the Qt voice client (`VoiceClient`, `VoiceAudio`). Discord user-side voice behavior is undocumented; verify against live traffic when in doubt.

## 1. Flow Overview

1. Main gateway: send op 4 (Voice State Update).
2. Receive `VOICE_STATE_UPDATE` (gives `session_id`) and `VOICE_SERVER_UPDATE` (gives `token`, `endpoint`).
3. Call `VoiceClient::connectVoice(...)`.
4. Voice gateway: Hello → Identify → Ready → UDP discovery → Select Protocol → Session Description.
5. Audio flows over UDP (Opus → DAVE → libsodium transport encryption).

## 2. Main Gateway (DM / Group DM calls)

### Op 4: Voice State Update (send)

```json
{"op": 4, "d": {"guild_id": null, "channel_id": "<dm_channel_id>", "self_mute": false, "self_deaf": false}}
```

- DM calls: `guild_id` is `null`, `channel_id` is the DM or group DM channel.
- Joining starts the call if none exists.
- Leave: `channel_id: null`.

### Events (receive)

| Event | Key fields | Use |
|---|---|---|
| `VOICE_STATE_UPDATE` | `session_id`, `channel_id`, `user_id` | Store own `session_id` |
| `VOICE_SERVER_UPDATE` | `token`, `endpoint`, `channel_id` (DM) or `guild_id` | Connect voice. If `endpoint` is null, tear down and wait for the next update |
| `CALL_CREATE` | `channel_id`, `ringing[]` | Incoming call |
| `CALL_UPDATE` / `CALL_DELETE` | `channel_id` | Ring changes / call ended |

### REST

| Method | Path | Body | Purpose |
|---|---|---|---|
| POST | `/channels/{channel_id}/call/ring` | `{"recipients": null}` or `["user_id", ...]` | Ring users |
| POST | `/channels/{channel_id}/call/stop-ringing` | `{"recipients": null}` | Decline / stop ringing |

## 3. VoiceClient API

```cpp
void connectVoice(endpoint, token, serverId, userId, sessionId, channelId);
void leave();
void setMicEnabled(bool enabled);
bool isConnected() const;
QString status() const;
static bool selfTest();
```

- For DMs pass the **DM channel ID** as both `serverId` and `channelId`.
- Endpoint is parsed to host/port; path is `/?v=8`.

**Signals:** `statusChanged()`, `connectedChanged()`, `readyReceived(int ssrc)`, `sessionDescriptionReceived(QString mode)`, `errorOccurred(QString message)`.

## 4. Voice Gateway (v8)

### WebSocket handshake

- `GET /?v=8`, `Sec-WebSocket-Key` = base64 of 16 random bytes, version 13.
- No extensions. All client frames must be masked.

### Text opcodes

| Op | Dir | Name | Notes |
|---|---|---|---|
| 0 | C→S | Identify | `server_id`, `user_id`, `session_id`, `token`, `max_dave_protocol_version` |
| 1 | C→S | Select Protocol | `protocol: "udp"`, `data: {address, port, mode}` |
| 2 | S→C | Ready | `ssrc`, `ip`, `port`, `modes[]` |
| 3 | C→S | Heartbeat | `{t, seq_ack}` |
| 4 | S→C | Session Description | `secret_key[]`, `mode`, `dave_protocol_version` |
| 5 | both | Speaking | `speaking`, `delay`, `ssrc` (+ `user_id` inbound) |
| 8 | S→C | Hello | `heartbeat_interval` |
| 11 | S→C | Clients Connect | `user_ids[]` |
| 13 | S→C | Client Disconnect | `user_id` |
| 21 | S→C | DAVE Prepare Transition | `transition_id`, `protocol_version` |
| 22 | S→C | DAVE Execute Transition | `transition_id` |
| 23 | C→S | DAVE Transition Ready | `transition_id` |
| 24 | S→C | DAVE Prepare Epoch | `epoch`, `protocol_version` |
| 31 | C→S | DAVE Invalidate Commit/Welcome | `transition_id` |

### Binary opcodes (DAVE / MLS)

- Server→client frame: `[seq u16 BE][op u8][payload]`
- Client→server frame: `[op u8][payload]`

| Op | Dir | Name |
|---|---|---|
| 25 | S→C | External Sender |
| 26 | C→S | Key Package |
| 27 | S→C | Proposals |
| 28 | C→S | Commit / Welcome |
| 29 | S→C | Announce Commit Transition |
| 30 | S→C | Welcome |

### Encryption modes (preference order)

1. `aead_xchacha20_poly1305_rtpsize`
2. `aead_xchacha20_poly1305`
3. `xsalsa20_poly1305`

## 5. UDP

**IP discovery** (74 bytes, big-endian):

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Type: `0x0001` request, `0x0002` reply |
| 2 | 2 | Length: `0x0046` |
| 4 | 4 | SSRC |
| 8 | 64 | Address (null-terminated, reply only) |
| 72 | 2 | Port (reply only) |

After discovery, send Select Protocol with the discovered address and port.

## 6. VoiceAudio API

```cpp
bool loadLibraries();   // opus.dll, libsodium.dll, libdave.dll from exe dir
void configure(selfUserId, channelId, secret, ssrc, mode);
void start(); void stop();
void setMicEnabled(bool);
void handleRtpPacket(const QByteArray &);
void setSpeakingUser(userId, ssrc);
void removeUser(userId);
```

**DAVE:** `setDaveProtocolVersion`, `prepareDaveEpoch`, `prepareDaveTransition`, `executeDaveTransition`, `processDaveCommit`, `processDaveWelcome`, `setDaveExternalSender`, `processDaveProposals`.

**Signals:** `daveKeyPackageReady(QByteArray)`, `daveInvalidateTransition(int)`, `rtpPacketReady(QByteArray)`.

If the DLLs are missing, the client runs signalling-only (`max_dave_protocol_version` is sent as 0).

## 7. Constraints

- DM calls require a user token; bot accounts cannot join them. Using user tokens violates Discord's ToS.
- DAVE group ID = channel ID.
- Heartbeat `seq_ack` must track the last `seq` seen (binary header and JSON `seq`).
