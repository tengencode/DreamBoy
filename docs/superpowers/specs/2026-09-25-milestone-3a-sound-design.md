# Milestone 3a — Sound (APU, DMA sound, PC audio) — Design

**Date:** 2026-09-25
**Status:** Implemented — milestone 3a complete 2026-09-25 (tag `milestone-3a`)
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§4.2 APU, §4.3 DMA, §4.4 timers, §9 testing, §10 milestones)
**Builds on:** milestone 2 (tag `milestone-2`): PPU, DMA 0–3 (immediate/VBlank/HBlank), timers 0–3, test runner with frame fingerprints, `gbaview`.

---

## 1. Goal and scope

Produce the GBA's sound: the four PSG channels, the two DMA sound channels fed by timers and DMA1/DMA2, the hardware mixer, and a 32,768 Hz stereo output stream — then hear it on the PC and lock it in with tests.

**Milestone 3 is split into three parts (user decision, 2026-09-25):** **3a sound** (this document), **3b saves + cartridge extras** (SRAM/Flash/EEPROM, save-type detection, game database, RTC, solar, gyro, rumble, tilt), **3c timing accuracy** (ROM prefetch buffer, open-bus and write-only register reads, mGBA timing/DMA suites). Each part gets its own spec, plan and checkpoint.

| In scope (3a) | Out of scope |
|---|---|
| PSG channels 1–4, frame sequencer (length, sweep, envelope) | Band-limited or oversampled synthesis |
| Wave RAM with two banks, 64-sample mode, 75% force | SOUNDBIAS resolution modes other than 32,768 Hz (the register is stored; output rate stays fixed) |
| DMA sound A/B: FIFOs, timer 0/1 link, DMA1/DMA2 special timing | Correct read-back of write-only sound registers (3c) |
| Mixer (SOUNDCNT_L/H/X, SOUNDBIAS), 10-bit clamp, int16 output | Prefetch / DMA cycle accuracy (3c) |
| `gba_audio_read()` ring buffer | Android audio (milestone 4) |
| Test runner: `--wav`, `--audio-hash`, `--expect-audio-hash`; audio fingerprints in CTest | Resampling (the PC plays 32,768 Hz directly) |
| `gbaview` audio via waveOut, audio-paced; `--mute` | Gamepad input on PC |
| Own sound test ROMs built from assembly; Tonc `snd1_demo` | Third-party games/demos with music |

---

## 2. Files

```
core/include/gba.h            + size_t gba_audio_read(gba_t *g, int16_t *out, size_t max_frames);
core/src/apu/apu.h / apu.c    registers, FIFOs, mixer, sample event, output ring buffer
core/src/apu/psg.h / psg.c    square 1/2, wave, noise, frame sequencer
core/src/sys/sched.h          + EV_APU_SAMPLE, EV_APU_FRAME
core/src/sys/timer.c          timer 0/1 overflow → apu_on_timer_overflow()
core/src/sys/dma.c            special timing for DMA1/DMA2 (sound FIFO refill)
core/src/mem/io.c             + dispatch for 0x060–0x0A7
core/src/gba_internal.h       + apu_t apu
tools/common/wav.{h,c}        WAV writer (16-bit stereo PCM)
tools/common/frame_hash.h     + audio_hash_update() (FNV-1a over int16 LE bytes)
tools/testrunner/main.c       + --wav/--audio-hash/--expect-audio-hash
tools/viewer/main.c           + waveOut output, audio pacing, --mute
tools/wav_probe.py            pitch (DFT peak), RMS level, waveform PNG
tools/build_test_roms.py      assembles tests/own_roms/src/*.S → tests/own_roms/*.gba
tests/own_roms/src/snd_psg.S, snd_dma.S, snd_mix.S   own sound test ROM sources
tests/own_roms/*.gba          generated, committed (.gitignore exception)
tools/fetch_test_roms.py      + Tonc snd1_demo
tests/expected_frames.json    + optional "audio" fingerprint per entry
```

---

## 3. Sampling and output

- **Hardware-rate sampling (approved approach).** `EV_APU_SAMPLE` fires every **512 cycles** (16,777,216 / 512 = 32,768 Hz). Each firing: advance every PSG channel by the cycles since the last sample, compute the mixed left/right value, push one stereo frame into the output ring.
- **Catch-up before writes:** any write to 0x060–0x0A7 first advances the PSG channels to `g->now`, so a note triggered between two samples starts at the right cycle.
- **Frame sequencer:** `EV_APU_FRAME` fires every **32,768 cycles** (512 Hz), stepping 0..7.
- Both events always run while SOUNDCNT_X bit 7 (master enable) is set; when it is clear the output is silence (bias only → int16 0) and the frame sequencer is idle.
- **Output ring buffer:** 2048 stereo frames (≈3.7 video frames) of interleaved int16 L,R inside `apu_t` (no allocation during frames). When full, new samples are dropped. `gba_audio_read(g, out, max_frames)` copies up to `max_frames` stereo frames and returns how many.

