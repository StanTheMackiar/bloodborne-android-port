#!/bin/sh
# Shared by run-thor.sh (glibc runtime) and run-bionic.sh (Bionic runtime): the game settings from
# bbport.ini turned into BB_* variables, and the game folder / mods / patch setup. Sourced after the
# caller set $files, $host and $base; a caller may define bb_lsfg (Lossless Scaling setup) first.
export BB_CONFIG="$base/bbport.ini"
[ -f "$BB_CONFIG" ] || cp "$base/bbport.default.ini" "$BB_CONFIG"
setting() { sed -n "s/^$1=//p" "$BB_CONFIG" 2>/dev/null | tail -n 1; }
# BB_ANDROID_INPUT (set by the app): its pad state file and text requests.
if [ "${BB_ANDROID_INPUT:-0}" = 1 ]; then
    export BB_PAD_FILE="$base/android-pad.state" BB_PAD_QUIET=1
    # Text the game asks for (the character name): the app's touch keyboard.
    export BB_IME_FILE="$base/ime.request"
fi
# Graphics settings (the app's settings screen writes bbport.ini). The output is the Thor's
# 1280x720 display; the preset sets the scene size, FSR 3.1 upscales it to the output.
upscaler=$(setting upscaler); preset=$(setting preset)
# FSR 4 and 4.1.1 (INT8 models) are not offered: on the Thor's Adreno 740 FSR 4 took ~3 min to
# compile in Turnip, then ran at 2.4 FPS (~470 ms of GPU time a frame) with a black scene, and with
# the Lossless Scaling layer it crashed in Turnip. Any value but off means FSR 3.1.
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
# The GPU's check of indirect dispatch counts (upstream 0.5, vk_indirect_guard.h) is off: it was
# suspected of 0.5.0's hangs on the Thor (those came from the Lossless Scaling layer's later
# changes, undone in 0.5.1) and is not tested here on its own. BB_INDIRECT_GUARD=1 in
# thor-local.sh turns it on.
export BB_INDIRECT_GUARD=${BB_INDIRECT_GUARD:-0}
# The two-stage draw pipeline (bb:DrawRec) turns itself on only with 8 or more CPUs the game may
# use, so thor_cpus=3-7 turned it off. On, it frees the GPU command thread: 31 -> 34 FPS on the
# Thor with all cores. BB_DRAW_PIPE=0 in thor-local.sh turns it off.
export BB_DRAW_PIPE=${BB_DRAW_PIPE:-1}
# Lossless Scaling frame generation: only where the caller offers it (run-thor.sh).
lsfg=
if command -v bb_lsfg >/dev/null 2>&1; then bb_lsfg; fi
[ -n "$lsfg" ] || rm -f "$base/lsfg-vk.toml"
# FSR 4 model assets (fetch_fsr4_assets.sh), in the native bundle when fetched.
[ -d "$base/arm64/fsr4_shaders" ] && export BB_FSR4_DIR="$base/arm64/fsr4_shaders"
export BB_WINDOW_SIZE=1280x720
export BB_RENDER_RES=$render BB_OUTPUT_RES=1280x720 BB_LIVE_RES=0
export BB_DMEM_MB=5056 BB_PREUPLOAD=0 BB_GUEST_IN_PLACE=0 BB_UFFD=0
export BB_COPY_GPU_BUFFERS=1 BB_GPU_WRITE_TWINS=1 BB_GPU_WRITE_TWINS_MAX=65536
# GPU memory is the Thor's system RAM, shared with the game's 5 GB and Android: the texture
# collector works to a 4 GB budget instead of the driver's 11 GB heap (the app was killed for
# memory once the world loaded), and idle copies of game memory go after 20 s instead of 60 s.
export BB_GC_BUDGET_MB=${BB_GC_BUDGET_MB:-4096} BB_VRAM_IDLE_SECONDS=${BB_VRAM_IDLE_SECONDS:-20}
# FPS limit (fps_limit): 30 is the game's own timing; above it the delta-time patch
# (fps=uncap.bin) with presents at once (vblank 480 Hz, as run.sh) and BB_FPS_LIMIT (0: none).
fps_limit=$(setting fps_limit)
case $fps_limit in 40|45|60|0) export BB_VBLANK_HZ=480 BB_FPS_LIMIT=$fps_limit ;;
    *) fps_limit=30; export BB_VBLANK_HZ=60 ;; esac
# Frame generation adds frames on screen only: at the game's own 30 FPS timing a slower frame
# still slows the game down. With it, 30 means the delta-time patch at 30 real frames a second.
if { [ "${BB_FRAME_GEN:-0}" = 1 ] || [ -n "$lsfg" ]; } && [ "$fps_limit" = 30 ]; then
    fps_limit=30-delta; export BB_VBLANK_HZ=480 BB_FPS_LIMIT=30
fi
# CPU cores (thor_cpus, a taskset list such as 3-7): empty for all of them.
[ -z "${BB_THOR_CPUS:-}" ] && BB_THOR_CPUS=$(setting thor_cpus)
:

# The game folder chosen in the app (shared storage), else the bundle's own: prepares its files and
# the mods overlay. Sets game_dir and app0_dir.
bb_prepare_game() {
    game_dir=${BB_GAME_DIR:-$base/game}
    [ -f "$game_dir/eboot.bin" ] || { echo "Game missing: $game_dir"; exit 2; }
    if [ -x "$host/usr/bin/python3.13" ]; then
        PYTHONHOME="$host/usr" "$host/usr/bin/python3.13" \
            "$base/scripts/prepare_game.py" "$game_dir" "$base/data"
    elif [ ! -f "$base/data/boot-linked.bin" ]; then
        echo 'Python runtime missing; cannot prepare the selected game files' >&2
        exit 2
    fi
    # Loose-file mods (mods_enabled, default on): a symlink overlay of the game folder. The app builds it
    # (ModManager.apply, in Java) right before the launch and passes the folder to run in BB_APP0_DIR.
    mods_dir=$(setting mods_dir)
    [ -n "$mods_dir" ] || mods_dir=/storage/emulated/0/Bloodborne/mods
    export BB_MODS_DIR="$mods_dir"
    [ -n "${BB_MOD_TRACE:-}" ] && export BB_MOD_TRACE
    app0_dir=$game_dir
    if [ -n "${BB_APP0_DIR:-}" ]; then
        # The app's overlay (or the game folder itself without mods). Without it (a launch from a shell, or
        # by an older app) the rootfs's Python builds it below.
        app0_dir=$BB_APP0_DIR
    elif [ "$(setting mods_enabled)" != 0 ] && [ -x "$host/usr/bin/python3.13" ]; then
        mods_game=$(PYTHONHOME="$host/usr" "$host/usr/bin/python3.13" "$base/scripts/mods_android.py" apply \
            --game "$game_dir" --out "$base/mod-game" --mods-dir "$mods_dir" \
            --config "$base/mods.json" | tail -n 1) || mods_game=
        if [ -n "$mods_game" ] && [ -f "$mods_game/eboot.bin" ]; then
            app0_dir=$mods_game
        else
            echo 'Mods: could not be set up, starting without them'
        fi
    else
        echo 'Mods: off'
    fi
    [ "$app0_dir" = "$game_dir" ] && rm -rf "$base/mod-game"
    return 0
}
# The settings' pre-compiled patches (build_thor_patch_parts.py) as the words of $patch_args (none of
# the paths has a space); exports BB_CHEATS_FILE. Needs $parts and $render.
bb_patch_args() {
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
    return 0
}
