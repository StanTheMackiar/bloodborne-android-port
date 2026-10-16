<h1 align="center">Bloodborne-Android</h1>

<p align="center">
  <b>Bloodborne on Android.</b> Yharnam on Snapdragon phones and handhelds, and experimentally on Mali.
</p>

<p align="center">
  <a href="https://github.com/ducvd89/Bloodborne-Android/releases/latest"><img src="https://img.shields.io/github/v/release/ducvd89/Bloodborne-Android?label=download&color=8b1e1e" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/Android-9%2B-3ddc84?logo=android&logoColor=white" alt="Android 9+">
  <img src="https://img.shields.io/badge/GPU-Adreno%206xx%20%7C%207xx%20%7C%208xx-d6a64a" alt="Adreno">
  <img src="https://img.shields.io/badge/GPU-Mali%20G610%2B%20(experimental)-5a7bbf" alt="Mali (experimental)">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--2.0-blue" alt="GPL-2.0"></a>
</p>

<table align="center">
  <tr>
    <td align="center" width="40%"><img src="docs/images/ayn-thor.jpg" alt="Bloodborne running on the AYN Thor"><br><sub>AYN Thor · Snapdragon 8 Gen 2</sub></td>
    <td align="center" width="60%"><img src="docs/images/find-n5.jpg" alt="Bloodborne with the on-screen controller on the Oppo Find N5"><br><sub>Oppo Find N5 · Snapdragon 8 Elite · on-screen controller</sub></td>
  </tr>
</table>

Port of Bloodborne (PS4, **v1.09**, every retail release). The runtime and renderer run natively on ARM64, FEXCore translates the game's x86-64 code, and Mesa's Vulkan drivers drive the GPU (Turnip on Adreno, PanVK on Mali). Experimental.

## How it works

This is a port, not an emulator. No PS4 hardware, firmware or operating system is emulated, and no PS4 BIOS is needed. It works like Wine + DXVK for one game: the game's own `eboot.bin` runs in an Android app process, its calls to PS4 system libraries land in native replacements, and its graphics are translated to Vulkan. The game runs as a plain Android (NDK) process: no Linux system, X server or Python is installed on the phone. Because the PS4 has an x86-64 CPU and the phone an ARM64 one, the game's machine code needs one more layer.

**Translated while the game runs:**

| What | From | To | By |
|---|---|---|---|
| The game's CPU code (`eboot.bin`) | x86-64 (AVX, BMI) | ARM64 | [FEXCore](https://github.com/FEX-Emu/FEX), built into the game process as a library: only the game's code goes through it, no x86 Linux, Wine or Box64 |
| GPU commands the game writes itself (it links libGnm statically) | AMD GCN command buffers (PM4) | Vulkan commands | the renderer |
| Shaders | AMD GCN | SPIR-V | the renderer's shader recompiler |

**Native ARM64 code, no translation:**