## 4. PSG channels

Register offsets from 0x04000000. Channel outputs are unsigned 0..15.

- **Square 1** — SOUND1CNT_L 0x060 (sweep: shift 0–2, decrease 3, time 4–6), SOUND1CNT_H 0x062 (length 0–5, duty 6–7, envelope step 8–10, envelope up 11, initial volume 12–15), SOUND1CNT_X 0x064 (frequency 0–10, length enable 14, trigger 15).
- **Square 2** — SOUND2CNT_L 0x068 (as SOUND1CNT_H), SOUND2CNT_H 0x06C (as SOUND1CNT_X).
- **Duty patterns** (8 steps): 12.5% `00000001`, 25% `10000001`, 50% `10000111`, 75% `01111110`. Each step lasts **16 × (2048 − freq)** cycles. Output = envelope volume when the step is 1, else 0.
- **Sweep** (square 1, steps 2 and 6 of the sequencer, when time ≠ 0): new = f ± (f >> shift); new > 2047 disables the channel; otherwise f = new when shift ≠ 0. Trigger reloads the sweep timer and runs the overflow check once when shift ≠ 0.
- **Wave** — SOUND3CNT_L 0x070 (64-sample mode 5, bank 6, DAC on 7), SOUND3CNT_H 0x072 (length 0–7, volume 13–14: 0%/100%/50%/25%, force 75% bit 15), SOUND3CNT_X 0x074 (frequency 0–10, length enable 14, trigger 15). Wave RAM 0x090–0x09F (16 bytes = 32 samples, high nibble first) — **CPU accesses go to the bank that is not selected for playback**. Playback starts at the selected bank; in 64-sample mode it plays both banks. Each sample lasts **8 × (2048 − freq)** cycles. Volume: 100% = s, 50% = s >> 1, 25% = s >> 2, 0% = 0; force 75% = (s × 3) >> 2.
- **Noise** — SOUND4CNT_L 0x078 (length 0–5, envelope 8–15), SOUND4CNT_H 0x07C (ratio r 0–2, 7-bit width 3, shift s 4–7, length enable 14, trigger 15). LFSR 15-bit (7-bit when width set), reset to all ones on trigger; each clock: bit = b0 ^ b1, shift right, bit goes in at bit 14 (and bit 6 in 7-bit mode). Clock period **64 × r × 2^s** cycles, or **32 × 2^s** when r = 0. Output = envelope volume when bit 0 of the LFSR is 0, else 0.
- **Frame sequencer steps:** 0, 2, 4, 6 clock length counters; 2 and 6 also clock the sweep; 7 clocks envelopes. Length counters: 64 − n for squares/noise, 256 − n for wave; reaching 0 with length enable set turns the channel off. Envelopes: step time 0 = frozen; otherwise every `step` clocks volume moves ±1 within 0..15.
- **Trigger:** channel on (if its DAC is on), length reloaded if 0, envelope volume/timer reloaded, position/LFSR reset, sweep reloaded (square 1).
- **DAC off:** squares/noise with initial volume 0 and envelope decreasing, or wave with bit 7 of SOUND3CNT_L clear, are off.
- **SOUNDCNT_X 0x084:** bit 7 master enable; bits 0–3 read as the channels' on/off state. Clearing bit 7 zeroes 0x060–0x081 and ignores writes there until it is set again.

## 5. DMA sound

- **SOUNDCNT_H 0x082:** PSG volume 0–1 (0 = 25%, 1 = 50%, 2 = 100%, 3 treated as 100%), A volume 2 (0 = 50%, 1 = 100%), B volume 3, A right 8, A left 9, A timer 10, A FIFO reset 11, B right 12, B left 13, B timer 14, B FIFO reset 15. Reset bits empty the FIFO and read back as 0.
- **FIFOs:** FIFO_A 0x0A0, FIFO_B 0x0A4; 32-byte queues fed by 8/16/32-bit writes (bytes in little-endian order). Writes to a full FIFO are dropped.
- **Timer link:** on each overflow of timer 0 or 1, every DMA channel using that timer pops one signed byte into its current output sample (held until the next pop; an empty FIFO keeps the last sample). If 16 bytes or fewer remain afterwards, it requests a refill.
- **Refill = DMA special timing:** a refill request from FIFO A starts whichever of DMA1/DMA2 is enabled with special timing and destination 0x040000A0 (FIFO B: 0x040000A4). A requested refill transfers **4 words**, 32-bit, destination fixed, regardless of CNT_L and the size bit; source control applies as usual. The channel stays enabled (repeat is expected), the IRQ fires if enabled. DMA0 and DMA3 with special timing still never fire.

