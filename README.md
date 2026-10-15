<h1 align="center">Bloodborne-Android</h1>

<p align="center">
  <b>Bloodborne on Android.</b> Yharnam on Snapdragon phones and handhelds.
</p>

<p align="center">
  <a href="https://github.com/ducvd89/Bloodborne-Android/releases/latest"><img src="https://img.shields.io/github/v/release/ducvd89/Bloodborne-Android?label=download&color=8b1e1e" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/Android-9%2B-3ddc84?logo=android&logoColor=white" alt="Android 9+">
  <img src="https://img.shields.io/badge/GPU-Adreno%206xx%20%7C%207xx%20%7C%208xx-d6a64a" alt="Adreno">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--2.0-blue" alt="GPL-2.0"></a>
</p>

<table align="center">
  <tr>
    <td align="center" width="40%"><img src="docs/images/ayn-thor.jpg" alt="Bloodborne running on the AYN Thor"><br><sub>AYN Thor · Snapdragon 8 Gen 2</sub></td>
    <td align="center" width="60%"><img src="docs/images/find-n5.jpg" alt="Bloodborne with the on-screen controller on the Oppo Find N5"><br><sub>Oppo Find N5 · Snapdragon 8 Elite · on-screen controller</sub></td>
  </tr>
</table>

Port of Bloodborne (PS4, **v1.09**, every retail release). The runtime and renderer run natively on ARM64, FEXCore translates the game's x86-64 code, and Mesa Turnip drives the GPU. Experimental.

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
- **Vulkan driver**: the phone's own driver, or a Mesa Turnip for Android build you add from the app's **GPU driver** menu.
- **The Android app**: menu, on-screen controller, mods and PKG installers.

FEXCore runs in its fastest mode (no x86 memory-ordering emulation, reduced x87 precision). The PC build has no translation layer at all: there, the game's x86-64 code runs directly on the CPU.

## Features

- **Faster rendering** — new in 0.8.0: about 30 → 34–35 FPS on the AYN Thor in Central Yharnam, with steadier frame times
- **Updates from the app** — new in 0.8.0: menu → **Check for updates** downloads and installs the newest release
- **Mods**: install .zip, .7z or PS4 .pkg mods for 1.09, switch them on and off and set their load order
- **Cheats**: infinite health and infinite blood vials/items, switched on and off while you play
- **Game settings** in the app's menu: skip the online/offline screen (straight to the main menu), skip intros, free camera, debug menu
- **FSR 3.1** upscaling and **FSR 3.1 frame generation** (poor image quality for now). Lossless Scaling frame generation is paused: it was built for the former Linux runtime and comes back once it is ported to the Android one
- **On-screen PlayStation controller** with an editable layout; physical controllers work too
- **GPU driver menu**: the phone's own Vulkan driver, or a Turnip for Android build downloaded from this project's releases (or any adrenotools-style zip)
- 16:9 picture on any screen, including foldables

## Devices

| Tested | SoC | GPU | Turnip build |
|---|---|---|---|
| AYN Thor | Snapdragon 8 Gen 2 | Adreno 740 | Mesa main |
| Oppo Find N5 | Snapdragon 8 Elite | Adreno 830 | Mesa turnip/gen8 |

Other Adreno 6xx/7xx/8xx devices are untested. Requires Android 9+ and 4 KB memory pages; tested with 16 GB RAM.

## Install

