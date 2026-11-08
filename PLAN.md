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

- [x] Multi-guild switching (store guild ids; refetch channels on select)
- [x] Multi-channel switching (per-channel message cache + history)
- [x] Message pagination (REST `?before=<id>&limit=50`)
- [x] Reconnect/resume (op 7 → reconnect; RESUME op 6 + session_id + last seq)
- [x] Heartbeat ACK tracking (zombie-connection detection)
- [x] Rate-limit handling (HTTP 429 + `retry_after` retry)
- [x] Correct intents (configurable `--intents`; bot vs user)
- [x] Message ordering/dedup by `id` (snowflake monotonic)

REA inputs: gateway `op` reference, RESUME payload shape, rate-limit headers.

---

## Phase 2 — Full message features

- [x] `MESSAGE_UPDATE` / `MESSAGE_DELETE` (gateway → cache sync). `_BULK_DELETE` still pending
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

## Phase UI — Client interface (full Discord-like UI)

Deliverable: the app looks and behaves like Discord for text usage.

Struct:
- [ ] Split `main.qml` into `GuildRail.qml`, `ChannelSidebar.qml`, `MessageView.qml`, `MessageDelegate.qml`, `Composer.qml`, `MemberList.qml`, `Settings.qml`, `Theme.qml` (singleton)
- [ ] `SplitView` panes (guild rail / channel sidebar / main / member list), member list collapsible
- [ ] C++ item models (`QAbstractListModel`) for guilds / channels / messages / members (replace QVariantList bindings for perf)

Guild rail + sidebar:
- [ ] Guild icons (circular), selection/hover states, home/DM button, unread pill, tooltips
- [ ] Channel categories (collapsible), text/voice icons, unread badges, active highlight, server header

Messages:
- [ ] Avatars (CDN fetch + disk cache), role-colored usernames, timestamps, day dividers
- [ ] Message grouping (consecutive same author), cozy/compact density
- [ ] Markdown: bold/italic/underline/strike/spoiler/inline code/code block/links/blockquote/lists
- [ ] Emoji: unicode + custom `<:name:id>` (from CDN)
- [ ] Embeds: color bar, author, title, description, fields, image, thumbnail, footer
- [ ] Attachments: inline image previews, file chips, open/download
- [ ] Replies (quoted), mention highlight, @everyone/@here styling
- [ ] Reactions bar (emoji + count), add-reaction picker
- [ ] Edited indicator, hover actions (reply/react/edit/delete), system messages
- [ ] Jump-to-present + load-older on scroll

Composer:
- [ ] Multiline input, emoji picker, attach button, Enter-to-send, upload progress
- [ ] Typing indicator ("X is typing…")

Member list & presence:
- [ ] Online/offline grouping, status dots, activity line
- [ ] Own status/activity setter (online/idle/dnd/invisible)

Settings & theme:
- [ ] Settings modal (account/appearance/notifications) persisted via `QSettings`
- [ ] Themes: dark/light + accent; Discord-like palette

Polish:
- [ ] Keyboard nav, focus states, screen-reader labels, responsive to narrow widths
- [ ] Delegate reuse; cache avatars/emoji; avoid whole-list re-render

Acceptance: visually/behaviorally Discord-like for text; no QML warnings; builds clean.

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
