# Milestone 4a — Android App + Picture (project, native library, emulation thread, OpenGL screen, ROM picker, keys) — Design

**Date:** 2026-09-26
**Status:** Implemented — milestone 4a complete 2026-09-26 (tag `milestone-4a`); prototyped (§9); plan: `docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§1 decisions, §2 layout and layering rules, §5 Android platform layer, §7 app shell, §8 errors, §10 milestones)
**Builds on:** milestone 3 (tag `milestone-3c2`): the finished core behind `core/include/gba.h`.

---

## 1. Goal and scope

Milestone 4 (Android platform layer; parent exit criterion "games playable on the x86_64 emulator") is split into three parts (user decision 2026-09-26): **4a app + picture** → **4b sound** (OpenSL ES, resampler, audio-paced timing) → **4c controls + lifecycle** (touch overlay, save files, full pause/resume) → playable.

**4a finish line:** on the Pixel 9 phone emulator (AVD `Pixel_9_API_36_1`, x86_64), the user opens a GBA ROM with Android's file picker; it shows full-screen at the right speed with correct colours and can be played with the PC keyboard.

**User decisions (2026-09-26):**
- ROMs are opened with **Android's file picker** (Storage Access Framework), not a fixed folder.
- **PC keyboard / gamepad keys work in 4a** (the touch overlay comes in 4c).
- **Approach A:** mostly C, as the parent spec says — a C emulation thread and a C OpenGL render thread driven by the display's refresh; Kotlin is a thin shell. (Rejected: Android's `GLSurfaceView` — less code but departs from the design; one thread for everything — wrong speed on 90/120 Hz screens and fights 4b's audio pacing.)
- Names: app **"GBA Emulator"**, package **`com.tengen.gbaemu`**.

| In scope (4a) | Later |
|---|---|
| Gradle Android project `app/` (Kotlin, Jetpack Compose start screen, `SurfaceView` game screen) | Sound output (4b) — until then the core's sound is read and dropped every frame |
| `platform/android/` C code + CMake, building `core/` unchanged into `libgbaemu.so` for x86_64 and arm64-v8a (generic C) | Touch overlay, save files, pause/resume that flushes saves (4c) |
| Emulation thread paced by a clock at 59.7275 Hz; triple buffer; render thread (EGL + GLES 3.0 + `AChoreographer`) | Filters (sharp-bilinear, LCD grid, colour correction), skins, settings, library (milestone 6) |
| File picker, keyboard/gamepad keys, clock set from the phone's time | Sensors, rumble (milestone 6); save states, rewind, fast-forward (milestone 5) |
| Rotation keeps running; leaving the app pauses, returning resumes | ARM64 assembly (milestone 7) |
| `tools/dev.sh` Android commands; PC and on-device tests | |

## 2. Project layout and build

```
app/                                   Gradle module (com.tengen.gbaemu)
  build.gradle.kts                     externalNativeBuild → platform/android/CMakeLists.txt; ABIs x86_64, arm64-v8a
  src/main/java/com/tengen/gbaemu/     MainActivity.kt, Screens.kt (StartScreen, GameScreen), NativeBridge.kt, KeyMap.kt
  src/test/                            JVM unit tests (KeyMapTest)
  src/androidTest/                     on-device tests (NativeLibraryTest, FingerprintTest, PictureTest, SessionTest, TestRoms helper)