1. Download the APK from [Releases](https://github.com/ducvd89/Bloodborne-Android/releases/latest) and install it.
2. Copy your own extracted dump of Bloodborne with the **1.09 update** merged into it to the phone (any release below).
3. Open the app, select the folder containing `eboot.bin` (or the folder that holds it), and allow storage access. The first launch takes a few minutes.

Every retail release runs the same 1.09 executable, so all of them work:

| Region | Bloodborne | Game of the Year / The Old Hunters |
|---|---|---|
| US | CUSA00900 | CUSA03179 |
| EU | CUSA00207 | CUSA03173 |
| UK | CUSA00208 | |
| Japan | CUSA00299 | CUSA03014 |
| Asia | CUSA01363 | CUSA03023 |

Use the original 1.09 `eboot.bin`. Some dumps ship one with a 60 fps patch already applied; that one crashes in the gestures menu.

The APK contains no game files. Updates keep saves, and settings too, except once: updating from 0.7.x or older to 0.8.0 sets the new [default settings](#default-settings-and-performance). Later releases install from the menu (**Check for updates**); Android asks once to allow the app to install apps.

On ColorOS / OxygenOS (Oppo, OnePlus) a "Security warning" about a malicious app may appear at launch. Tap **Got it**; the game keeps running. ColorOS shows it for any app that runs Linux programs, such as Termux and Winlator.

## The menu

Swipe in from the left edge (or press Back) to open the menu. Its sections:

- **Game:** resume, Game settings, Cheats, Mods, restart.
- **Graphics:** graphics settings (upscaler, render size, FPS limit, model detail, effects) and the GPU driver.
- **Controls:** turn the on-screen controller on or off; edit its layout (move, resize or reset the buttons). With the controller on, drag anywhere outside the buttons to turn the camera.
- **Storage:** choose the game folder, quit.
- **App:** check for updates (installing one closes the game, so save first).

Each entry shows its current setting underneath.

## Default settings and performance

The defaults are tuned on the AYN Thor: a 752×424 scene upscaled by FSR 3.1 to 1280×720, the costly effects (SSAO, SSR, dynamic shadows, depth of field, motion blur, the game's anti-aliasing) off, **lowest model detail** and a 60 FPS limit. Cheats start off. They apply on a first install and once when updating from 0.7.x or older; your controller mapping, GPU driver and mods choice are kept.

On the Thor most of the GPU's time goes to geometry, not pixels: a smaller render size barely changes the frame rate, the model detail setting does (lowest: about +10%).

## Game settings and cheats

**Game settings** are patches to the game, applied when it starts (Apply restarts it): skip the online/offline screen (on by default; there is no PSN here), skip the startup intros, the free camera (Cross + L3) and the game's debug menu (needs its font files).

**Cheats** switch at once, while you play:

- **Infinite health:** your HP refills to full every frame.
- **Infinite blood vials and items:** using a blood vial, bullet or other item leaves its count as it was. Items you move to storage or give away stay in your inventory too.

They are code hooks in the 1.09 executable (the same places as Shiningami's GoldHEN cheats), so they work on every release. They are always in place and switched by flags the game process reads from the app.

## Lossless Scaling frame generation

Paused. It ran through lsfg-vk inside the former Linux runtime (see [docs/ANDROID_LSFG.md](docs/ANDROID_LSFG.md)); the app no longer has the Lossless Scaling and Steam sign-in menus. It comes back once the layer is ported to the Android runtime.

## Build

```bash
git clone --recursive https://github.com/ducvd89/Bloodborne-Android.git
cd Bloodborne-Android
bash tools/android/build_bionic_deps.sh      # once: the NDK and the native libraries
bash tools/android/build_bionic.sh           # the game process, bb-prepare, ps4-pkg-tool
bash tools/android/build_release_apk.sh      # the runtime archive and the APK
```

Build inputs (SDK, signing key, game icon) and the app's layout are in [docs/ANDROID_THOR.md](docs/ANDROID_THOR.md). Turnip for Android builds: `tools/android/build_turnip_android.sh` and `release_turnip_android.py`. The Linux PC build is documented in [README.linux.md](README.linux.md).

## Credits

[bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) (Linux port), [shadPS4](https://github.com/shadps4-emu/shadPS4), [ARM_bloodborne_pc](https://github.com/MaSieS4Fun/ARM_bloodborne_pc), [FEX](https://github.com/FEX-Emu/FEX), [fexdroid](https://github.com/cobrabm12/fexdroid), [Mesa](https://gitlab.freedesktop.org/mesa/mesa), [mesa-unified turnip/gen8](https://github.com/whitebelyash/mesa-unified), [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan), [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) and [GameNative](https://github.com/utkarshdalal/GameNative) (for LSFG on Android, paused), and [GoldHEN cheats](https://github.com/GoldHEN/GoldHEN_Cheat_Repository) (Shiningami).

Licensed under [GPL-2.0](LICENSE); third-party notices in [THIRD_PARTY_LICENSES.txt](tools/android/frontend/THIRD_PARTY_LICENSES.txt). Not affiliated with Sony Interactive Entertainment, FromSoftware, Valve or the developers of Lossless Scaling.
