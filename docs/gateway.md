# Main Gateway

Connect: `wss://gateway.discord.gg/?v=9&encoding=json` (discover with `GET /gateway`).

## Opcodes

| Op | Dir | Name | Notes |
|---|---|---|---|
| 0 | S→C | Dispatch | `t` event name, `s` sequence, `d` data |
| 1 | both | Heartbeat | `d` = last `s` or null |
| 2 | C→S | Identify | token, properties, presence, capabilities |
| 3 | C→S | Presence Update | status, activities |
| 4 | C→S | Voice State Update | join/leave voice or DM call |
| 6 | C→S | Resume | token, session_id, seq |
| 7 | S→C | Reconnect | reconnect, then Resume |
| 8 | C→S | Request Guild Members | |
| 9 | S→C | Invalid Session | `d` true = resumable, else re-Identify |
| 10 | S→C | Hello | `heartbeat_interval` ms |
| 11 | S→C | Heartbeat ACK | missing ACK = zombie, reconnect |

## Lifecycle

1. Connect, receive Hello (10).
2. Send first Heartbeat after `interval * random(0..1)`, then every `interval`.
3. Send Identify (2), receive `READY`.
4. Track `s` on every Dispatch. On disconnect, Resume (6) using `session_id` and `resume_gateway_url` from `READY`.
5. Do not reconnect on fatal close codes (4004 bad token, 4010-4014).

## Identify (user client)

```json
{"op": 2, "d": {
  "token": "<token>",
  "capabilities": 16381,
  "properties": {"os": "Windows", "browser": "Chrome", "device": ""},
  "presence": {"status": "online", "since": 0, "activities": [], "afk": false},
  "compress": false
}}
```

`capabilities` and `properties` change over time; copy a current web client's values.

## Voice State Update (op 4)

```json
{"op": 4, "d": {"guild_id": null, "channel_id": "<id>", "self_mute": false, "self_deaf": false}}
```

- Guild voice: `guild_id` set. DM call: `guild_id` null.
- Leave: `channel_id` null.

## Dispatch Events

| Group | Events |
|---|---|
| Session | `READY`, `READY_SUPPLEMENTAL`, `RESUMED`, `SESSIONS_REPLACE` |
| Messages | `MESSAGE_CREATE`, `MESSAGE_UPDATE`, `MESSAGE_DELETE`, `MESSAGE_DELETE_BULK`, `MESSAGE_ACK` |
| Reactions | `MESSAGE_REACTION_ADD`, `MESSAGE_REACTION_REMOVE`, `MESSAGE_REACTION_REMOVE_ALL` |
| Typing / presence | `TYPING_START`, `PRESENCE_UPDATE` |
| Channels | `CHANNEL_CREATE`, `CHANNEL_UPDATE`, `CHANNEL_DELETE`, `CHANNEL_PINS_UPDATE` |
| Threads | `THREAD_CREATE`, `THREAD_UPDATE`, `THREAD_DELETE`, `THREAD_LIST_SYNC`, `THREAD_MEMBER_UPDATE` |
| Guilds | `GUILD_CREATE`, `GUILD_UPDATE`, `GUILD_DELETE` |
| Members / roles | `GUILD_MEMBER_ADD`, `GUILD_MEMBER_UPDATE`, `GUILD_MEMBER_REMOVE`, `GUILD_ROLE_CREATE`, `GUILD_ROLE_UPDATE`, `GUILD_ROLE_DELETE` |
| Voice | `VOICE_STATE_UPDATE` (gives `session_id`), `VOICE_SERVER_UPDATE` (`token`, `endpoint`) |
| Calls | `CALL_CREATE`, `CALL_UPDATE`, `CALL_DELETE` |
| Social | `RELATIONSHIP_ADD`, `RELATIONSHIP_REMOVE` |
