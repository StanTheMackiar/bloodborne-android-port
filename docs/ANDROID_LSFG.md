# Lossless Scaling frame generation on Android

The game runs as a native Android (Bionic) process, so Lossless Scaling's frame generation is no longer a Vulkan layer: `liblsfg-bb.so` is the framegen backend of [GameNative's Android fork of lsfg-vk 1.0](https://github.com/Ragnarok93/lsfg-vk-android) (MIT; pinned commit, `tools/android/build_lsfg_android.sh`) behind a small C API (`tools/android/lsfg/bb_lsfg.h`). libbbgpu loads it with `dlopen` only when the multiplier is 2 or more. Lossless Scaling itself is not included: it needs your own copy (Steam, app 993090).

## Using it

1. Graphics settings → **Lossless Scaling frame generation**: 2×, 3× or 4×, flow scale (0.25–1.0: lower is faster and the motion coarser) and performance mode (the light model).
2. **Sign in with Steam** downloads just `Lossless.dll` from your own copy (QR code or name/password; nothing but the file is kept), or **Choose Lossless.dll…** copies one from your storage (a PC copy is `steamapps/common/Lossless Scaling/Lossless.dll`). The file lives in `files/bbport/Lossless.dll`; `/sdcard/Bloodborne/Lossless.dll` is used when there is no such copy.
3. Apply: the game restarts. Without a Lossless.dll the game logs one line (`Lossless Scaling frame generation: no Lossless.dll`) and runs without it.

It replaces FSR 3.1 frame generation (the settings screen turns one off when you pick the other). It sees only finished frames, without the game's motion vectors, so expect more artifacts around fast motion and the HUD than with FSR 3.1 frame generation. Messages are in `files/bbport/logs/android-game.log` (`LSFG: ...`); `BB_LSFG_STATS=1` in `bbport/bionic-local.sh` logs real and generated frames a second.

## How a frame goes (gpu/shadps4/video_core/renderer_vulkan/vk_lsfg.cpp, vk_presenter.cpp)

For every real game frame, on the presenter's thread:

1. The final picture (before the overlay) is blitted into one of two input AHardwareBuffers (alternating) in a submission of its own; its semaphore is exported as a sync file.
2. `bb_lsfg_present` gives that sync file to the framegen, which runs on its own `VkDevice` of the same driver and physical device (made through the game's own `vkGetInstanceProcAddr`, so on the same Turnip), and makes the generated frames into output buffers. The call returns a sync file per generated frame and one for "done with the input buffers", which the next frame's copy waits for.
3. Each generated frame and then the real frame are blitted into swapchain buffers (waiting on the sync files on the GPU) and queued at once; only the hand-over to the app is spaced evenly over the real frame interval (`BB_LSFG_PACE=0` hands them over back to back).

Buffers change devices with queue-family ownership transfers to `VK_QUEUE_FAMILY_EXTERNAL`, as the fork's layer does. If a sync file cannot be exported or imported the call waits on the CPU (`BB_LSFG_SYNC=host` forces that); any other failure turns frame generation off and the game goes on with its own frames. The first four frames only fill the framegen's history.

## Pacing

Generated frames only look right when each is shown for one refresh. With frame generation on, libbbgpu asks the app for FIFO presentation whatever the VSync setting says, and the app picks the display mode of FPS limit × multiplier when the panel has it (30 × 2: 60 Hz; 30 × 4, 40 × 3 or 60 × 2: 120 Hz), else the fastest one. The presentation needs the multiplier plus three swapchain buffers.

## What the fork's patches do (tools/android/lsfg-android-patches)

- `0001`: the framegen's Vulkan instance comes from the entry point the game process gives (`volkInitializeCustom`), not volk's own loader (the process loads its driver directly: no Android loader, no layers).
- `0002`: the shaders are read from the one Lossless.dll the host names, not from a search of Steam folders (that removes the fork's config-file dependency).

The fork's layer, adaptive scheduler and config parser are not built. `tools/android/build_lsfg.sh` and `tools/android/lsfg-patches` are the old glibc layer build, kept for reference.

## Not verified on a device

Written and built without a device: the AHardwareBuffer transport mode the fork picks on Turnip/Adreno (`ahb_transport=` in the log line at start), whether both devices agree on the buffers' tiling, sync-file export and import between the two devices, the context creation time (the framegen's pipelines are compiled at the first frame) and the frame pacing.
