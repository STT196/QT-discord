# qdiscord

qdiscord is an experimental Qt/C++ + QML desktop client that aims to recreate a Discord-like experience for text-based usage using Discord's REST and Gateway APIs. The project is built around a direct Qt implementation rather than Electron, with protocol details reverse-engineered from the installed Discord client and implemented in custom networking code.

This repo is a learning and engineering project, not an official Discord client. It focuses on the parts that are practical to implement in Qt while keeping the scope honest about the harder voice and media features.

## Project goals

- Build a Discord-inspired desktop client with Qt and QML
- Support guild/channel navigation and message history
- Authenticate with bot or user tokens
- Reconnect and resume Discord gateway sessions
- Handle rate limiting, heartbeat, and message cache sync
- Keep the implementation grounded in the project plan and practical Reverse Engineering / REA work

## Current status

The code and plan indicate that the project is in the early-to-mid implementation stage:

- Completed: gateway bootstrap, REST auth, message fetching, reconnect/resume logic, heartbeats, routing, and a usable text-focused UI shell
- In progress: replies, mentions, reactions, attachments, embeds, markdown rendering, typing indicators, and richer channel/guild features
- Not yet complete: voice/video/media features, advanced Discord UI parity, and many platform-level features

See [PLAN.md](PLAN.md) for the detailed roadmap and current phase status.

## Repository layout

- [CMakeLists.txt](CMakeLists.txt) — CMake build configuration
- [main.cpp](main.cpp) — application entry point
- [discordclient.cpp](discordclient.cpp) / [discordclient.h](discordclient.h) — Discord networking and client logic
- [voiceclient.cpp](voiceclient.cpp) / [voiceclient.h](voiceclient.h) — voice-related logic
- [qml/main.qml](qml/main.qml) — primary QML UI entry point
- [docs/ui-analysis.md](docs/ui-analysis.md) — UI notes and analysis
- [bootstrap/manifest.json](bootstrap/manifest.json) — local bootstrap metadata

## Requirements

- CMake 3.16 or newer
- C++17 compiler
- Qt 6 with the following modules:
  - Core
  - Network
  - Widgets
  - Quick
  - Multimedia
- A valid Discord token for testing against a real guild or bot account

## Build instructions

From the project root:

```bash
cmake -S . -B build
cmake --build build
```

On Windows with a Qt environment configured, you may need to use the Qt kit or set up the correct CMake generator and environment variables before running the above commands.

## Run

After a successful build, launch the app from the build output directory:

- Windows: `build\qdiscord.exe`
- Linux/macOS: `./build/qdiscord`

## Notes for use

- This is intended for development and experimentation.
- Discord's real client protocol is partially undocumented and changes often.
- Voice and video work is explicitly high risk and not guaranteed to be complete.
- Use test or personal tokens only; do not expose credentials in public repositories.

## Related docs

- [PLAN.md](PLAN.md) — phase plan, architecture notes, and project scope
- [docs/ui-analysis.md](docs/ui-analysis.md) — UI and interface analysis

## License

This project does not currently declare a license file. Check the repository state before redistributing or publishing forks.
