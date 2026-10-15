#!/bin/sh
# Runs inside fexdroid's Android app sandbox, with its patched ARM Linux runtime.
set -eu
files=${FXD_FILES:-/data/data/com.ducvd89.bloodborne/files}
host="$files/rootfs"
base="$files/bbport"
export FXD_FILES="$files" FXD_ROOT="$host"
export PATH="$host/usr/bin:$host/bin:/system/bin"
export HOME="$files/home/bbport"
export TMPDIR="$host/tmp" XDG_RUNTIME_DIR="$host/tmp"
export FEX_ROOTFS="$base/rootfs"
export FEX_APP_CONFIG_LOCATION="$HOME/.fex-emu/"
export FEX_APP_DATA_LOCATION="$HOME/.fex-emu/"
export FEX_APP_CACHE_LOCATION="$HOME/.cache/fex-emu/"
export GLIBC_TUNABLES='glibc.cpu.hwcaps=-AVX2,-AVX,-AVX_Fast_Unaligned_Load,-AVX2_Usable,-AVX_Usable'
# Lossless Scaling frame generation (lsfg_multiplier 2-4): the lsfg-vk 1.0 layer with our Turnip
# patches (bbport/arm64/lsfg, build_lsfg.sh) and Lossless.dll from the player's own Lossless
# Scaling (Steam), next to the game folder or in bbport/. It replaces FSR 3.1 frame generation.
# Its messages (lsfg-vk: ...) go to the game log.
bb_lsfg() {
lsfg=$(setting lsfg_multiplier)
case $lsfg in 2|3|4) ;; *) lsfg= ;; esac
if [ -n "$lsfg" ] && [ -f "$base/arm64/lsfg/VkLayer_LS_frame_generation.json" ]; then
    lsfg_dll=
    for dll in "${BB_GAME_DIR:+${BB_GAME_DIR%/*}/Lossless.dll}" "$base/Lossless.dll"; do
        [ -n "$dll" ] && [ -f "$dll" ] && { lsfg_dll=$dll; break; }
    done
    if [ -n "$lsfg_dll" ]; then
        flow=$(setting lsfg_flow_scale)
        case $flow in 0.25|0.5|0.75|1|1.0) ;; *) flow=1.0 ;; esac
        performance=$([ "$(setting lsfg_performance)" = 1 ] && echo true || echo false)
        unset BB_FRAME_GEN
        # Config file mode: lsfg-vk watches it, so the app changes the multiplier (1: off), flow
        # scale and performance mode while the game runs (SettingsDialog.showLossless). The file
        # exists only while the layer is loaded; BB_LSFG: vk_swapchain.cpp restarts without it if
        # it fails. An implicit layer, so the layer's own framegen instance skips it (DISABLE_LSFG).
        # Present mode: FIFO paces the generated frames (LSFG_PRESENT=mailbox|immediate to try).
        export BB_LSFG=1 LSFG_CONFIG="$base/lsfg-vk.toml"
        export VK_ADD_IMPLICIT_LAYER_PATH="$base/arm64/lsfg"
        cat > "$LSFG_CONFIG.tmp" <<EOF
version = 1

[global]
dll = "$lsfg_dll"

[[game]]
exe = "bb-probe"
multiplier = $lsfg
flow_scale = $flow
performance_mode = $performance
experimental_present_mode = "${LSFG_PRESENT:-fifo}"
EOF
        mv -f "$LSFG_CONFIG.tmp" "$LSFG_CONFIG"
        echo "Lossless Scaling frame generation: x$lsfg, flow scale $flow, $lsfg_dll"
    else
        lsfg=; echo 'Lossless Scaling frame generation: no Lossless.dll next to the game folder'
    fi
else
    lsfg=
fi
}
. "$base/run-common.sh" # BB_CONFIG, setting, the graphics/fps/pad/mods settings (shared with run-bionic.sh)
# The GPU driver. One the player installed from a zip (the app's GPU driver menu, DriverManager.java)
# turns the automatic choice off: drivers/<gpu_driver>/icd.json names its library there, the zip's
# other libraries beside it. Without one (gpu_driver empty or not installed), the bundled Turnip by
# GPU: Mesa main for Adreno 6xx/7xx (the Thor's 740, chip id 0x43...), the turnip/gen8 build on
# Adreno 8xx (0x44..., e.g. the 8 Elite's 830). BB_TURNIP=main|gen8 overrides that choice.
gpu_driver=$(setting gpu_driver)
case $gpu_driver in .*|*/*) gpu_driver= ;; esac
if [ -n "$gpu_driver" ] && [ -f "$files/drivers/$gpu_driver/icd.json" ]; then
    icd="$files/drivers/$gpu_driver/icd.json"
    export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}$files/drivers/$gpu_driver"
    echo "GPU driver: $gpu_driver (installed from a zip, automatic choice off)"
else
    [ -n "$gpu_driver" ] && echo "GPU driver $gpu_driver is not installed: automatic choice"
    turnip=${BB_TURNIP:-}
    if [ -z "$turnip" ]; then
        case $("$base/arm64/bin/kgsl-chip-id" 2>/dev/null || true) in
            0x44*) turnip=gen8 ;;
            *) turnip=main ;;
        esac
    fi
    icd="$host/usr/share/vulkan/icd.d/freedreno_icd.aarch64.json"
    [ "$turnip" = gen8 ] && [ -f "$host/usr/share/vulkan/icd.d/freedreno_gen8_icd.aarch64.json" ] &&
        icd="$host/usr/share/vulkan/icd.d/freedreno_gen8_icd.aarch64.json"
    echo "Turnip: ${icd##*/}"
