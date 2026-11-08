# QtDiscord Clone — Development Plan

Goal: rebuild Discord's desktop client in Qt (C++ + QML), feature-parity where feasible.
REA supplies protocol facts from the installed Electron app; Qt/C++/QML is hand-written.

Reality check baked into ordering: Discord's client is ~99% a web app driven by
undocumented server protocols. Phases 1–3 (text/media) are achievable by REA + Qt.
Phases 4–6 (voice/video/SFU) depend on private media protocols and are high-risk.

---

## Phase 0 — Foundations (DONE)

- [x] REA: extract `app.asar`, recover endpoints
      (REST `discord.com/api`, Gateway `wss://gateway.discord.gg/?v=10&encoding=json`, CDN `cdn.discordapp.com`)
- [x] Manual WebSocket framing on `QSslSocket` (no Qt6WebSockets for 6.12)
- [x] REST bootstrap, IDENTIFY, heartbeat
- [x] QML shell: guild sidebar, channel list, message list, send bar
- [x] Bot + user token auth (`Authorization: Bot <t>` vs raw `<t>`)

---

## Phase 1 — Complete text client

Deliverable: usable text-only Discord.

- [ ] Multi-guild switching (store guild ids; refetch channels on select)
- [ ] Multi-channel switching (per-channel message cache + history)
- [ ] Message pagination (REST `?before=<id>&limit=50` on scroll-up)
- [ ] Reconnect/resume (op 7 → reconnect; RESUME op 6 + session_id + last seq)
- [ ] Heartbeat ACK tracking (zombie-connection detection)
- [ ] Rate-limit handling (HTTP 429 + `retry_after`, `X-RateLimit-*`; per-route buckets)
- [ ] Correct intents (user vs bot differing privileged intents)
- [ ] Message ordering/dedup by `id` (snowflake monotonic)

REA inputs: gateway `op` reference, RESUME payload shape, rate-limit headers.

---

## Phase 2 — Full message features

- [ ] `MESSAGE_UPDATE` / `MESSAGE_DELETE` / `_BULK_DELETE`
- [ ] Replies (message_reference), mentions, @everyone rendering
- [ ] Reactions (REST `PUT/DELETE /channels/{c}/messages/{m}/reactions/...` + `MESSAGE_REACTION_ADD/REMOVE`)
- [ ] Attachments download/upload (REST multipart `/channels/{c}/messages` upload; CDN fetch)
- [ ] Embeds: parse `embeds[]` (type, title, fields, image/thumbnail)
- [ ] Markdown subset: bold/italic/code/spoiler/links/emoji (`<:name:id>` custom emoji)
- [ ] Typing indicator (`TYPING_START`, `POST /channels/{c}/typing`)
- [ ] DMs / group DMs (`CHANNEL_CREATE` type 1/3, `GET /users/@me/channels`)

---

## Phase 3 — Channels & guild model

- [ ] Threads (`THREAD_CREATE/UPDATE`, thread channels)
- [ ] Categories, channel ordering (by `position`), voice channel entries (list only)
- [ ] Guild members (`GUILD_MEMBER_*`, `GET /guilds/{g}/members`)
- [ ] Roles + permission bitfields (compute `@everyone` + member role bitmask client-side)
- [ ] Presence (`PRESENCE_UPDATE`), status, activity
- [ ] Pinned messages, message search (`GET /guilds/{g}/messages/search`)
- [ ] Notifications (unread counts `READY.read_state`, `ACK`)

REA inputs: permission flag values, channel type enum, member/role payload shapes.

---

## Phase 4 — Voice

Risk: HIGH. Requires WebRTC media pipeline + Opus; SFU protocol is private.

- [ ] Voice gateway connect (`op 4` voice state update → voice server update)
- [ ] Voice WS handshake (op 0 identify / op 1 heartbeat)
- [ ] UDP hole-punch / IP discovery, encryption (aead_xchacha20_poly1305 / xsalsa20)
- [ ] Opus encode/decode (bundle libopus)
- [ ] jitter buffer + RTP
- [ ] Qt audio I/O (`QAudioSource`/`QAudioSink`)
- [ ] Deaf/mute/join/leave UI

REA inputs: voice op codes, encryption mode negotiation from the client bundle.

---

## Phase 5 — Video / screen share

Risk: VERY HIGH. VP8/VP9/AV1 + RTP + SFU simulcast.

- [ ] Camera capture (`QCamera`)
- [ ] Screen capture (native per-OS)
- [ ] Video encode (libvpx/libaom) + RTP payload
- [ ] Simulcast + `Go Live` (RTMP/SFU) endpoints

---

## Phase 6 — Platform features

- [ ] Slash commands / application commands (`INTERACTION_CREATE`, modals)
- [ ] Stickers, soundboard, GIF picker
- [ ] Settings persistence (`QSettings`), themes
- [ ] Multi-account / profile switching
- [ ] Rich presence / activity

---

## Architecture targets (refactor as it grows)

```
src/
  net/          gateway WS + REST buckets + rate limiter
  model/        guilds, channels, messages, members (QAbstractListModel)
  features/     reactions, embeds, voice, media
  ui/           QML views + C++ QML-exposed bridges
qml/
  main.qml, Guilds.qml, Channels.qml, Messages.qml, Composer.qml, Voice.qml
```

Rules:
- Keep gateway/REST on the Qt event loop; no blocking.
- One rate-limit bucket manager shared by all REST calls.
- Snowflake ids as `quint64` / string (never int32).

---

## Verification per phase

- Unit: snowflake ordering, rate-limit backoff, markdown parser, permission bitmask.
- Integration: scripted gateway frames (op 10/11/0) fed into the client; assert state.
- Manual: run against a real bot token in a test guild.

---

## Honest scope statement

- Phases 1–3: realistic with REA + Qt.
- Phase 4–5: days-to-weeks each; success not guaranteed without reverse-engineering
  private media protocols that the client bundle only partially reveals.
- Phase 6: open-ended; Discord ships new features continuously.
- "1:1" is not attainable; "feature-complete for my use" is.
