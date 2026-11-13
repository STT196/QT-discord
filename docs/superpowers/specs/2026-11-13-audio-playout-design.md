# Audio Playout Stability Design

## Problem

Voice transport and DAVE are healthy, but continuous music has audible jitter. The receive log can report about 50 played frames per second while also dropping many packets as late.

`VoiceAudio` already owns a dedicated `QThread`. The defect is in playout policy: playback starts after two frames (about 40 ms), then tries to keep six frames (about 120 ms) queued in `QAudioSink`. When the receive queue empties during that fill, the current code generates concealment frames and advances the expected RTP sequence. Real packets then arrive behind that artificial position and are discarded as late.

The timing diagnostic also survives stop/start, so a large first `play tick late` value can include disconnected time and must not be treated as an active playback stall.

## Design

Keep one audio worker as the sole owner of audio devices, Opus codecs, DAVE media cryptors, jitter queues, and playback state.

For each speaker:

1. Buffer six 20 ms frames before starting playout, matching the sink's six-frame target.
2. Decode the expected RTP frame when present.
3. Use Opus PLC only when a newer queued RTP frame proves that the expected frame is missing.
4. If the queue is empty, stop filling the sink without advancing the expected sequence.
5. If both the receive queue and sink buffer drain, mark the stream unstarted so it must prebuffer again. A temporarily empty receive queue while the sink still holds audio does not trigger rebuffering.
6. Keep the existing large-sequence-jump resynchronization and latency cap.

Reset playback timing diagnostics on every start and stop. Count every PLC frame and any short sink write so diagnostics represent actual output behavior.

## Threading

Do not add another thread. RTP packets and control calls already cross into the existing audio worker with queued invocations. A second media thread would add synchronization around mutable DAVE decryptors and Opus decoders without addressing premature sequence advancement.

Blocking MLS control requests are outside the initial jitter fix. Replace them with asynchronous request/result signals only if post-fix measurements show MLS transitions disrupting active playback; startup transition latency alone is not evidence of steady-state jitter.

## Verification

Extend the existing voice self-test with deterministic jitter-queue checks:

- An empty queue does not advance the expected sequence or emit PLC.
- A proven one-packet gap emits exactly one PLC frame and advances once.
- Playout does not start below the six-frame prebuffer target.
- The playout state transition used by stop/start resets timing diagnostics.

Then build, run `--voice-selftest` and `--voice-audio-selftest`, launch the client, and observe continuous music. Success means steady-state `late=0` (apart from genuine reordered packets), `plc` only on actual gaps, no recurring `play tick late` warnings, and no audible bottleneck-like jitter.
