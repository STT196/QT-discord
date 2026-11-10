# DM / Group DM Calls

Voice transport is identical to guild voice. Only main-gateway signaling differs.

## Flow

1. Send gateway op 4 with `guild_id: null` and `channel_id` = DM channel. Joining starts the call.
2. Optionally `POST /channels/{id}/call/ring`. `stop-ringing` cancels.
3. Receive `VOICE_STATE_UPDATE` (own `session_id`) and `VOICE_SERVER_UPDATE` (`token`, `endpoint`, with `channel_id` instead of `guild_id`).
4. Call `VoiceClient::connectVoice(endpoint, token, channelId, userId, sessionId, channelId)`.
5. Voice gateway: Hello, Identify, Ready, UDP discovery, Select Protocol, Session Description (see [voice.md](voice.md)).
6. Leave: op 4 with `channel_id: null`.

## Incoming Calls

- `CALL_CREATE` lists who is being rung.
- Accept: send op 4 with that channel ID.
- Decline: `POST /channels/{id}/call/stop-ringing`.

## Rules

- Identify `server_id` = DM channel ID.
- DAVE group ID = channel ID.
- Keep `max_dave_protocol_version` at 1.
- If `VOICE_SERVER_UPDATE.endpoint` is null (server migration), tear down and wait for the next update.
- DM calls need a user token; bots cannot join.

## Region

- Region is chosen before connecting, not in the voice protocol. The result arrives in `VOICE_SERVER_UPDATE.endpoint` (e.g. `c-sin01-xxxx.discord.media:443`; the prefix hints at the city).
- Guild channels use the channel's `rtc_region` (`null` = automatic, based on the first user to join). DM calls have their own region, automatic by default.
- To change region: `PATCH /channels/{id}/call` with `{"region": "<id>"}` (see [rest.md](rest.md)), then reconnect when the next `VOICE_SERVER_UPDATE` arrives.
- `VoiceClient` needs no region logic; it connects to whatever endpoint it is given.