settings.gradle.kts, build.gradle.kts, gradle.properties, gradlew(.bat), gradle/wrapper/  Gradle 8.14.3 wrapper
platform/android/
  CMakeLists.txt                       add_subdirectory(core) + libgbaemu.so from every platform/android/*.c
  jni_bridge.c                         JNI entry points (NativeBridge)
  emu_thread.{h,c}                     emulation thread, frame pacing
  frame_pacer.{h,c}                    pure C: next deadline, reset when behind / on pause
  triple_buffer.{h,c}                  pure C: lock-free 3-slot frame exchange
  render_thread.{h,c}                  EGL window surface, AChoreographer loop
  egl_ctx.{h,c}                        EGL display + RGBA8888 config + ES 3 context (window or pbuffer)
  video_gl.{h,c}                       texture upload, shader, draw
  gl_readback.{h,c}                    test helper: draw one frame offscreen and read the pixels back
  fit_rect.{h,c}                       pure C: aspect-fit viewport
```

- **Toolchain:** Android Gradle Plugin 8.13.0, Kotlin 2.2.21 (+ Compose compiler plugin), Gradle 8.14.3, JDK = Android Studio's bundled JBR 21, NDK 27.2.12479018, SDK CMake 3.31.6, compileSdk/targetSdk 36, **minSdk 26**; Compose UI/Foundation 1.11.4, Material3 1.4.0, Activity 1.12.3, Core 1.17.0; tests: JUnit 4.13.2, androidx.test runner 1.7.0, ext-junit 1.3.0. The plugin ids are mapped straight to their artifacts in `settings.gradle.kts` (the Gradle cache has no plugin markers). The cache lacked 15 small files (~750 KB, e.g. `javapoet` 1.10.0); **the user approved downloading them once from Google's Maven / Maven Central (2026-09-26)**; `dev.sh` runs Gradle with `--offline`, so any further download fails loudly and is asked about first.
- The pure-C pieces (`triple_buffer`, `frame_pacer`, `fit_rect`) also build on the PC and are unit-tested by `bash tools/dev.sh unit` (they use no Android API).
- `tools/dev.sh` gains: `apk` (assemble debug), `jvmtest` (Kotlin unit tests), `emu` (start the AVD if not running and wait for boot), `install`, `launch`, `androidtest` (on-device tests), `push <file>` (copy into the emulator's Download folder). Gradle runs with `JAVA_HOME` = Android Studio's JBR; `adb` runs with `MSYS_NO_PATHCONV=1` (Git Bash otherwise turns `/sdcard/...` into a Windows path).
- **The native code is optimised in the debug APK too** (`CMAKE_C_FLAGS_DEBUG = -O2 -g`): unoptimised, the core ran at only ~49 fps on the phone emulator.
- `AChoreographer_postFrameCallback64` (Android 10+) is called behind `__builtin_available`; the library is built with `__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__` and `-Werror=unguarded-availability` (minSdk 26).
- The core is not changed; layering rules (parent §2.1) hold: `core/` knows nothing about Android; `platform/android/` owns threads and OS APIs; JNI carries control only.

## 3. Threads and data flow

```
Kotlin (screens, picker, keys)
   │  JNI: loadRom, setSurface, start/stop, pause/resume, setKeys
   ▼
Emulation thread (C) ──frame──► triple buffer ──newest frame──► Render thread (C, OpenGL)
   ▲ reads key mask (atomic)                                       │ draws on every display refresh
```

- **Emulation thread** (pthread; the only code that touches `gba_t` once started): each iteration reads the atomic key mask → `gba_set_keys` → `gba_run_frame` → copies `gba_framebuffer()` into the triple buffer's back slot and publishes it → drains `gba_audio_read` and drops the samples (4b replaces this) → sleeps with `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` until the next deadline. Frame period **280,896 / 16,777,216 s = 16.7427 ms (59.7275 Hz)**, kept in nanoseconds with the remainder carried so it does not drift.
- **Frame pacer** (pure C): `next = previous + period`; if the thread is more than 3 periods late, `next = now + period` (no catch-up burst); `pause` → on resume `next = now + period`.
- **Triple buffer** (pure C): three 240×160 `uint16_t` slots; indices `back` (producer), `front` (consumer) and a shared `middle` word holding a slot index plus a "fresh" bit, swapped with atomic exchange. Producer: write back, `back = xchg(middle, back | FRESH)`. Consumer: if `middle` has FRESH, `front = xchg(middle, front) & ~FRESH` → newest complete frame; a torn frame is impossible; old frames are skipped, never queued.
- **Render thread** (pthread with an `ALooper`): owns the EGL display/context; `AChoreographer` frame callbacks (the 64-bit variant on API 29+, the older one below) wake it once per display refresh; each callback takes the newest frame if there is one, uploads it, draws, `eglSwapBuffers`, and re-posts the callback. No new frame → the previous texture is drawn again (correct on 90/120 Hz panels).
- **Window changes:** Kotlin's `SurfaceHolder.Callback` calls `setSurface(surface)` on create/change and `setSurface(null)` on destroy. The render thread receives the new `ANativeWindow` through a message on its looper; on `null` it destroys the `EGLSurface` (keeps the context) and **`setSurface(null)` returns only after the render thread has let go of the window** (Android requires that before `surfaceDestroyed` returns). A new window → new `EGLSurface`; `EGL_CONTEXT_LOST` → rebuild context, texture and shader.
- **Keys:** Kotlin keeps the 10-bit GBA mask and calls `setKeys(mask)` on every change → one atomic store, read by the emulation thread at the start of each frame.
- **Clock:** at `loadRom`, `gba_set_rtc(time(NULL), local UTC offset)` from the phone's clock (game A's real-time clock).
- **Control (JNI, `NativeBridge.kt`):** `loadRom(ByteBuffer): Int` (stops any running game, creates the core, loads the ROM — the core copies it — returns a `gba_err_t`), `setSurface(Surface?): Boolean` (false if OpenGL ES 3 cannot draw into the window; a window that arrives before `start()` is kept and handed over then), `start()` (render thread + emulation thread), `stop()` (render thread first, then the emulation thread; frees the core), `pause()` / `resume()` (emulation thread waits on a condition variable; pacer reset on resume), `setKeys(Int)`. Test-only: `debugFrameCount(): Long`, `debugRunFrames(rom, frames): String?` (headless run on the calling thread → `"<frame hash> <audio hash>"`, exactly like `testrunner --frames n`) and `debugDrawFrame(pixels, width, height): IntArray?` (draws one frame into an offscreen pbuffer with the real `video_gl` code; RGBA pixels, top row first).

## 4. Picture

- **OpenGL ES 3.0**, EGL config RGBA8888, one `GL_R16UI` 240×160 texture updated with `glTexSubImage2D`; one quad; the fragment shader reads the texel with `texelFetch` (nearest) and unpacks BGR555: each 5-bit channel `c` → 8-bit `(c << 3) | (c >> 2)`, alpha 255. No colour correction (milestone 6 filter).
- **Scaling (`fit_rect`):** the largest 3:2 rectangle that fits the surface, centred, rounded to whole pixels; the rest is cleared to black.
- Only nearest-neighbour in 4a.

## 5. Kotlin app

- **`MainActivity`** (single activity; `android:configChanges` includes orientation and screen size so rotation does not restart it — the surface just changes size).
- **Start screen** (Compose, dark Material 3 colour scheme on a `Surface`): "Open ROM" button → `ACTION_OPEN_DOCUMENT` (any file type; the file picker shows everything, the core checks the size) → bytes read through `ContentResolver` into a direct `ByteBuffer` → `loadRom`. Error → message under the button (e.g. "Not a GBA ROM (size 0 or over 32 MiB)"), stay on the start screen.
- **Game screen:** a full-screen `SurfaceView` (hosted with `AndroidView`), system bars hidden (immersive), screen kept on. **Back** → `stop()` and return to the start screen.
- **Lifecycle (4a level):** `onPause` → `pause()`; `onResume` → `resume()`; `onDestroy` → `stop()`. (Flushing save RAM and an auto-save state on pause come in 4c/5.)
- **Keys (`KeyMap.kt`):** `onKeyDown/onKeyUp` →

| Android key | GBA |
|---|---|
| D-pad / arrow keys | D-pad |
| `X`, `BUTTON_A` | A |
| `Z`, `BUTTON_B` | B |
| `A`, `BUTTON_L1` | L |
| `S`, `BUTTON_R1` | R |
| `ENTER`, `BUTTON_START` | Start |
| `DEL` (Backspace), `BUTTON_SELECT` | Select |

## 6. Errors

| Case | Behaviour |
|---|---|
| ROM rejected by `gba_load_rom` | Message on the start screen; nothing started |
| EGL/GLES 3.0 cannot start (`setSurface` returns false) | Game stopped, back to the start screen with "This device cannot show the game: OpenGL ES 3.0 failed." |
| Surface gone (background, rotation) | Render thread idles without a surface; emulation paused while the activity is paused |
| File cannot be read | Message on the start screen |
| Anything unexpected in native code | `logcat` with tag `gbaemu` |

## 7. Testing

1. **PC unit tests** (`bash tools/dev.sh unit`, test-first): triple buffer (newest frame wins, a frame is never torn, no fresh frame → same front slot), frame pacer (deadlines, 3-period reset, pause reset, no drift over 600 frames), `fit_rect` (3:2 fits, bars, odd sizes).
2. **Kotlin unit tests** (`dev.sh jvmtest`): `KeyMap` — every mapped key sets/clears the right bit; unmapped keys ignored.
3. **On-device tests** (`dev.sh androidtest`, on the Pixel 9 emulator). The test APK bundles the locally present test ROMs and `tests/expected_frames.json` as androidTest assets (taken from the gitignored `tests/roms/` and `tests/own_roms/` at build time — never committed, never in the app APK):
   - **Same core, same results:** all **32** approved entries of `tests/expected_frames.json` (26 frames, 4 with audio, 2 save-chip ROMs on a blank chip) run through `debugRunFrames` must give exactly the PC's frame (and audio) fingerprints.
   - **Picture read-back:** `debugDrawFrame` with a known pattern at 480×320 (exact 2× → each GBA pixel a 2×2 block of the expected 8-bit colour) and at 720×320 (120-pixel black bars left and right, the picture 2× in between). Only whole-number scales can be compared pixel by pixel; `fit_rect` is also unit-tested on the PC for other sizes.
   - **Window hand-over:** the render thread draws a running game (jsmolka `stripes`) into a real window (a 480×320 `ImageReader` surface): the middle row is not black, and `setSurface(null)` returns.
   - **Speed:** over 5 s the emulation thread runs elapsed-time × 59.7275 ± 3 frames (≈ 299).
   - **Robustness:** 20 rounds of load → start → pause (no frames while paused) → resume (frames again) → stop without a hang or crash; an empty ROM is rejected.
4. **PC side unchanged:** CTest 56/56 (the core is not modified).
5. **Claude's check** on the phone emulator before the user's: drive the real app with `adb` (push a Tonc demo, tap Open ROM, pick it in the file picker, hold a key, rotate, home and back) and inspect screenshots.
6. **User check** on the phone emulator: a Tonc demo and — only with the user's OK to copy it onto the emulator — their game A hack, opened through the file picker; picture right, speed right, keyboard works, rotation keeps running, leaving and returning pauses and resumes.

## 8. Done when

- All tests in §7 pass; CTest 56/56 on the PC.
- The user's check (§7.6) passes.

## 9. Prototype results (2026-09-26)

The whole of 4a was prototyped in a throwaway worktree (branch `proto-4a`) before the plan was written; the plan's code is that prototype's code.
- Gradle builds the app, the test APK and the Kotlin tests fully offline after the one approved download; the C library builds for x86_64 and arm64-v8a with no warnings.
- **The Android-built core reproduced all 32 approved fingerprints exactly** on the Pixel 9 emulator (Android 16, x86_64, OpenGL ES 3.1 through the PC's GPU, 60 Hz).
- Picture read-back pixel-exact at 480×320 and 720×320; the window test, speed, pause/resume (20 rounds) and empty-ROM tests pass, three runs in a row (7/7).
- Found while prototyping: the unoptimised debug library ran at ~49 fps (→ `-O2` in debug); `AChoreographer_postFrameCallback64` needs weak linking at minSdk 26; `adb push` to `/sdcard/...` needs `MSYS_NO_PATHCONV=1` in Git Bash; the first frame pacer dropped the first period's 0.3 ns (a PC test caught it: 1 ns short after 600 frames); the start screen's text was black on the dark window until a dark colour scheme and a `Surface` were added.
- By hand (adb): start screen → Open ROM → Android file picker → Downloads → `m7_demo.gba` → the game fills the width in portrait (bars above and below), landscape gives bars left and right, holding Up moves the Mode 7 camera, home and back resumes; Android shows its one-time "Viewing full screen" tip on first entry.

## 10. User check (2026-09-26)

- Tonc `obj_demo` and the user's game A hack (copied onto the emulator with their OK) opened through the file picker: picture, speed, rotation, home/back and Back all fine.
- **Keys did not reach the game at first — root cause outside the app:** the AVD had `hw.keyboard=no`, so the emulator passed no PC key to Android (a `getevent` recording showed the user's clicks but not one key event). With the user's OK the AVD was switched to `hw.keyboard=yes` (backup `~/.android/avd/Pixel_9_API_36_1.avd/config.ini.bak-before-keyboard`); after a restart the recording showed Enter/X/Z/arrows arriving and **the user confirmed the controls work in game A**. Task 8's adb check could not catch this: `adb shell input keyevent` injects keys inside Android.
- `dev.sh push` failed for absolute Git Bash paths (adb, a Windows program, got `/c/...` because path rewriting is off for adb); fixed with `cygpath -w`.
