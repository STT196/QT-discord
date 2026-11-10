# REST API

- Base: `https://discord.com/api/v9`
- Auth: `Authorization: <token>` (user token, no `Bot ` prefix)
- JSON bodies with `Content-Type: application/json`
- IDs are snowflake strings

## Rate Limits

Read `X-RateLimit-Remaining` and `X-RateLimit-Reset-After`. On 429, wait `retry_after` seconds. Never retry in a tight loop.

## Users and Relationships

| Method | Path | Purpose |
|---|---|---|
| GET | `/users/@me` | Current user |
| GET | `/users/{id}/profile` | User profile |
| GET | `/users/@me/guilds` | Guild list |
| GET | `/users/@me/channels` | Open DMs |
| POST | `/users/@me/channels` | Open DM: `{"recipients": ["<id>"]}` (several = group DM) |
| GET | `/users/@me/relationships` | Friends / blocked / pending |
| PUT | `/users/@me/relationships/{id}` | Send request / accept |
| DELETE | `/users/@me/relationships/{id}` | Remove / decline |

## Messages

| Method | Path | Purpose |
|---|---|---|
| GET | `/channels/{id}/messages?limit=50&before=&after=&around=` | History (limit ≤ 100) |
| POST | `/channels/{id}/messages` | Send: `{content, nonce, message_reference?, attachments?}` |
| PATCH | `/channels/{id}/messages/{mid}` | Edit |
| DELETE | `/channels/{id}/messages/{mid}` | Delete |
| POST | `/channels/{id}/messages/{mid}/ack` | Mark read |
| PUT / DELETE | `/channels/{id}/messages/{mid}/reactions/{emoji}/@me` | Add / remove reaction (URL-encode emoji; custom = `name:id`) |
| POST | `/channels/{id}/typing` | Typing indicator (~10 s) |
| GET | `/channels/{id}/pins` | Pinned messages |
| PUT / DELETE | `/channels/{id}/pins/{mid}` | Pin / unpin |

## Attachments

1. `POST /channels/{id}/attachments` with `{"files": [{"id": "0", "filename": "a.png", "file_size": 1234}]}`. Returns `upload_url` and `upload_filename`.
2. `PUT <upload_url>` with the raw bytes.
3. Send the message with `"attachments": [{"id": "0", "filename": "a.png", "uploaded_filename": "<upload_filename>"}]`.

## Guilds and Channels

| Method | Path | Purpose |
|---|---|---|
| GET | `/guilds/{id}` | Guild info |
| GET | `/guilds/{id}/channels` | Channels |
| GET | `/guilds/{id}/members/{uid}` | Member |
| GET | `/guilds/{id}/roles` | Roles |
| GET | `/channels/{id}` | Channel |
| POST | `/channels/{id}/threads` | Create thread |
| GET | `/invites/{code}` | Invite info |
| POST | `/invites/{code}` | Accept invite |

## Calls

| Method | Path | Body |
|---|---|---|
| POST | `/channels/{id}/call/ring` | `{"recipients": null}` or `["<user_id>"]` |
| POST | `/channels/{id}/call/stop-ringing` | `{"recipients": null}` |

## Voice Regions

| Method | Path | Purpose |
|---|---|---|
| GET | `/voice/regions` | List regions (`id`, `name`, `optimal`, `deprecated`, `custom`) |
| GET | `/guilds/{id}/regions` | Regions available to a guild |
| PATCH | `/channels/{id}` | Set voice channel region: `{"rtc_region": "singapore"}` (`null` = automatic) |
| PATCH | `/channels/{id}/call` | Set DM call region: `{"region": "singapore"}` |

Region IDs include `us-east`, `us-west`, `us-central`, `us-south`, `singapore`, `hongkong`, `japan`, `sydney`, `india`, `rotterdam`, `brazil`, `southafrica`. There is no literal `asia` ID. Regions change over time, so treat `GET /voice/regions` as the source of truth.