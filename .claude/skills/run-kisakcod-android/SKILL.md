---
name: run-kisakcod-android
description: Build, install, launch, and drive the KisakCOD Android port (COD4 decompilation ported to arm64). Use when asked to run the Android app, build the APK, screenshot its menus, tap through it, or check device logs/crashes.
---

Native Android port of the decompiled COD4 engine (KisakCOD), JNI bootstrap
under `android/`, driven on a physical USB-attached device via `adb` — drive
it with `.claude/skills/run-kisakcod-android/driver.sh`. There is no
emulator/headless path: the renderer needs real GLES3 hardware and the app
expects a device already carrying the retail COD4 asset files (see Gotchas).

All paths below are relative to the repo root (`/home/jacques/Projects/KisakCOD`,
where `gradlew` lives — NOT `android/`, even though the Gradle module is
`:android:app`).

## Prerequisites

Android SDK + NDK installed and on `PATH` (this box has them at
`~/Android/Sdk`, no `ANDROID_HOME` export needed — `gradlew` finds it via
`local.properties`/default location). A device with USB debugging enabled,
attached and authorized:

```bash
adb devices -l
# a38b2d7c   device usb:1-2 product:sapphiren_eea model:23124RA7EO ...
```

If `adb` shows no daemon, the first `adb` call auto-starts one — no setup
needed.

## Build

```bash
./gradlew :android:app:assembleDebug
```

Produces `android/app/build/outputs/apk/debug/app-debug.apk` for all 4 ABIs
(arm64-v8a, armeabi-v7a, x86, x86_64). ~4s incremental, ~9s from clean
native + Java/dex/package. `./gradlew ":android:app:buildCMakeDebug[arm64-v8a]"`
compiles just the native lib (faster, use it for a pure compile-check when
you don't need an installable APK).

## Run (agent path)

Use the driver — it wraps the adb dance so you don't have to re-derive the
package name / activity / paths each time:

```bash
.claude/skills/run-kisakcod-android/driver.sh build
.claude/skills/run-kisakcod-android/driver.sh install a38b2d7c
.claude/skills/run-kisakcod-android/driver.sh launch a38b2d7c
.claude/skills/run-kisakcod-android/driver.sh tap 460 391 a38b2d7c   # e.g. "Mission Select" on the main menu
.claude/skills/run-kisakcod-android/driver.sh screenshot /tmp/out.png a38b2d7c
.claude/skills/run-kisakcod-android/driver.sh logs a38b2d7c
.claude/skills/run-kisakcod-android/driver.sh crashcheck a38b2d7c   # exits 1 if a crash/FATAL was logged
```

The `a38b2d7c` serial is optional — every subcommand omits it and falls
back to the sole attached device; pass one explicitly only when more than
one device/emulator is attached (adb itself will otherwise error clearly).

Screenshots are 2400x1080 landscape (device-native), letterboxed to the
game's 640x480 virtual canvas internally — `tap` coordinates are raw
on-screen pixels, not canvas coordinates.

| driver.sh subcommand | what it does |
|---|---|
| `build` | `./gradlew :android:app:assembleDebug` |
| `install [serial]` | `adb install -r` the built debug APK |
| `launch [serial]` | clears logcat, `am start`s `MainActivity`, waits 3s |
| `tap X Y [serial]` | `adb shell input tap X Y`, waits 2s for the scene rebuild |
| `screenshot OUT [serial]` | `adb exec-out screencap -p > OUT` |
| `logs [serial]` | dumps the app's own `KisakCODAndroid` log tag |
| `crashcheck [serial]` | checks the crash buffer + `*:F`/`AndroidRuntime:E`; non-zero exit on a hit |
| `data-status [serial]` | lists what's under the app's `files/cod4/` on-device |
| `run [serial]` | build + install + launch + logs + crashcheck, one shot |

For a full smoke pass: `driver.sh run <serial>`.

## Run (human path)

`am start -n com.kisakcod.android/.MainActivity` after installing puts the
app in front of you like any Android launcher tap would — same command the
driver uses, just without the log capture around it.

## Test

No unit/instrumented test suite exists for the Android module yet — device
smoke testing (this skill) is the only verification path. `find android -iname "*Test*"`
turns up nothing.

## Gotchas

- **The app expects retail COD4 assets already on the device** — this repo
  and this skill don't (and shouldn't) fetch or bundle them. Before `launch`
  will show more than the bootstrap/error screen, the device needs
  `/sdcard/Android/data/com.kisakcod.android/files/cod4/` populated with
  the `iw_*.iwd`/`localized_*.iwd` files, a `zone/` directory, and — this
  is the non-obvious part — **`iw3sp.exe`/`iw3mp.exe` present as bare
  marker files** (the native FS bootstrap checks for their existence as
  its "is this a real COD4 install" signal; without them it falls back to
  a checkerboard placeholder even though the actual asset files are all
  there). Check with `driver.sh data-status`.
- **No emulator path.** The renderer needs a real Adreno/Mali GLES 3.2
  driver; this was never tried under `emulator -gpu` and likely isn't
  worth chasing — physical device over USB is the established workflow.
- **`/tmp` is not durable across sessions.** COD4 data staged there for
  device pushes (e.g. an extracted retail ISO) can vanish on reboot; the
  device itself is the durable copy once pushed.
- **Screenshot resolution is the device's real display size** (2400x1080
  here), not the game's 640x480 virtual menu canvas — don't assume `tap`
  coordinates map 1:1 to in-game UI element positions without checking
  the actual screenshot first.

## Troubleshooting

- **`driver.sh install` fails with "APK not found"**: run `driver.sh build`
  first — `install` doesn't build for you.
- **Renderer log shows repeated `IWI read failed: ... runtime FS non
  initialise`**: the device's `files/cod4/` is missing data or the
  `iw3sp.exe`/`iw3mp.exe` marker files (see Gotchas) — `driver.sh
  data-status` to check what's actually there.
