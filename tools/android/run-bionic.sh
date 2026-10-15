#!/system/bin/sh
# Runs inside the app's sandbox (started by MainActivity.java with /system/bin/sh): the game process,
# bb-probe with libbbgpu/libbbcpu (an Android NDK build), from the app's data directory. It uses the
# phone's own Vulkan driver, or an adrenotools-style one the player installed (files/drivers/<name>/).
# The game files are prepared by bb-prepare, the mods overlay is built by the app (BB_APP0_DIR): no
# Python, no rootfs, no X server. libbbgpu presents through the app (BB_PRESENT_SOCKET).
# Only Android's own tools run here (toybox: sed, tail, cat, cp, mkdir, printf, rm, taskset).
# usage: run-bionic.sh [game|vulkan]   env from the app: BB_SCREEN_W/BB_SCREEN_H (the surface's size),
#        BB_GAME_DIR (the chosen game folder), BB_APP0_DIR (the folder to run from: the game with mods),
#        BB_ANDROID_INPUT=1
set -eu
files=${FXD_FILES:-/data/data/com.ducvd89.bloodborne/files}
base="$files/bbport"
bionic="$base/bionic"
export FXD_FILES="$files"
export PATH=/system/bin
export HOME="$files/home/bbport"
export TMPDIR="$files/tmp" XDG_RUNTIME_DIR="$files/tmp"
# FEXCore (libbbcpu) reads its Config.json from here (it runs only the guest's code: no x86 rootfs,
# loader or syscalls of its own).
export FEX_APP_CONFIG_LOCATION="$HOME/.fex-emu/"
export FEX_APP_DATA_LOCATION="$HOME/.fex-emu/"
export FEX_APP_CACHE_LOCATION="$HOME/.cache/fex-emu/"
mkdir -p "$HOME/.fex-emu" "$HOME/.cache/fex-emu" "$TMPDIR" "$base/user" "$base/logs" "$base/logs/failed-shaders"

# ---- Settings: bbport.ini (the app's settings screen writes it) turned into BB_* variables ----
export BB_CONFIG="$base/bbport.ini"
[ -f "$BB_CONFIG" ] || cp "$base/bbport.default.ini" "$BB_CONFIG"
setting() { sed -n "s/^$1=//p" "$BB_CONFIG" 2>/dev/null | tail -n 1; }
# The app's pad state file (PadBridge.java) and text requests (the character name: its touch keyboard).
export BB_ANDROID_INPUT=1
export BB_PAD_FILE="$base/android-pad.state" BB_PAD_QUIET=1
export BB_IME_FILE="$base/ime.request"
# The output is 1280x720; the preset sets the scene size, FSR 3.1 upscales it to the output.
upscaler=$(setting upscaler); preset=$(setting preset)
# FSR 4 (INT8 models) is not offered: on the Adreno 740 it took ~3 min to compile in Turnip, then ran
# at 2.4 FPS with a black scene. Any value but off means FSR 3.1.
case $upscaler in off) ;; *) upscaler=fsr3 ;; esac
echo "Upscaler setting: $upscaler"
case $preset in 0|1|2|3|4) ;; *) preset=3 ;; esac
render=1280x720
if [ "$upscaler" != off ]; then
    case $preset in 1) render=854x480 ;; 2) render=752x424 ;; 3) render=640x360 ;; 4) render=426x240 ;; esac
fi
export BB_FULLSCREEN=1 BB_UPSCALER=$upscaler BB_UPSCALE_PRESET=$preset
# FSR 3.1 frame generation (frame_generation=1): a frame between each two, with FSR 3.1 only.
[ "$(setting frame_generation)" = 1 ] && [ "$upscaler" = fsr3 ] && export BB_FRAME_GEN=1
# The GPU's check of indirect dispatch counts (vk_indirect_guard.h) is off; BB_INDIRECT_GUARD=1 in
# bionic-local.sh turns it on.
export BB_INDIRECT_GUARD=${BB_INDIRECT_GUARD:-0}
# The two-stage draw pipeline (bb:DrawRec) frees the GPU command thread: 31 -> 34 FPS on the Thor
# with all cores. BB_DRAW_PIPE=0 in bionic-local.sh turns it off.
export BB_DRAW_PIPE=${BB_DRAW_PIPE:-1}
export BB_WINDOW_SIZE=1280x720
export BB_RENDER_RES=$render BB_OUTPUT_RES=1280x720 BB_LIVE_RES=0
export BB_DMEM_MB=5056 BB_PREUPLOAD=0 BB_GUEST_IN_PLACE=0 BB_UFFD=0
export BB_COPY_GPU_BUFFERS=1 BB_GPU_WRITE_TWINS=1 BB_GPU_WRITE_TWINS_MAX=65536
# GPU memory is the phone's system RAM, shared with the game's 5 GB and Android: the texture
# collector works to a 4 GB budget instead of the driver's heap (the app was killed for memory once
# the world loaded), and idle copies of game memory go after 20 s instead of 60 s.
export BB_GC_BUDGET_MB=${BB_GC_BUDGET_MB:-4096} BB_VRAM_IDLE_SECONDS=${BB_VRAM_IDLE_SECONDS:-20}
# FPS limit (fps_limit): 30 is the game's own timing; above it the delta-time patch
# (fps=uncap.bin) with presents at once (vblank 480 Hz) and BB_FPS_LIMIT (0: none).
fps_limit=$(setting fps_limit)
case $fps_limit in 40|45|60|0) export BB_VBLANK_HZ=480 BB_FPS_LIMIT=$fps_limit ;;
    *) fps_limit=30; export BB_VBLANK_HZ=60 ;; esac