## 6. Mixer

Per side (left/right):

1. `psg = (ch1 + ch2 + ch3)` for the channels enabled on that side in SOUNDCNT_L (0x080: right volume 0–2, left volume 4–6, right enables 8–11, left enables 12–15), then `psg <<= 3`, then `psg += ch4 << 3` if ch4 is enabled on that side.
2. `psg *= 1 + side_volume` (0..7 → ×1..×8), then `psg >>= 4 − psg_ratio` (ratio 0/1/2 → shift 4/3/2).
3. `dma = (fifo_sample << 2) >> (volume_bit ? 0 : 1)` for A and B when enabled on that side; `sum = psg + dma_a + dma_b`.
4. `sum += bias` (SOUNDBIAS 0x088 bits 1–9, reset value 0x200), clamp to 0..0x3FF.
5. Output `int16 = (sum − bias) × 64`.

## 7. PC tools

- **WAV writer** (`tools/common/wav.c`): 16-bit stereo PCM at 32,768 Hz; header patched with the data size on close.
- **Audio fingerprint:** 64-bit FNV-1a over every drained int16 sample (little-endian bytes, L then R), printed as 16 hex digits.
- **Test runner:** drains the core's audio every frame. `--wav file.wav` writes it; `--audio-hash` prints `AUDIO <hex>`; `--expect-audio-hash <hex>` compares (exit 1 on mismatch). `tests/expected_frames.json` entries may carry `"audio": "<hex>"`; CTest passes both expectations when present.
- **gbaview:** opens waveOut (PCM, 2 channels, 32,768 Hz, 16-bit) with a ring of 8 buffers of up to 1024 stereo frames. After each emulated frame it drains the core's samples into the next free buffer and queues it; when 3 or more buffers are in flight it waits (1 ms sleeps) for one to finish — **the emulator is paced by audio**. If waveOut cannot open, or with `--mute`, it uses the existing 59.7275 fps timer pacing.
- **`tools/wav_probe.py`:** prints duration, RMS level per channel and the dominant frequency (DFT peak over a chosen window), and writes a waveform PNG (reusing the PNG code pattern from `png_probe.py`), so Claude can check pitch and shape without listening.
- **Own test ROMs:** `tools/build_test_roms.py` assembles each `tests/own_roms/src/*.S` with the NDK's clang/ld.lld/llvm-objcopy (same approach as `tools/gen_bios_stub.py`), fills the header's title, game code and complement checksum (the Nintendo logo area stays blank — the ROMs are for HLE mode), and writes `tests/own_roms/*.gba`. The generated files are committed (`.gitignore` gets `!tests/own_roms/*.gba`).
  - `snd_psg` — plays, one second each: square 1 at 440 Hz (freq 1750), square 2 at 880 Hz (freq 1899), wave channel with a sine table at 440 Hz (32 samples: freq 1899), noise; then a square 1 sweep.
  - `snd_dma` — timer 0 at 16,384 Hz (reload 0xFC00) plays a 16,384-byte buffer (a 32-byte triangle repeated, so a 512 Hz tone; the DMA source only moves forward, so one second needs one second of data) through DMA1 → FIFO A, then the same through timer 1 + DMA2 → FIFO B.
  - `snd_mix` — the same tones through left only, right only, 25/50/100% PSG ratio, DMA 50/100%.
- **Fetch script:** adds Tonc `snd1_demo` to the extracted demos (the same approved, SHA-256-pinned zip).

## 8. Testing

1. **Unit tests (exact samples):** each duty pattern and period; length expiry; envelope up/down/frozen; sweep up/down/overflow disable; wave playback, bank access rule, 64-sample mode, all volume codes; noise LFSR sequence (15- and 7-bit) and period; frame sequencer step order; SOUNDCNT_X status and master-off register clearing; FIFO order, full-drop, empty-hold, reset bits; timer 0/1 selection; refill request at ≤16 bytes and 4-word special DMA; mixer arithmetic (volumes, ratios, enables, bias clamp, int16 conversion); ring buffer read/drop.
2. **Pitch checks:** `wav_probe.py` measures each segment of `snd_psg`/`snd_dma`/`snd_mix`; expected pitches must match within 1%.
3. **Approved audio fingerprints:** after the pitch checks, Claude's look at the waveform PNGs, and the user's listening check in `gbaview`, the fingerprints of the 3 own ROMs and `snd1_demo` go into `tests/expected_frames.json`.
4. All milestone 1–2 tests keep passing; ASan + UBSan on for every host run.

## 9. Done when

- All unit tests pass (milestones 1–2 included).
- The 3 own ROMs and `snd1_demo` match their approved audio (and frame) fingerprints and expected pitches.
- The user confirms in `gbaview` that `snd1_demo` and `snd_dma` sound right, with no crackles.
- Sanitizers report nothing.