- **Runtime**: replaces the PS4 system libraries the game imports: threads, mutexes, semaphores, memory, time, files, save data, controller input, audio with ATRAC9 decoding, video output, movie playback. A call from the game goes straight to this code.
- **Renderer**: based on [shadPS4](https://github.com/shadps4-emu/shadPS4)'s, through the [bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) Linux port: command processing, texture and buffer caches, FSR 3.1.
- **Vulkan driver**: a Mesa driver for Android that ships inside the APK and is picked for your GPU (see [GPU drivers](#gpu-drivers)), or the phone's own driver.
- **The Android app**: menu, on-screen controller, mods and PKG installers.

FEXCore runs in its fastest mode (no x86 memory-ordering emulation, reduced x87 precision). The PC build has no translation layer at all: there, the game's x86-64 code runs directly on the CPU.

## Features

- **Native Android program** — new in 0.9.0: no Linux system on the phone any more; the APK is about 37 MB and frames go straight to the screen
- **GPU driver picked for you** — new in 0.9.0: Turnip for Adreno 6xx/7xx and 8xx and PanVK for Mali ship inside the APK ([GPU drivers](#gpu-drivers))
- **Output resolution** (1080p, 720p or 480p) with **FSR 3.1** upscaling presets relative to it, and a **VSync** option — new in 0.9.0
- **Lossless Scaling frame generation** (2×–4×, needs your own Lossless.dll), changed while you play from its own menu page — new in 0.9.0; also **FSR 3.1 frame generation** (poor image quality for now)
- **Report a problem** — new in 0.9.0: saves a logs zip for a GitHub issue ([Reporting problems](#reporting-problems))
- **Mods**: install .zip, .7z or PS4 .pkg mods for 1.09, switch them on and off and set their load order
- **Cheats**: infinite health and infinite blood vials/items, switched on and off while you play
- **Game settings** in the app's menu: skip the online/offline screen (straight to the main menu), skip intros, free camera, debug menu
- **On-screen PlayStation controller** with an editable layout; physical controllers work too
- **Updates from the app**: menu → **Check for updates** downloads and installs the newest release
- 16:9 picture on any screen, including foldables

## Devices

| Tested | SoC | GPU | Driver (Automatic) |
|---|---|---|---|
| AYN Thor | Snapdragon 8 Gen 2 | Adreno 740 | Turnip 26.3.0 |
| Oppo Find N5 | Snapdragon 8 Elite | Adreno 830 | Turnip Gen8 V37 (0.8.x was tested with the Linux turnip/gen8 build) |

Other Adreno 6xx/7xx/8xx devices are untested. Mali (G610 and newer, Android 15+) is experimental and has not been run on a Mali phone yet. Requires Android 9+ and 4 KB memory pages; tested with 16 GB RAM.

## Install

1. Download the APK from [Releases](https://github.com/ducvd89/Bloodborne-Android/releases/latest) and install it.
2. Copy your own extracted dump of Bloodborne with the **1.09 update** merged into it to the phone (any release below).
3. Open the app, select the folder containing `eboot.bin` (or the folder that holds it), and allow storage access. The first launch takes a few minutes. The GPU driver for your phone is set up on its own.

Every retail release runs the same 1.09 executable, so all of them work:

| Region | Bloodborne | Game of the Year / The Old Hunters |
|---|---|---|
| US | CUSA00900 | CUSA03179 |
| EU | CUSA00207 | CUSA03173 |
| UK | CUSA00208 | |
| Japan | CUSA00299 | CUSA03014 |
| Asia | CUSA01363 | CUSA03023 |

Use the original 1.09 `eboot.bin`. Some dumps ship one with a 60 fps patch already applied; that one crashes in the gestures menu.

The APK contains no game files. Updates keep saves, settings, mods and the game folder, except once: updating from 0.7.x or older to 0.8.0 or later sets the new [default settings](#default-settings-and-performance). Updating from 0.8.x to 0.9.0 deletes the old Linux runtime from the app's storage on the first start (several hundred MB) and removes the 0.8.x Linux GPU drivers, which no longer load. Later releases install from the menu (**Check for updates**); Android asks once to allow the app to install apps. The updater skips betas: if you installed 0.9.0 Beta 1, install 0.9.0 by hand.

On ColorOS / OxygenOS (Oppo, OnePlus) a "Security warning" about a malicious app may appear at launch. Tap **Got it**; the game keeps running. ColorOS showed it for 0.8.x and other apps that run Linux programs, such as Termux and Winlator.

## The menu

Swipe in from the left edge (or press Back) to open the menu. Its sections:

- **Game:** resume, Game settings, Cheats, Mods, restart.
- **Graphics:** graphics settings (output resolution, upscaler and preset, VSync, FPS limit, model detail, effects), Frame generation (Lossless Scaling, applies at once) and the GPU driver.
- **Controls:** turn the on-screen controller on or off; edit its layout (move, resize or reset the buttons). With the controller on, drag anywhere outside the buttons to turn the camera.
- **Storage:** choose the game folder, back up and restore saves, quit.
- **App:** report a problem, check for updates (installing one closes the game, so save first).

Each entry shows its current setting underneath.

## Default settings and performance

The defaults are tuned on the AYN Thor: a 752×424 scene upscaled by FSR 3.1 to 1280×720, the costly effects (SSAO, SSR, dynamic shadows, depth of field, motion blur, the game's anti-aliasing) off, **lowest model detail** and a 60 FPS limit. Cheats start off. They apply on a first install and once when updating from 0.7.x or older; your controller mapping, GPU driver and mods choice are kept.

On the Thor most of the GPU's time goes to geometry, not pixels: a smaller render size barely changes the frame rate, the model detail setting does (lowest: about +10%).

The **output resolution** (1080p, 720p or 480p) is the size of the final picture and the game's interface; the upscaler preset sets the scene size relative to it:

| Output | Native | Quality | Balanced | Performance | Ultra Perf. |
|---|---|---|---|---|---|
| 1080p | 1920×1080 | 1280×720 | 1130×636 | 960×540 | 640×360 |
| 720p | 1280×720 | 854×480 | 752×424 | 640×360 | 426×240 |
| 480p | 854×480 | 570×320 | 502×282 | 428×240 | 284×160 |

FSR 3.1 runs at the output size, so 1080p costs more GPU time than 720p. **VSync** (off by default) shows every frame for at least one screen refresh, in order: smoother pacing for a little more latency (best on Android 12+; pick an FPS limit that divides the refresh rate).

## GPU drivers

The app carries three Android builds of Mesa's open-source Vulkan drivers and uses the one for your GPU when the GPU driver menu is on **Automatic** (the default):

| GPU | Driver |
|---|---|
| Adreno 6xx / 7xx | Turnip, Mesa 26.3.0 ([release](https://github.com/ducvd89/Bloodborne-Android/releases/tag/turnip-android-main-26.3.0-35b085c)) |
| Adreno 8xx | Turnip Gen8 V37 by StevenMXZ ([original](https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases/tag/v37), [our mirror](https://github.com/ducvd89/Bloodborne-Android/releases/tag/turnip-android-gen8-26.3.0-9c7e022)) |
| Mali G610 and newer (Android 15+) | PanVK V3.2 for mali_kbase, the Bachata S4 series ([release](https://github.com/ducvd89/Bloodborne-Android/releases/tag/panvk-android-v3.2-2a9358a)); experimental |

Other GPUs, and Mali on Android below 15, use the phone's own driver, which does not run the game yet on Adreno (black screen). A driver you chose yourself is kept. **Download** in the menu lists the 10 newest Android driver builds from this project's releases (Turnip and PanVK), marked for your GPU; **Install a zip** takes any adrenotools-style driver zip built for Android.

## Reporting problems

Menu → **Report a problem** saves `Bloodborne-logs-<date>.zip` to the Download folder: a device summary (phone, SoC, Android version, GPU, driver), your settings, the logs of the last two game runs and the app's log. No saves, game files, Lossless.dll or accounts. It can share the zip and opens a GitHub issue with the summary filled in: drag the zip into it.

## Game settings and cheats

**Game settings** are patches to the game, applied when it starts (Apply restarts it): skip the online/offline screen (on by default; there is no PSN here), skip the startup intros, the free camera (Cross + L3) and the game's debug menu (needs its font files).

**Cheats** switch at once, while you play:

- **Infinite health:** your HP refills to full every frame.
- **Infinite blood vials and items:** using a blood vial, bullet or other item leaves its count as it was. Items you move to storage or give away stay in your inventory too.

They are code hooks in the 1.09 executable (the same places as Shiningami's GoldHEN cheats), so they work on every release. They are always in place and switched by flags the game process reads from the app.

## Lossless Scaling frame generation

In the menu's Frame generation page (it applies at once, no restart): 2×, 3× or 4× frames, flow scale and performance mode. It needs `Lossless.dll` from your own copy of Lossless Scaling (Steam): the same page signs in to Steam to download just that file, or copies one you choose. It replaces FSR 3.1 frame generation and raises the screen's refresh rate to the FPS limit times the multiplier. How it works, and what is and is not verified: [docs/ANDROID_LSFG.md](docs/ANDROID_LSFG.md).

## Build

```bash
git clone --recursive https://github.com/ducvd89/Bloodborne-Android.git
cd Bloodborne-Android
bash tools/android/build_bionic_deps.sh      # once: the NDK and the native libraries
bash tools/android/build_bionic.sh           # the game process, bb-prepare, ps4-pkg-tool, liblsfg-bb.so
bash tools/android/build_release_apk.sh      # the runtime archive and the APK (fetches the bundled GPU drivers)
```

Build inputs (SDK, signing key, game icon) and the app's layout are in [docs/ANDROID_THOR.md](docs/ANDROID_THOR.md). The bundled drivers are pinned by checksum in `tools/android/fetch_bundled_drivers.sh`. Turnip for Android builds: `tools/android/build_turnip_android.sh` and `release_turnip_android.py`. The Linux PC build is documented in [README.linux.md](README.linux.md).

## Credits

[bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) (Linux port), [shadPS4](https://github.com/shadps4-emu/shadPS4), [ARM_bloodborne_pc](https://github.com/MaSieS4Fun/ARM_bloodborne_pc), [FEX](https://github.com/FEX-Emu/FEX), [fexdroid](https://github.com/cobrabm12/fexdroid), [Mesa](https://gitlab.freedesktop.org/mesa/mesa), [mesa-unified turnip/gen8](https://github.com/whitebelyash/mesa-unified), [StevenMXZ's Turnip builds](https://github.com/StevenMXZ/Adreno-Tools-Drivers) (Turnip Gen8 V37), [panvk-kbase-android](https://github.com/JICA98/panvk-kbase-android) (PanVK for Mali, Bachata V3 series), [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan), [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) and [GameNative's Android fork of it](https://github.com/Ragnarok93/lsfg-vk-android) (Lossless Scaling frame generation), and [GoldHEN cheats](https://github.com/GoldHEN/GoldHEN_Cheat_Repository) (Shiningami).

Licensed under [GPL-2.0](LICENSE); third-party notices in [THIRD_PARTY_LICENSES.txt](tools/android/frontend/THIRD_PARTY_LICENSES.txt) (the APK's copy also covers the bundled GPU drivers: Mesa's MIT licence, and GPL-3.0 for StevenMXZ's build of Turnip Gen8 V37). Not affiliated with Sony Interactive Entertainment, FromSoftware, Valve or the developers of Lossless Scaling.
