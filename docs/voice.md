# Voice Gateway v8, UDP and DAVE

Connect: `wss://<endpoint>/?v=8`. Plain RFC 6455: random 16-byte `Sec-WebSocket-Key`, masked client frames, no extensions.

## Flow

Hello (8) → Identify (0) → Ready (2) → UDP discovery → Select Protocol (1) → Session Description (4) → Speaking (5).

## Text Opcodes

| Op | Dir | Name | Payload |
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
| 31 | C→S | DAVE Invalidate Transition | `transition_id` |

## Binary Opcodes (DAVE / MLS)

- S→C: `[seq u16 BE][op u8][payload]`
- C→S: `[op u8][payload]`

| Op | Dir | Name |
|---|---|---|
| 25 | S→C | External Sender |
| 26 | C→S | Key Package |
| 27 | S→C | Proposals |
| 28 | C→S | Commit / Welcome |
| 29 | S→C | Announce Commit Transition |
| 30 | S→C | Welcome |

## Heartbeat

`seq_ack` must be the last `seq` seen, from the binary header and from JSON `seq`. Send -1 if none yet.

## Encryption Modes (preference order)

1. `aead_xchacha20_poly1305_rtpsize`
2. `aead_xchacha20_poly1305`
3. `xsalsa20_poly1305`

## UDP IP Discovery (74 bytes, big-endian)

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Type: `0x0001` request, `0x0002` reply |
| 2 | 2 | Length `0x0046` |
| 4 | 4 | SSRC |
| 8 | 64 | Address (reply, null-terminated) |
| 72 | 2 | Port (reply) |

## Media Path

Opus frame → DAVE encrypt (libdave) → RTP → transport encrypt (libsodium) → UDP.
