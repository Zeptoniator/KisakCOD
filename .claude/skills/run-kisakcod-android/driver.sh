#!/usr/bin/env bash
# Driver for the KisakCOD Android port. Wraps build/install/launch/interact
# so an agent can drive the real app on a real device instead of paraphrasing
# the source. Run from the repo root (where gradlew lives) or pass -C.
#
# Usage:
#   driver.sh build                          # ./gradlew :android:app:assembleDebug
#   driver.sh install [serial]               # adb install -r the built APK
#   driver.sh launch  [serial]                # clear logcat + am start MainActivity
#   driver.sh tap X Y [serial]               # adb shell input tap (2400x1080 canvas)
#   driver.sh screenshot OUT.png [serial]    # adb exec-out screencap -p > OUT.png
#   driver.sh logs [serial]                  # tail the app's own log lines
#   driver.sh crashcheck [serial]            # non-zero exit if a crash/FATAL was logged
#   driver.sh data-status [serial]           # check whether COD4 assets are present
#   driver.sh run [serial]                   # build + install + launch + logs (smoke)
#
# serial defaults to the sole `adb devices` entry; pass one explicitly when
# more than one device/emulator is attached.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PKG="com.kisakcod.android"
ACTIVITY="$PKG/.MainActivity"
APK="$REPO_ROOT/android/app/build/outputs/apk/debug/app-debug.apk"
DATA_DIR="/sdcard/Android/data/$PKG/files/cod4"

serial_arg() {
    # Prints `-s <serial>` if a serial was passed as $1, else nothing (adb
    # picks the sole attached device on its own, and errors clearly if
    # there's more than one — that's the behavior we want here too).
    if [ -n "${1:-}" ]; then printf -- '-s %s' "$1"; fi
}

cmd_build() {
    cd "$REPO_ROOT"
    ./gradlew :android:app:assembleDebug
}

cmd_install() {
    local serial="${1:-}"
    [ -f "$APK" ] || { echo "APK not found at $APK — run 'driver.sh build' first" >&2; exit 1; }
    adb $(serial_arg "$serial") install -r "$APK"
}

cmd_launch() {
    local serial="${1:-}"
    adb $(serial_arg "$serial") logcat -c
    adb $(serial_arg "$serial") shell am start -n "$ACTIVITY"
    sleep 3
}

cmd_tap() {
    local x="$1" y="$2" serial="${3:-}"
    adb $(serial_arg "$serial") shell input tap "$x" "$y"
    sleep 2
}

cmd_screenshot() {
    local out="$1" serial="${2:-}"
    adb $(serial_arg "$serial") exec-out screencap -p > "$out"
    echo "Screenshot saved to $out"
}

cmd_logs() {
    local serial="${1:-}"
    adb $(serial_arg "$serial") logcat -d -s KisakCODAndroid:*
}

cmd_crashcheck() {
    local serial="${1:-}"
    local crash fatal
    crash="$(adb $(serial_arg "$serial") logcat -d -b crash 2>&1)"
    fatal="$(adb $(serial_arg "$serial") logcat -d -s '*:F' AndroidRuntime:E 2>&1)"
    if [ -n "$crash" ] || [ -n "$fatal" ]; then
        echo "=== crash buffer ===" >&2
        echo "$crash" >&2
        echo "=== fatal/error ===" >&2
        echo "$fatal" >&2
        return 1
    fi
    echo "No crash/FATAL log entries."
}

cmd_data_status() {
    local serial="${1:-}"
    echo "Checking $DATA_DIR on device..."
    adb $(serial_arg "$serial") shell "ls $DATA_DIR 2>&1"
}

cmd_run() {
    local serial="${1:-}"
    cmd_build
    cmd_install "$serial"
    cmd_launch "$serial"
    cmd_logs "$serial"
    cmd_crashcheck "$serial"
}

sub="${1:-}"; shift || true
case "$sub" in
    build)        cmd_build ;;
    install)      cmd_install "$@" ;;
    launch)       cmd_launch "$@" ;;
    tap)          cmd_tap "$@" ;;
    screenshot)   cmd_screenshot "$@" ;;
    logs)         cmd_logs "$@" ;;
    crashcheck)   cmd_crashcheck "$@" ;;
    data-status)  cmd_data_status "$@" ;;
    run)          cmd_run "$@" ;;
    *)
        echo "Usage: driver.sh {build|install|launch|tap|screenshot|logs|crashcheck|data-status|run} [args...] [serial]" >&2
        exit 1
        ;;
esac