fi
export VK_DRIVER_FILES="$icd"
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
# Sound: SDL's disk driver mixes into the FIFO the app plays (AudioBridge.java). Only when the
# app made it: a FIFO nobody reads would stall the game's audio. Its reads pace the output.
if [ "${BB_ANDROID_INPUT:-0}" = 1 ] && [ -p "$base/audio.fifo" ]; then
    export SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE="$base/audio.fifo"
    export SDL_AUDIO_DISK_TIMESCALE=0 SDL_AUDIO_DEVICE_SAMPLE_FRAMES=512
fi
# Diagnostics stay off in releases: BB_FRAME_STATS (per-frame timers and stall logs; any value
# turns them on) and BB_PRESENT_DUMP_TRIGGER go in thor-local.sh when needed.
export SDL_VIDEODRIVER=x11 DISPLAY=:0
export MESA_VK_WSI_DEBUG=sw MESA_VK_WSI_PRESENT_MODE=relaxed
export FEXDROID_PRESENT="$host/tmp/fxpresent.sock"
export SDL_JOYSTICK_HIDAPI=0
# Find the built-in controller again after a reboot (event numbers can change).
for thor_node in /sys/class/input/event*; do
    [ -r "$thor_node/device/id/vendor" ] && [ -r "$thor_node/device/id/product" ] || continue
    IFS= read -r thor_vendor < "$thor_node/device/id/vendor"
    IFS= read -r thor_product < "$thor_node/device/id/product"
    if [ "$thor_vendor:$thor_product" = '2020:0112' ]; then
        export SDL_JOYSTICK_DEVICE="/dev/input/${thor_node##*/}"
        break
    fi
done
export SDL_GAMECONTROLLERCONFIG_FILE="$base/thor-gamecontrollerdb.txt"
export BB_GAMEPAD='030018dc202000001201000000000000'
export PULSE_SERVER="unix:$host/tmp/pulse/native" PULSE_LATENCY_MSEC=60
mkdir -p "$HOME/.fex-emu" "$HOME/.cache/fex-emu" "$base/user" "$base/logs"
cp "$base/fex-compatible.json" "$HOME/.fex-emu/Config.json"
# Local overrides for this device (experiments, tuning): sourced last, not part of the bundle.
[ -f "$base/thor-local.sh" ] && . "$base/thor-local.sh"
cd "$base"
case ${1:-game} in
    gamepads)
        exec "$host/usr/bin/FEX" "$base/bin/bb-gpu-capabilities" --gamepads
        ;;
    controller)
        exec "$host/usr/bin/FEX" "$base/bin/controller-probe" 120
        ;;
    cpu)
        exec "$host/usr/bin/FEX" "$base/bin/bb-probe" "$base/data/cpu-fixture.bin" --cpu-only
        ;;
    vulkan)
        exec "$host/usr/bin/FEX" "$base/bin/bb-probe" --vulkan-only
        ;;
    renderer)
        exec "$host/usr/bin/FEX" "$base/bin/renderer-probe"
        ;;
    window)
        exec "$host/usr/bin/FEX" "$base/bin/bb-probe" "$base/data/cpu-fixture.bin" --timeout 60
        ;;
    game)
        bb_prepare_game
        # The native ARM64 build (runtime and GPU native, the game's code in FEXCore) when it is
        # installed; BB_THOR_X86=1: the whole x86-64 loader under FEX instead.
        if [ -x "$base/arm64/bin/bb-probe" ] && [ "${BB_THOR_X86:-0}" != 1 ]; then
            unset GLIBC_TUNABLES
            set -- "$base/arm64/bin/bb-probe"; native=1
            # BB_THOR_CPUS: the CPUs the game may run on (taskset -c list), e.g. 3-7 big cores only.
            [ -n "${BB_THOR_CPUS:-}" ] && set -- taskset -c "$BB_THOR_CPUS" "$@"
        else
            set -- "$host/usr/bin/FEX" "$base/bin/bb-probe"; native=0
        fi
        # Pre-compiled patches for the settings (build_thor_patch_parts.py; the native loader
        # takes several), else the bundle's single file.
        parts="$base/arm64/patch-parts"
        if [ "$native" = 1 ] && [ -d "$parts" ]; then
            bb_patch_args
            set -- "$@" "$base/data/boot-linked.bin" $patch_args
        else
            set -- "$@" "$base/data/boot-linked.bin" --patches "$base/data/patches.bin"
        fi
        exec "$@" \
            --content-profile "$base/data/content.bin" \
            --app0 "$app0_dir" --user "$base/user" --timeout "${BB_TIMEOUT:-0}"
        ;;
    *) echo 'usage: run-thor.sh cpu|vulkan|renderer|window|gamepads|controller|game' >&2; exit 2 ;;
esac
