# Milestone 4d — Rumble and motion/light sensors on Android — Design

**Date:** 2026-09-26
**Status:** Implemented — milestone 4d complete 2026-09-27 (tag `milestone-4d`). Approved design (brainstormed with the user 2026-09-26); prototyped and tried by the user on the emulator and their phone (2026-09-27); plan: `docs/superpowers/plans/2026-09-27-milestone-4d-rumble-sensors.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§4 cartridge extras, §5.4 input & sensors, §10 milestones)
**Builds on:** 3b-2 (tag `milestone-3b2`: the core's game list and extras — `gba_cart_hardware`, `gba_set_tilt`, `gba_set_gyro`, `gba_set_light`, `gba_rumble_active`; `docs/superpowers/specs/2026-09-25-milestone-3b2-cart-extras-design.md`), 4a–4c (tags `milestone-4a`, `-4b`, `-4c`: the app, sound, touch buttons, save files, `KeyMap`, `MainActivity`, the key latch).

---

## 1. Goal and scope

**Finish line:** on the user's **real phone** (phone A, Android 16, arm64 — installed with the user's OK), game D reacts to **turning the phone** and the user **feels its rumble**; on the Pixel 9 emulator the same game works with the Q/E keys. Tilt (game F, game G) and light (game E) are proven by tests and by Claude's check with the emulator's virtual sensors. All PC, Kotlin and on-device tests pass.

**User decisions (2026-09-26):**
- The user owns **game D** (gyro + rumble) — the game for their check; tilt and light are checked by tests and by Claude.
- **Light:** the phone's light sensor **plus a manual slider** (a sun button on the game screen, only for solar cartridges; "Auto" = the sensor).
- **Motion keys too:** I/J/K/L tilt and Q/E turn (as in `gbaview`), besides moving the phone.
- **Real phone at the end:** a deliberate install on the user's phone for the final check (the app's first run on a real device), only when the user says so.
- **Sensors are read in Kotlin** (option 1 of 3): Kotlin already owns keys and touch; motion, keys and the slider are merged there with pure, PC-tested maths; three numbers go to C like the buttons; Kotlin polls the rumble motor once per display frame and drives the vibrator. (Rejected: sensors in C with `ASensorManager` as the parent spec sketched — keys and slider would still come from Kotlin, so merging would be split, and rumble needs Kotlin anyway (no NDK vibrator API); a mix of both — the drawbacks of each.)

| In scope (4d) | Later |
|---|---|
| Tilt ← accelerometer, turn ← gyroscope, light ← light sensor | Sensitivity settings, "recentre" (milestone 6) |
| I/J/K/L tilt, Q/E turn keys | Gamepad sticks for motion |
| Sun button + light slider (solar carts), Auto = sensor | Game Boy Player rumble |
| Rumble → the phone's vibrator | Gamepad vibration |
| Sensors only while such a game runs, off in the background | |
| `dev.sh phone-install` / `phone-uninstall` for the user's phone, only on request | Any automatic use of the real phone |

## 2. Files

```
app/src/main/java/com/tengen/gbaemu/
  MotionMath.kt       pure maths: tilt from gravity (per screen rotation), turn from the rotation rate, light from
                      lux, key overrides; also the rumble on/off decision
  MotionInput.kt      Android sensors (accelerometer, gyroscope, light) + I/J/K/L, Q/E + the slider → setMotion/setLight
  Rumble.kt           a Choreographer frame callback: rumbleActive() changed → Vibrator on/off
  Screens.kt          + the sun button and light slider (solar cartridges only)
  MainActivity.kt     + motion keys, MotionInput and Rumble started/stopped with the game and the app's pause/resume
  NativeBridge.kt     + setMotion, setLight, cartHardware, rumbleActive; test hook debugMotionSeen
app/src/main/AndroidManifest.xml   + <uses-permission android:name="android.permission.VIBRATE"/>
platform/android/emu_thread.{h,c}   + motion/light atomics applied at frame start; rumble state after each frame
platform/android/jni_bridge.c       + the functions above
tests/own_roms/src/hw_gyro.S        game code RZWT (gyro + rumble by the game list, like game D):
                                    the rumble motor on/off every 30 frames
