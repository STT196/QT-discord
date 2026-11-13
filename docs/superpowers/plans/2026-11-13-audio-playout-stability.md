# Audio Playout Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent the receive jitter buffer from advancing past packets that have not arrived merely to fill the hardware output buffer.

**Architecture:** Keep the existing `VoiceAudio` worker thread and `QAudioSink`. Centralize the per-speaker playout decision in one small state transition used by both the live mixer and deterministic self-tests; prebuffer six frames, conceal only proven sequence gaps, and wait without advancing when no future packet exists.

**Tech Stack:** C++17, Qt 6 Multimedia, Opus, existing `--voice-selftest` executable path

**Spec:** `docs/superpowers/specs/2026-11-13-audio-playout-design.md`

## Global Constraints

- Keep one audio worker; do not add threads or dependencies.
- Preserve RTP, transport encryption, DAVE, Opus, and signal interfaces.
- PLC is valid only when a newer queued sequence proves a gap.
- Do not commit unless the user explicitly requests it.

---

### Task 1: Make Playout Decisions Deterministic

**Files:**
- Modify: `voiceaudio.h:109-129`
- Modify: `voiceaudio.cpp:20-59,247-252,584-693,1013-1031`

**Interfaces:**
- Consumes: `VoiceAudio::Jitter { q, next, hi, have, started }`
- Produces: private `enum class PlayoutAction { Wait, Decode, Conceal, Resync }` and `static PlayoutAction nextPlayoutAction(Jitter &j)`

- [ ] **Step 1: Add failing queue-policy checks to `VoiceAudio::selfTest()`**

Add checks that construct private `Jitter` values directly:

```cpp
auto fail = [&ok](const char *message) {
    qWarning("[voice-selftest] %s", message);
    ok = false;
};
Jitter j;
j.have = true;
j.q.insert(100, QByteArray("a"));
j.q.insert(101, QByteArray("b"));
if (nextPlayoutAction(j) != PlayoutAction::Wait || j.started || j.next != 0)
    fail("started below prebuffer target");

j.q.clear();
for (qint64 seq = 100; seq < 106; ++seq)
    j.q.insert(seq, QByteArray("x"));
if (nextPlayoutAction(j) != PlayoutAction::Decode || !j.started || j.next != 100)
    fail("did not start at prebuffer target");

j.q.clear();
const qint64 before = j.next;
if (nextPlayoutAction(j) != PlayoutAction::Wait || j.next != before)
    fail("empty queue advanced sequence");

j.q.insert(before + 1, QByteArray("future"));
if (nextPlayoutAction(j) != PlayoutAction::Conceal || j.next != before)
    fail("proven gap did not request PLC");
```

Use the existing warning-and-`ok = false` style rather than introducing a test framework.

- [ ] **Step 2: Run the self-test and verify RED**

Run:

```powershell
$env:Path="C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.12.0\mingw_64\bin;$env:Path"
cmake --build build
.\build\qdiscord.exe --voice-selftest
```

Expected: compilation fails because `PlayoutAction` and `nextPlayoutAction` do not exist.

- [ ] **Step 3: Implement the minimal playout state transition**

Add `kPrebufferFrames = 6` beside the audio constants. Add the private enum/helper to `VoiceAudio`:

```cpp
VoiceAudio::PlayoutAction VoiceAudio::nextPlayoutAction(Jitter &j) {
    if (!j.started) {
        if (j.q.size() < kPrebufferFrames)
            return PlayoutAction::Wait;
        j.started = true;
        j.next = j.q.firstKey();
    }
    if (j.q.contains(j.next))
        return PlayoutAction::Decode;
    if (j.q.isEmpty())
        return PlayoutAction::Wait;
    if (j.q.firstKey() - j.next > 50) {
        j.next = j.q.firstKey();
        return PlayoutAction::Resync;
    }
    return PlayoutAction::Conceal;
}
```

Replace the inline `started`/missing/empty branches in `onPlayTick()` with this action. Increment `j.next` only after `Decode` or `Conceal`; increment `m_stats.plc` only for `Conceal`; `Wait` must not decode, increment, or synthesize a frame; `Resync` continues the mixer round and decodes on the next pass.

When no speaker produces a frame and `m_sink->bufferSize() - m_sink->bytesFree() < kFrameBytes`, set `started = false` only for jitter queues that are empty. This requires a six-frame prebuffer after a real sink underrun, but not while the sink still carries audio.

- [ ] **Step 4: Reset timing diagnostics at lifecycle boundaries**

At both `start()` and `stop()`, call:

```cpp
m_tickGap.invalidate();
m_statClock.invalidate();
m_stats = Stats();
```

This prevents disconnected time from appearing as worker starvation.

- [ ] **Step 5: Verify GREEN**

Run:

```powershell
$env:Path="C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.12.0\mingw_64\bin;$env:Path"
cmake --build build
.\build\qdiscord.exe --voice-selftest
.\build\qdiscord.exe --voice-audio-selftest
```

Expected: build succeeds; both commands exit `0` and print `ok`.

- [ ] **Step 6: Verify live continuous audio**

Launch with stderr redirected, join the music-bot channel, and observe at least 30 seconds:

```powershell
Start-Process .\build\qdiscord.exe -RedirectStandardError "$env:TEMP\opencode\qdiscord-playout.log"
Get-Content "$env:TEMP\opencode\qdiscord-playout.log" -Wait
```

Expected: `rx` and `played` remain near 50 per second, steady-state `late=0`, `plc` increases only for genuine packet gaps, no recurring `play tick late` warnings, and music is continuous.

- [ ] **Step 7: Review the final diff without committing**

Run `git diff --check` and `git diff -- voiceaudio.h voiceaudio.cpp`. Confirm only the queue policy, self-checks, and timing reset changed.
