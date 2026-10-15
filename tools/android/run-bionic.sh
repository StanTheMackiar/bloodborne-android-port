#!/bin/sh
# Runs inside the app's sandbox: the Bionic (Android NDK) build of the game process, bb-probe with
# libbbgpu/libbbcpu, started from the app's data directory with the phone's own Vulkan driver (or one
# the player installed, files/drivers/<name>/). The rootfs of the glibc runtime stays for its
# scripts only: its Python prepares the game files and the mods overlay (run-common.sh). The game
# itself needs no X server, rootfs loader or Mesa: libbbgpu presents through the app (BB_PRESENT_SOCKET).
# usage: run-bionic.sh [game|vulkan]   env from the app: BB_SCREEN_W/BB_SCREEN_H (the surface's size)
set -eu
files=${FXD_FILES:-/data/data/com.ducvd89.bloodborne/files}
host="$files/rootfs"
base="$files/bbport"
bionic="$base/bionic"
export FXD_FILES="$files" FXD_ROOT="$host"
export PATH="$host/usr/bin:$host/bin:/system/bin"
export HOME="$files/home/bbport"
export TMPDIR="$host/tmp" XDG_RUNTIME_DIR="$host/tmp"
# FEXCore (libbbcpu) reads its Config.json from here (FEX_ROOTFS is not needed: it runs only the
# guest's code, no x86 rootfs, loader or syscalls of its own).
export FEX_APP_CONFIG_LOCATION="$HOME/.fex-emu/"
export FEX_APP_DATA_LOCATION="$HOME/.fex-emu/"
export FEX_APP_CACHE_LOCATION="$HOME/.cache/fex-emu/"
export BB_ANDROID_INPUT=1
bb_lsfg() { :; } # the Lossless Scaling layer is a glibc build: not offered here
. "$base/run-common.sh" # BB_CONFIG, setting, the graphics/fps/pad/mods settings (shared with run-thor.sh)
[ -n "$(setting lsfg_multiplier)" ] && [ "$(setting lsfg_multiplier)" != 1 ] &&
    echo 'Lossless Scaling frame generation: not available in the Bionic runtime'

# The GPU driver (bbport.ini gpu_driver): empty or "system" is the phone's stock driver through its
# Vulkan loader (BB_VULKAN_DRIVER unset); else files/drivers/<name>/ made by DriverManager.java, whose
# driver.lib names the library (an adrenotools-style Turnip for Android), loaded directly.
gpu_driver=$(setting gpu_driver)
case $gpu_driver in .*|*/*|system) gpu_driver= ;; esac
unset BB_VULKAN_DRIVER
if [ -n "$gpu_driver" ]; then
    driver_lib=$(cat "$files/drivers/$gpu_driver/driver.lib" 2>/dev/null || true)
    case $driver_lib in
        ''|*/*|.*) echo "GPU driver $gpu_driver is not installed for this runtime: system driver" ;;
        *)
            if [ -f "$files/drivers/$gpu_driver/$driver_lib" ]; then
                export BB_VULKAN_DRIVER="$files/drivers/$gpu_driver/$driver_lib"
                echo "GPU driver: $gpu_driver (installed from a zip)"
            else
                echo "GPU driver $gpu_driver has no library: system driver"
            fi ;;
    esac
fi
[ -n "${BB_VULKAN_DRIVER:-}" ] || echo 'GPU driver: system (stock)'

# Sound: the mixer writes s16 stereo 48 kHz into the FIFO the app plays (AudioBridge.java), only when
# the app made it: a FIFO nobody reads would fill. Present: libbbgpu connects to the app's socket.
[ -p "$base/audio.fifo" ] && export BB_AUDIO_FIFO="$base/audio.fifo"
export BB_PRESENT_SOCKET="$files/bbpresent.sock"
export BB_SCREEN_W="${BB_SCREEN_W:-1280}" BB_SCREEN_H="${BB_SCREEN_H:-720}"
mkdir -p "$HOME/.fex-emu" "$HOME/.cache/fex-emu" "$base/user" "$base/logs"
cp "$base/fex-compatible.json" "$HOME/.fex-emu/Config.json"
# Local overrides for this device (experiments, tuning): sourced last, not part of the bundle.
[ -f "$base/bionic-local.sh" ] && . "$base/bionic-local.sh"
cd "$base"
[ -x "$bionic/bin/bb-probe" ] || { echo "The Bionic runtime is missing: $bionic" >&2; exit 2; }
# The game files and the mods overlay come from the rootfs's Python, still with the app's glibc paths.
[ "${1:-game}" = game ] && bb_prepare_game
# From here on the glibc libraries of the app's own LD_LIBRARY_PATH must not reach a Bionic process.
LD_LIBRARY_PATH="$bionic/lib"; export LD_LIBRARY_PATH
unset LD_PRELOAD
case ${1:-game} in
    vulkan)
        exec "$bionic/bin/bb-probe" --vulkan-only
        ;;
    game)
        set -- "$bionic/bin/bb-probe"
        # BB_THOR_CPUS: the CPUs the game may run on (taskset -c list), e.g. 3-7 big cores only.
        [ -n "${BB_THOR_CPUS:-}" ] && set -- /system/bin/taskset -c "$BB_THOR_CPUS" "$@"
        parts="$base/arm64/patch-parts"
        [ -d "$parts" ] || { echo "The patches are missing: $parts" >&2; exit 2; }
        bb_patch_args
        set -- "$@" "$base/data/boot-linked.bin" $patch_args
        exec "$@" \
            --content-profile "$base/data/content.bin" \
            --app0 "$app0_dir" --user "$base/user" --timeout "${BB_TIMEOUT:-0}"
        ;;
    *) echo 'usage: run-bionic.sh game|vulkan' >&2; exit 2 ;;
esac
