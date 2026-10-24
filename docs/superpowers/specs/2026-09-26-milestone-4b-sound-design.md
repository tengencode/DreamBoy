# Milestone 4b — Sound (AAudio output, rate conversion, sound-paced emulation) — Design

**Date:** 2026-09-26
**Status:** Implemented — milestone 4b complete 2026-09-26 (tag `milestone-4b`). Approved design (brainstormed with the user 2026-09-26); prototyped (§8); plan: `docs/superpowers/plans/2026-09-26-milestone-4b-sound.md`; key-latch fix added during the listening check (§9)
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§1 decisions — audio API changed here, §5.1 threads, §5.3 audio, §8 errors, §10 milestones)
**Builds on:** milestone 4a (tag `milestone-4a`): the Android app, emulation thread, frame pacer, render thread (`docs/superpowers/specs/2026-09-26-milestone-4a-app-picture-design.md`); the core's sound output from milestone 3a (`gba_audio_read`, stereo int16 at 32,768 Hz).

---

## 1. Goal and scope

**Finish line:** on the Pixel 9 phone emulator, games play **with sound** through the PC's speakers — clear, right pitch, no crackle, in step with the picture — and the game's speed is set by the sound.

**User decisions (2026-09-26):**
- **AAudio** (Android's current native audio API, Android 8+) instead of the parent spec's OpenSL ES (deprecated by Google). Safe settings only: shared mode, no exclusive/MMAP. minSdk stays 26. (Rejected: OpenSL ES — deprecated, longer code; the Oboe library — a download and C++ in a C project.)
- **Approach A — the sound sets the pace** (as the parent spec and `gbaview`): the emulation thread runs frames while the sound buffer is under its target and waits when it is fuller; a tiny rate adjustment (at most ±0.5 %) keeps the buffer near the target; 4a's 59.7275 Hz clock timer is used only when there is no sound. (Rejected: the clock sets the pace and the sound bends to follow — drift can become audible; pacing by the display — wrong speed on 60/90/120 Hz screens.)

| In scope (4b) | Later |
|---|---|
| AAudio output stream (open, pause/resume, close, reopen after a device change) | Fast-forward (sound off while fast), rewind (milestone 5) |
| Cubic (Hermite) rate conversion 32,768 Hz → the phone's rate, stateful across frames | Volume / mute / latency settings (milestone 6) |
| Lock-free sound buffer between the emulation thread and AAudio's callback | NEON rate conversion (milestone 7) |
| Rate adjustment ±0.5 % by buffer fill; sound-paced emulation; watchdog; clock fallback | Touch overlay, save files, full lifecycle (4c) |
| Test hooks and on-device tests; PC tests for the plain-C parts | |

## 2. Files

```
platform/android/
  resampler.{h,c}       plain C: stereo int16 cubic Hermite rate conversion, state kept between calls
  audio_ring.{h,c}      plain C: lock-free single-producer/single-consumer ring of stereo int16 frames
  rate_control.{h,c}    plain C: output/input ratio from the buffer fill
  audio_out.{h,c}       AAudio: open/close/pause/resume, data + error callbacks, statistics
  emu_thread.c          + sound: convert, push, sound pacing, watchdog, reopen, clock fallback
  jni_bridge.c          + test hooks (audio statistics, audio on/off, simulated disconnect)
app/src/main/java/com/tengen/gbaemu/NativeBridge.kt   + the test hooks' declarations
app/src/androidTest/.../AudioTest.kt                  on-device sound tests
tests/unit/test_audio.c                               PC tests of rate_control and audio_ring
tests/unit/test_resampler.c                           PC tests of the resampler
tests/unit/CMakeLists.txt                             + the three plain-C files
```

`platform/android/CMakeLists.txt` already compiles every `platform/android/*.c`; it gains `aaudio` in the link libraries. `resampler`, `audio_ring` and `rate_control` use no Android API and are also built into the PC `unit_tests`.

## 3. Data flow and pacing

```
Emulation thread                                     AAudio callback thread (Android, high priority)
 frame → gba_audio_read (32,768 Hz stereo)            asks for N frames every burst
 → resampler (ratio = rate/32768 × (1 ± ≤0.5 %))  →   audio_ring (lock-free)  →  copy out; short → silence
 → audio_ring push                                    (counts played / silent-filled / loud frames)
 → wait while the ring holds more than the target
```

- **Emulation thread, each frame (sound on):** keys → `gba_run_frame` → picture into the triple buffer (4a) → every sample from `gba_audio_read` goes through the resampler into `audio_ring` → **pacing:** while the ring holds more than `target` frames, sleep 1 ms and look again (the callback never has to wake anyone and never takes a lock).
- **Sizes (at the stream's rate `R`, e.g. 48,000):** ring capacity 8,192 stereo frames (fixed; ≥ 170 ms at 48 kHz); `target = R × 30 / 1000` frames (30 ms). One GBA frame adds `R × 280,896 / 16,777,216` ≈ 804 frames (16.7 ms at 48 kHz). AAudio's own buffer: set to **two bursts** with `AAudioStream_setBufferSizeInFrames` (Google's advice; the phone emulator's default was 4,360 frames = 91 ms, two bursts = 1,920 frames = 40 ms, with no dropouts in the tests). Expected total delay game → speaker ≈ 70 ms on the emulator (40 ms + 30 ms), less on phones with the low-latency path.
- **Rate adjustment (`rate_control`):** `fill = queued / (2 × target)`, clamped to [0, 1]; `ratio = (R / 32768) × (1 + 0.005 × (0.5 − fill) / 0.5)` output frames per input frame — emptier → slightly more output, fuller → slightly less, never beyond ±0.5 %.
- **Resampler:** 4-point cubic Hermite (Catmull-Rom) per channel on int16 input, output rounded and clamped to int16. State: the last three input frames and the fractional read position (step = 1 / ratio input frames per output frame), so converting in chunks gives exactly the same samples as converting everything at once. The ratio may change between calls without a jump.
- **Audio ring (`audio_ring`):** power-of-two capacity, `write` and `read` counters as atomics (producer: emulation thread; consumer: AAudio callback); `push` returns how many frames fitted (the rest is dropped — never overwritten; cannot normally happen because the thread waits); `pop` returns how many were available; `queued` for pacing; `clear` only while the stream is paused/stopped.
- **AAudio callback:** copies `min(asked, queued)` frames, fills the rest with zeros (an **underrun**), updates statistics (frames played, frames silence-filled, frames with any non-zero sample), returns `CONTINUE`. No locks, no allocation, no logging inside.
- **Watchdog:** if, while sound is on and not paused, the ring has not shrunk for 250 ms, the stream is considered stuck: close and reopen once; if it is still stuck (or will not open), switch to the clock timer for the rest of the session. **Not in the first 2 s after the stream starts or resumes:** a waking sound device takes a while (the prototype saw 350 ms on the emulator's cold start, which the first version mistook for a stuck stream).
- **Pause:** the emulation thread pauses the stream, empties the ring and resets the rate state; **resume:** starts the stream and paces afresh. **Stop:** the stream is closed before the core is freed.
- **Sound off** (no device, open failed, watchdog gave up, or the test hook): the sound is read and dropped and 4a's frame pacer runs the game at 59.7275 Hz.

All AAudio calls except the callbacks are made on the emulation thread (it opens the stream when it starts and handles pause/resume/reopen at frame boundaries).

## 4. AAudio stream

| Setting | Value |
|---|---|
| Direction / sharing | output, `AAUDIO_SHARING_MODE_SHARED` |
| Format / channels | `AAUDIO_FORMAT_PCM_I16`, 2 |
| Sample rate | unspecified (Android picks the native rate); read back with `AAudioStream_getSampleRate` |
| Performance mode | `AAUDIO_PERFORMANCE_MODE_LOW_LATENCY` |
| Usage / content type | `AAUDIO_USAGE_GAME` / `AAUDIO_CONTENT_TYPE_MUSIC` (API 28+ only; skipped below) |
| Callbacks | data callback (§3) + error callback that only sets a "disconnected" flag |

| Case | Behaviour |
|---|---|
| Stream will not open | `logcat` (`gbaemu`), sound off, clock pacing — the game plays silently |
| Device disconnected (headphones/Bluetooth in or out, output switched) | error callback sets the flag; at the next frame the emulation thread closes and reopens the stream (Android forbids reopening inside the callback); reopen fails → sound off |
| Ring runs dry | silence for that moment, counted as underrun |
| Stuck stream | watchdog (§3) |
| App in the background | stream paused, ring emptied; resumed on return |

## 5. Test hooks (JNI, test-only)

- `debugAudioStats(): LongArray` = `[active (1/0), sampleRate, framesPerBurst, framesPlayed, framesSilent, framesLoud, reopens]` (counters since `loadRom`).
- `debugSetAudioEnabled(enabled: Boolean)` — applies to the next `start()` (false → clock pacing, no stream).
- `debugSimulateAudioDisconnect()` — sets the disconnected flag as the error callback would.
- `debugSimulateStuckStream()` — pauses the AAudio stream behind the emulation thread's back (the ring stops draining, nothing is reported), for the watchdog test.

## 6. Testing

1. **PC unit tests** (`bash tools/dev.sh unit`, test-first):
   - resampler: a 440 Hz sine at 32,768 Hz → 48,000 Hz is 440 ± 0.5 Hz and within 1 % of the input amplitude; a constant input gives exactly that constant; chunked conversion == one-shot conversion; the output count follows the ratio (±1 frame over a second); changing the ratio mid-stream gives no sample jump larger than the signal's own largest step;
   - audio_ring: fill level, wrap-around, full → the extra is dropped and nothing is overwritten, `clear`; two threads push/pop 1,000,000 frames with none lost, repeated or reordered;
   - rate_control: fill 0.5 → exactly `R / 32768`; fill 0 → +0.5 %; fill 1 → −0.5 %; beyond [0, 1] clamped.
2. **On-device tests** (`bash tools/dev.sh androidtest`, emulator only), new `AudioTest`:
   - the stream opens on the emulator: `active = 1`, `sampleRate` > 0, `framesPerBurst` > 0;
   - speed with sound: over 5 s the emulation runs elapsed × 59.7275 ± 3 frames, now sound-paced (4a's `runsAtGbaSpeed` keeps passing too);
   - sound really plays: `own_roms/snd_psg.gba` for 3 s → `framesPlayed` ≈ `sampleRate` × elapsed (± 5 %), `framesLoud` > 0, and no new silence-filled frames between 0.5 s and 3 s;
   - pause: `framesPlayed` does not grow while paused (after a 100 ms settle) and grows again after resume;
   - sound off: with `debugSetAudioEnabled(false)` the game still runs 59.7275 ± 3 frames/s and `active = 0`;
   - device change: `debugSimulateAudioDisconnect()` → `reopens` becomes 1, `active` stays 1 and `framesPlayed` keeps growing;
   - stuck stream: 2.5 s after start (past the grace), `debugSimulateStuckStream()` → within 0.8 s `reopens` becomes 1, `active` stays 1 and `framesPlayed` keeps growing.
   - All 4a on-device tests (fingerprints, picture, session) keep passing.
3. **PC side unchanged:** CTest 56/56 (the core is not modified).
4. **Claude's check** on the emulator: run the app with `snd_psg`, read the statistics hook and `logcat`.
5. **User check (listening)** on the phone emulator: `own_roms/snd_psg.gba` (the beeps, hiss and rising tone approved in 3a), Tonc `snd1_demo` (SOS twice), their game A hack (title music, menu blips on key presses feel immediate, a battle); leaving and returning plays no stale sound.

## 7. Done when

- All tests in §6 pass; CTest 56/56 on the PC.
- The user's listening check (§6.5) passes.

## 8. Prototype results (2026-09-26)

4b was prototyped in a throwaway worktree (branch `proto-4b`) before the plan; the plan's code is that prototype's code.
- PC: 13 new tests (rate control 3, sound buffer 5, resampler 5) — a 440 Hz tone stays 440 Hz at the same loudness, pieces == one go, no jump on a ratio change, a million frames through the buffer from two threads with none lost. 349 unit tests in total.
- Phone emulator: AAudio uses Android's legacy path there ("perf = NO"): 48,000 Hz, bursts of 960 frames; buffer set to 1,920 frames (40 ms). On-device 15/15 (the 8 from 4a + 7 new), repeated runs — sound-paced speed, sound playing with **zero silence-filled frames** after the first half-second, pause, clock fallback, reopen after a simulated device loss, watchdog after a simulated stuck stream.
- Found while prototyping: the watchdog's start-up grace (above); the resampler's start-up (its window starts as silence, so the first ~6 output frames mix zeros in); `M_PI` is not standard C (tests define their own).
- **The user listened to the prototype on the emulator (2026-09-26): `snd_psg`, `snd1_demo` and their game A hack — "Everything worked."**

## 9. Found in the listening check: short key presses (2026-09-26)

In the listening check the sound passed, but in game A only the arrows and Enter worked. A `getevent -lt` recording showed the phone emulator's window sends **letter keys as down+up pairs 0.1 ms apart, even while held** (Enter: one real press; a held arrow: repeated ~34 ms presses), and the emulation thread reads the held-keys mask only once per frame (16.7 ms) — so X (A) and Z (B) were almost never seen. **Decision (user, 2026-09-26): fix it in 4b** with a key latch (`platform/android/key_latch.{h,c}`, plan Task 5): every button pressed since the last frame counts as held for that frame, so no press is lost (also covers short touch taps in 4c). PC tests (5) + on-device `KeyTest` (2, with a `debugKeysSeen` hook: buttons the game was given since the last call); the tap test failed before the latch was wired in. Limit on the PC emulator only: holding a letter key acts like fast repeated taps (e.g. holding Z to run stutters); real phones, gamepads and the arrow keys are unaffected. Test totals after this: 354 PC unit tests, 17 on-device.
