# API Docs Index

| File | Covers |
|---|---|
| [gateway.md](gateway.md) | Main gateway: opcodes, lifecycle, Identify, events |
| [rest.md](rest.md) | REST endpoints, auth, rate limits |
| [dm-calls.md](dm-calls.md) | DM / group DM call flow across gateway, REST, voice |
| [voice.md](voice.md) | Voice gateway v8, UDP, DAVE/MLS, encryption modes |
| [qt-internal.md](qt-internal.md) | `VoiceClient` and `VoiceAudio` C++ APIs |

## Sources and caveats

- `voice.md` and `qt-internal.md` are derived from the uploaded source.
- `gateway.md`, `rest.md` and `dm-calls.md` are from general knowledge of Discord's undocumented user-side API. Verify against live traffic before relying on them.
- `discordclient.cpp`, `main.cpp` and the QML files were not provided, so the app's own classes are not documented.
- User-account automation violates Discord's ToS. Never log or commit tokens, `secret_key` or session IDs.
