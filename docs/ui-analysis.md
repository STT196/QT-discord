# Discord UI / dark-theme design-token analysis

Source: Discord renderer JS bundle extracted from the Chromium disk cache.
- Bundle file: `C:\Users\thisa\AppData\Roaming\discord\Cache\Cache_Data\f_00337f`
- Bundle size: 12,410,287 bytes (12.4 MB)
- SHA-256: `667a771c61ff709e2f0400d395a324f0a5b1b5aa89e11e263289144a12d084ae`
- Read as Latin1; near-identical bundles also present as `f_003423`, `f_0034c9`, `f_00355a`.

All values below are extracted from the installed client build — they are **not guesses**. Full machine-readable output: `docs/discord-theme-dark.json`.

## Extraction counts

| Item | Count |
|------|-------|
| Palette entries (`NAME:"#rrggbb"`, deduped) | 1,729 |
| Semantic token blocks found | 642 |
| Resolved for DARK theme | 642 |
| Resolved for ONYX theme | 642 |
| Unresolved | 0 |

Every semantic token references a palette entry that exists; every opacity parsed as a finite number in `[0,1]`. Nothing was invented. `docs/discord-theme-dark.json` contains `palette`, `themes.dark`, `themes.onyx` (CSS var kebab-name → `#hex` or `rgba(...)`), and `unresolved` (empty).

Token-name → CSS-var rule: lower-case, `_` → `-`, prefix `--` (e.g. `BACKGROUND_BASE_LOWER` → `--background-base-lower`). Opacity < 1 is emitted as `rgba(r,g,b,a)` over the resolved palette color; opacity == 1 is emitted as solid `#hex`.

## Dark theme — key values (CSS var → value)

App frame / guild rail
- `--app-frame-background` = `#070709`
- `--panel-bg` = `#1a1a1e` (guild-rail / side-panel base)
- `--app-frame-border` = `rgba(151,151,159,0.121569)`

Channel sidebar
- `--background-secondary-alt` = `#383a43` (Plum_15)
- `--background-base-low` = `#202024`

Chat background / message area
- `--chat-background` = `#31323b`
- `--chat-background-default` = `#222327`
- `--chat-border` = `rgba(151,151,159,0.121569)`

Surface layers
- `--background-base-lower` = `#1a1a1e`
- `--background-base-lowest` = `#121214`
- `--background-surface-high` = `#242429`
- `--bg-surface-raised` = `#26272f`
- `--card-background-default` = `#242429`
- `--card-border-default` = `rgba(151,151,159,0.121569)`

Text (default / strong / muted / subtle / link)
- `--text-strong` = `#fbfbfb`
- `--text-default` = `#efeff1`
- `--text-muted` = `#96979e`
- `--text-subtle` = `#abacb2`
- `--text-link` = `#4d96ee`
- `--text-brand` = `#798df9`

Interactive (normal / hover / active / muted)
- `--interactive-background-default` = `rgba(151,151,159,0.078431)`
- `--interactive-background-hover` = `rgba(151,151,159,0.121569)`
- `--interactive-background-active` = `rgba(151,151,159,0.160784)`
- `--interactive-background-selected` = `rgba(151,151,159,0.2)`
- `--interactive-text-default` = `#abacb2`
- `--interactive-text-hover` = `#fbfbfb`
- `--interactive-text-active` = `#fbfbfb`
- `--interactive-muted` = `#4d505b`

Borders
- `--border-subtle` = `rgba(151,151,159,0.121569)`
- `--border-normal` = `rgba(151,151,159,0.2)`
- `--border-strong` = `rgba(151,151,159,0.439216)`
- `--border-muted` = `rgba(151,151,159,0.039216)`

Brand / blurple
- `--background-brand` = `#5865f2` (BLURPLE_50)
- `--control-primary-background-default` = `#5865f2`

Status (online / idle / dnd / offline / streaming / typing)
- `--text-status-online` = `#3d9e60` (`--icon-status-online` = `#3d9e60`)
- `--text-status-idle` = `#ffcb6e` (`--icon-status-idle` = `#ffcb6e`)
- `--text-status-dnd` = `#dc4247` (`--icon-status-dnd` = `#dc4247`)
- `--text-status-offline` = `#9d9ea5` (`--icon-status-offline` = `#9d9ea5`)
- Streaming / typing use brand blurple (`#5865f2`) and `#4d96ee` link respectively; no separate streaming token found.

Message hover / mentions
- `--reaction-background-hover` = `rgba(151,151,159,0.160784)` (message hover)
- `--keyword-highlight-background` = `rgba(151,151,159,0.2)` (mention / keyword highlight)
- `--control-secondary-background-hover` = `rgba(151,151,159,0.160784)`

Full token set (642 dark + 642 onyx) and the complete 1,729-entry palette are in `docs/discord-theme-dark.json`.

## Notes for Qt/QML replication

Map these tokens to a QML `Theme` singleton (approx. 25 most important for a Discord-like desktop layout):