tests/own_roms/src/hw_tilt.S        game code KYGT (tilt, like game F): an idle loop
tests/own_roms/src/hw_solar.S       game code U3IT (RTC + solar, like game E): an idle loop
app/src/test/.../MotionMathTest.kt, app/src/androidTest/.../MotionTest.kt
tools/dev.sh                        + phone-install, phone-uninstall
```

The core is not changed (its extras are tested since 3b-2).

## 3. How the values are made (`MotionMath.kt`)

- **Tilt:** Android's accelerometer gives gravity in m/s² on the device's natural axes (x right, y up the screen, z out of it, portrait). Remapped to the **current screen** (rotation 0/90/180/270: x,y swapped/negated), then `tilt_x = clamp(-ax / 9.81 × 32767)`, `tilt_y = clamp(ay / 9.81 × 32767)` with signs chosen so that lowering the screen's right edge gives the same as `gbaview`'s L key (+x) and lowering its top edge the same as the I key (−y). Lying flat = level (0, 0); 1 g along an edge = full tilt — like the cartridge's sensor, which measures gravity the same way.
- **Turn:** gyroscope z (rad/s, counter-clockwise positive, unchanged by screen rotation) → `turn = clamp(-wz / (4π) × 32767)` — clockwise positive as the core expects; full scale = two turns per second.
- **Light:** lux → `level = clamp(255 × log10(lux / 10) / 4)` — 0 at ≤ 10 lux (a dark room), 255 at ≥ 100,000 lux (direct sun). The slider (0–255) replaces the sensor until set back to **Auto**.
- **Keys:** while I/J/K/L or Q/E is held, that axis takes the key value instead of the sensor: tilt ±24000 (L = +x, J = −x, K = +y, I = −y), turn ±16000 (E = +, Q = −) — exactly `gbaview`'s values.
- **Rumble decision:** a tiny state machine `next(active) → START | STOP | NOTHING` (only changes act).

## 4. Data flow

- **Kotlin → C:** `setMotion(tiltX, tiltY, turn)` and `setLight(level)` store atomics in the emulation thread's `emu_t`; at the start of every frame (next to the keys) the thread calls `gba_set_tilt`, `gba_set_gyro`, `gba_set_light` with them. `cartHardware()` returns `gba_cart_hardware` (read once after `loadRom`). After each frame the thread stores `gba_rumble_active` in an atomic that `rumbleActive()` reads.
- **Sensors:** `MotionInput` registers only the sensors the cartridge needs (TILT → accelerometer, GYRO → gyroscope, SOLAR → light) with `SENSOR_DELAY_GAME`, in `onResume` while playing; unregisters in `onPause` and when the game stops. Each event (and each motion key change) recomputes and calls `setMotion`/`setLight`. A missing sensor → that input stays 0 (level / still / dark) and a one-time toast says to use the keys or the slider.
- **Rumble:** `Rumble` posts a `Choreographer` frame callback while playing a rumble cartridge; on a change it starts a long one-shot vibration (`VibratorManager` default vibrator on API 31+, `Vibrator` below; 5 s, re-issued while still on) or cancels it. Pause, quit and the app going to the background cancel it.
- **Debug log:** in debug builds `MotionInput` logs `motion: tilt x y turn t light l` (tag `gbaemu`) at most once a second when the values changed — for Claude's check with the emulator's virtual sensors.
- **Sun button:** on the game screen, only when `cartHardware()` has SOLAR: a small ☀ button; tapping it shows a slider with an **Auto** switch.

## 5. The real phone

- `dev.sh phone-install`: finds the attached devices that are **not** emulators; exactly one → prints its model and serial and runs `adb -s <serial> install -r <app-debug.apk>`; none or several → an error, nothing installed. `dev.sh phone-uninstall`: `adb -s <serial> uninstall com.tengen.gbaemu`. Both run only when the user asks. All other commands keep targeting the emulator only (`ANDROID_SERIAL`); tests never run on the phone.
- The user copies their game D ROM to the phone themselves (or asks Claude, who then pushes only that file with the same one-device rule).

## 6. Testing

1. **JVM tests** (`bash tools/dev.sh jvmtest`), `MotionMathTest`: gravity along each screen edge → the right signed full tilt, in rotations 0 and 90; flat → (0, 0); beyond 1 g → clamped; a clockwise turn → positive, 4π rad/s → 32767; 10 lux → 0, 100,000 lux → 255, 1,000 lux → about 128; a held key overrides only its axis; the rumble decision acts only on changes.
2. **On-device tests** (emulator only), `MotionTest`: values from `setMotion`/`setLight` reach the core within a frame (hook `debugMotionSeen()` = the last values applied); `hw_gyro.gba`: `cartHardware()` = GYRO | RUMBLE and `rumbleActive()` is seen both on and off within 1.5 s; `hw_tilt.gba`: TILT; `hw_solar.gba`: RTC | SOLAR; `m7_demo`: 0. All earlier tests keep passing.
3. **Claude's check** (adb on the emulator): the three test cartridges in the app — `hw_tilt.gba` + `adb emu sensor set acceleration …` and I/J/K/L → the `motion:` log line follows; `hw_gyro.gba` + `adb emu sensor set gyroscope …` and Q/E → the turn follows, and `dumpsys vibrator_manager` shows the vibrations; `hw_solar.gba` + `adb emu sensor set light …` → the light follows, the ☀ button and slider appear, the slider value (and Auto) is logged.
4. **User check:** game D on the emulator with Q/E; then, with their OK, `dev.sh phone-install` and the same game on their phone: turning the phone turns the game, the rumble is felt.

## 7. Done when

- All tests in §6 pass; CTest 56/56 on the PC.
- The user's check (§6.4) passes on the emulator and on their phone.
