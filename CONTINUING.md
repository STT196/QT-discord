# Qt Discord Client — built state & how to continue

## What was built (verified)

A minimal, working **text-only Discord client** in Qt 6.12.0 + MinGW 13.1 with a **QML frontend**.

Protocol recovered from Discord's installed `app.asar` (REA):
- REST: `https://discord.com/api`
- Gateway WS: `wss://gateway.discord.gg/?v=10&encoding=json`
- CDN: `https://cdn.discordapp.com`

Files:
| File | Role |
|------|------|
| `CMakeLists.txt` | Qt6 Network + Core + Quick |
| `main.cpp` | QGuiApplication + QQmlApplicationEngine, token from `--token` or `DISCORD_TOKEN` env |
| `discordclient.h/.cpp` | REST bootstrap + gateway WS + heartbeat + IDENTIFY (Q_PROPERTYs for QML binding) |
| `qml/main.qml` | QML UI: guild sidebar, channel list, message view, send bar |
| `qdiscord.qrc` | Qt resource file (currently unused; QML loaded from local path) |

Flow (verified end-to-end with a real bot token):
1. `GET /users/@me` → bot identity
2. `GET /users/@me/guilds` → guilds → pick first
3. `GET /guilds/{id}/channels` → text channels → pick first
4. `GET /channels/{id}/messages?limit=25` → recent messages → renders in QML list
5. Open WS, HTTP upgrade → `101`, `IDENTIFY` (op 2), heartbeat loop
6. `MESSAGE_CREATE` dispatches flow into the QML message list
7. `POST /channels/{id}/messages` → send a message from the QML send bar

## Build

```powershell
# configure once
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:\Qt\6.12.0\mingw_64"

# build
cmake --build build

# run (REQUIRES a Discord token)
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.12.0\mingw_64\bin;$env:PATH"

# Bot token (recommended, sanctioned):
.\build\qdiscord.exe --token <BOT_TOKEN>

# User token (your personal account — may violate Discord ToS):
.\build\qdiscord.exe --token <USER_TOKEN> --user
```

Get a bot token at https://discord.com/developers/applications (Bot section). Invite the bot to a guild with `bot` and `messages.read` scopes.

No token = exits code 1 with a stderr message (no window opens).

## Gateway intents

IDENTIFY sends `intents` (default `513` = GUILDS | GUILD_MESSAGES). Override with `--intents <N>`:

- Bot token, to receive message `content` in `MESSAGE_CREATE`: enable "Message Content Intent" in the Discord developer portal and run with `--intents 33281` (513 + 32768 MESSAGE_CONTENT). Without enabling it in the portal, Discord closes with 4014.
- User token: no privileged intent needed; keep the default `513`.

## Deferred scope (not built — add later)

These are real work, not speculative. Add in this order:

1. **Multi-guild/channel switching** — currently auto-picks the first guild/channel. Store guild+channel id lists and re-run the REST chain on selection.
2. **Reconnect / resume** — on `op 7` (reconnect) or WS close, re-identify with `resume` + last seq.
3. **Rate-limit handling** — Discord returns `429` with `retry_after`; queue and back off.
4. **Rich embeds / attachments / embed previews** — render `embeds[]`, `attachments[]` in QML message items.
5. **Reactions, edits, deletes** — `MESSAGE_UPDATE`, `MESSAGE_DELETE` dispatches + REST endpoints.
6. **Voice** — separate voice gateway (`/api/v9/channels/{id}/voice-servers` → WS), UDP + opus. Heavy; defer until text is solid.
7. **Video / screen share** — even heavier; last.

## ToS boundary

Connecting to Discord's gateway with a **user token** via an unofficial client may violate Discord's Terms of Service. This client uses a **bot token**. Only test with a bot you own.

## REA artifacts (reference only)

- `discord.app.asar` — original Electron bundle (do not ship/commit Discord code)
- `extracted/` — `app.asar` contents used to recover the protocol
- `bootstrap/` — Discord's bootstrap folder