1. `theme.appFrameBackground` ← `--app-frame-background` `#070709` (window chrome)
2. `theme.guildRailBackground` ← `--panel-bg` `#1a1a1e`
3. `theme.guildRailBorder` ← `--app-frame-border` `rgba(151,151,159,0.121569)`
4. `theme.channelSidebarBackground` ← `--background-secondary-alt` `#383a43`
5. `theme.channelSidebarItemBackground` ← `--background-base-low` `#202024`
6. `theme.chatBackground` ← `--chat-background` `#31323b`
7. `theme.chatBackgroundDefault` ← `--chat-background-default` `#222327`
8. `theme.chatBorder` ← `--chat-border` `rgba(151,151,159,0.121569)`
9. `theme.surfaceBase` ← `--background-base-lower` `#1a1a1e`
10. `theme.surfaceLowest` ← `--background-base-lowest` `#121214`
11. `theme.surfaceHigh` ← `--background-surface-high` `#242429`
12. `theme.surfaceRaised` ← `--bg-surface-raised` `#26272f`
13. `theme.cardBackground` ← `--card-background-default` `#242429`
14. `theme.cardBorder` ← `--card-border-default` `rgba(151,151,159,0.121569)`
15. `theme.textStrong` ← `--text-strong` `#fbfbfb`
16. `theme.textDefault` ← `--text-default` `#efeff1`
17. `theme.textMuted` ← `--text-muted` `#96979e`
18. `theme.textSubtle` ← `--text-subtle` `#abacb2`
19. `theme.textLink` ← `--text-link` `#4d96ee`
20. `theme.brand` ← `--background-brand` `#5865f2` (primary buttons, blurple)
21. `theme.interactiveBgDefault/Hover/Active/Selected` ← the four `--interactive-background-*` values
22. `theme.borderSubtle/Normal/Strong` ← `--border-subtle/normal/strong`
23. `theme.statusOnline` ← `#3d9e60`, `theme.statusIdle` ← `#ffcb6e`, `theme.statusDnd` ← `#dc4247`, `theme.statusOffline` ← `#9d9ea5`
24. `theme.messageHover` ← `--reaction-background-hover` `rgba(151,151,159,0.160784)`
25. `theme.mentionHighlight` ← `--keyword-highlight-background` `rgba(151,151,159,0.2)`
26. `theme.unreadPill` ← `--background-brand` `#5865f2` (no dedicated unread token found)
27. `theme.inputBackground` ← `--control-secondary-background-default` `rgba(151,151,159,0.121569)` (no dedicated input-bg token found; secondary is the closest surface)
28. `theme.divider` ← `--border-muted` `rgba(151,151,159,0.039216)`

Alpha values: Discord composes `rgba` over the layer below; in QML use `Qt.rgba(r/255,g/255,b/255,a)` with the same alpha. Do not pre-compose over `#000` — the stack order matters.

## Typography / radius / spacing (best-effort, evidence snippets)

Extracted from the same bundle; mark uncertain where the value is behind a dynamic `resolve(e)` that depends on `enabledExperiments`/`density` and could not be evaluated statically.

Reliably literal:
- `FONT_SIZE_DEFAULT: 16`, `FONT_SIZE_MIN: 12`, `FONT_SIZE_MAX: 24`, `FONT_SIZES: [12,14,15,16,18,20,24]`
- `MARKUP_LINE_HEIGHT: {resolve:()=>"1.375rem"}`
- `CARD_DEFAULT_RADIUS: {resolve:()=>16}`
- `CHANNEL_ITEM_RADIUS: {resolve(e){...if(0===t.length)return 12;...}}` → default 12 (uncertain under experiments)
- `CHAT_INPUT_BORDER_RADIUS: {resolve(e){...default 24;...mobile-visual-refresh → 8}}` → default 24 (uncertain under experiments)
- `GUILD_ITEM_SELECTED_BORDER_RADIUS` → default 16 (uncertain)
- `CHANNEL_LIST_SUBTITLE_TEXT_STYLE` → default `"text-xs/medium"` (uncertain)
- `INPUT_FIELD_RADIUS_SM/MD/LG`, `SEGMENTED_CONTROL_BORDER_RADIUS`, `VOICE_TILE_BORDER_RADIUS`, `GUILD_FOLDER_BACKGROUND_RADIUS`, etc. — all `{resolve(e){let{enabledExperiments...` → dynamic, not statically resolvable.

Snippet evidence:
```
CARD_DEFAULT_RADIUS:{resolve:()=>16},CHANNEL_DETAILS_NAV_BUTTONS_GAP:{resolve(e){let{enabledExperiments:t}=e;if(0===t.length)return 12;...
MARKUP_LINE_HEIGHT:{resolve:()=>"1.375rem"}
FONT_SIZE_DEFAULT:16,FONT_SIZE_MIN:12,FONT_SIZE_MAX:24,FONT_SIZES:[12,14,15,16,18,20,24]
CHAT_INPUT_BORDER_RADIUS:{resolve(e){let{enabledExperiments:t}=e;if(0===t.length)return 24;for(let e of t)if("mobile-visual-refresh"===e)return 8;return 24}}
```

Caveats:
- `resolve(e)` tokens keyed on `enabledExperiments` (e.g. `mobile-visual-refresh`) and `density` return experiment-dependent values; the defaults shown are the `0===t.length` branch only. These are marked uncertain and should be verified against a running client.
- ONYX is Discord's "darker/black" theme; DARK values are reported here. Both are fully resolved in the JSON (0 unresolved).
- Palette includes 1,729 entries (ANSI, chart, gradient, badge/chip colors included); the design core is `NEUTRAL_*`, `BLURPLE_50`, `PLUM_15`, `PRIMARY_600`, `GREEN_NEW_49`, `BLACK_500`, `WHITE_500`.
- No dedicated unread-pill, input-background, or streaming token was found; closest surrogates are noted above.