# Frame generation adds frames on screen only: at the game's own 30 FPS timing a slower frame still
# slows the game down. With it, 30 means the delta-time patch at 30 real frames a second.
if [ "${BB_FRAME_GEN:-0}" = 1 ] && [ "$fps_limit" = 30 ]; then
    fps_limit=30-delta; export BB_VBLANK_HZ=480 BB_FPS_LIMIT=30
fi
# CPU cores (thor_cpus, a taskset list such as 3-7): empty for all of them.
[ -z "${BB_THOR_CPUS:-}" ] && BB_THOR_CPUS=$(setting thor_cpus)

# ---- The GPU driver (bbport.ini gpu_driver) ----
# Empty or "system" is the phone's stock driver through its Vulkan loader (BB_VULKAN_DRIVER unset);
# else files/drivers/<name>/ made by DriverManager.java, whose driver.lib names the library (an
# adrenotools-style Turnip for Android), loaded directly.
gpu_driver=$(setting gpu_driver)
case $gpu_driver in .*|*/*|system) gpu_driver= ;; esac
unset BB_VULKAN_DRIVER
if [ -n "$gpu_driver" ]; then
    driver_lib=$(cat "$files/drivers/$gpu_driver/driver.lib" 2>/dev/null || true)
    case $driver_lib in
        ''|*/*|.*) echo "GPU driver $gpu_driver is not installed: system driver" ;;
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

# ---- Sound and picture ----
# The mixer writes s16 stereo 48 kHz into the FIFO the app plays (AudioBridge.java), only when the app
# made it: a FIFO nobody reads would fill. libbbgpu connects to the app's socket to present.
[ -p "$base/audio.fifo" ] && export BB_AUDIO_FIFO="$base/audio.fifo"
export BB_PRESENT_SOCKET="$files/bbpresent.sock"
export BB_SCREEN_W="${BB_SCREEN_W:-1280}" BB_SCREEN_H="${BB_SCREEN_H:-720}"
# libbbgpu: SPIR-V of pipelines the driver rejects is written here (vk_shader_util.cpp).
export BB_FAILED_SHADER_DIR="$base/logs/failed-shaders"
cp "$base/fex-compatible.json" "$HOME/.fex-emu/Config.json"
# Local overrides for this device (experiments, tuning): sourced last, not part of the runtime.
[ -f "$base/bionic-local.sh" ] && . "$base/bionic-local.sh"
cd "$base"
[ -x "$bionic/bin/bb-probe" ] || { echo "The Bionic runtime is missing: $bionic" >&2; exit 2; }

# ---- The game files and the patches ----
# The game folder chosen in the app (BB_GAME_DIR), else the runtime's own; bb-prepare builds the image
# in data/ (a no-op when it matches). The folder to run from (BB_APP0_DIR) is the app's mods overlay of
# the game folder, or the game folder itself.
if [ "${1:-game}" = game ]; then
    game_dir=${BB_GAME_DIR:-$base/game}
    [ -f "$game_dir/eboot.bin" ] || { echo "Game missing: $game_dir"; exit 2; }
    "$bionic/bin/bb-prepare" "$game_dir" "$base/data"
    mods_dir=$(setting mods_dir)
    [ -n "$mods_dir" ] || mods_dir=/storage/emulated/0/Bloodborne/mods
    export BB_MODS_DIR="$mods_dir"
    [ -n "${BB_MOD_TRACE:-}" ] && export BB_MOD_TRACE
    app0_dir=${BB_APP0_DIR:-$game_dir}
    # The settings' pre-compiled patches (build_patch_parts.py) as the words of $patch_args (none of the
    # paths has a space).
    parts="$base/patch-parts"
    [ -d "$parts" ] || { echo "The patches are missing: $parts" >&2; exit 2; }
    patch_args="--patches $parts/res-$render.bin"
    for key in effect_chromatic_aberration effect_dof effect_motion_blur effect_ssao \
               effect_game_aa effect_dynamic_shadows effect_ssr skip_intro debug_camera debug_menu model_lod; do
        value=$(setting "$key")
        [ -f "$parts/$key=$value.bin" ] && patch_args="$patch_args --patches $parts/$key=$value.bin"
    done
    [ "$fps_limit" != 30 ] && patch_args="$patch_args --patches $parts/fps=uncap.bin"
    # No PSN here: the title screen goes straight to the main menu offline.
    [ "$(setting skip_network_choice)" != 0 ] && [ -f "$parts/skip_network_choice=1.bin" ] &&
        patch_args="$patch_args --patches $parts/skip_network_choice=1.bin"
    # Cheats (the app's Cheats menu): their hooks are always in, switched live by the flags
    # BB_CHEATS_FILE holds (the app rewrites it; here its start, from bbport.ini).
    if [ -f "$parts/cheats.bin" ]; then
        patch_args="$patch_args --patches $parts/cheats.bin"
        export BB_CHEATS_FILE="$base/cheats.state"
        printf '%s%s\n' "$([ "$(setting cheat_health)" = 1 ] && echo 1 || echo 0)" \
            "$([ "$(setting cheat_items)" = 1 ] && echo 1 || echo 0)" > "$BB_CHEATS_FILE"
    fi
fi

# The Bionic libraries only: the app's own environment has no library path.
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
        set -- "$@" "$base/data/boot-linked.bin" $patch_args
        exec "$@" \
            --content-profile "$base/data/content.bin" \
            --app0 "$app0_dir" --user "$base/user" --timeout "${BB_TIMEOUT:-0}"
        ;;
    *) echo 'usage: run-bionic.sh game|vulkan' >&2; exit 2 ;;
esac
