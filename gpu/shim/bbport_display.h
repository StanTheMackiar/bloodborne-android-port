// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the output's aspect ratio (any, not only 16:9) and the scene size, by hooks in the game's
// code and the startup resolution patch's immediates. Ported from deadinside28/bloodborne_pc (0.6-pre1,
// "any aspect ratio / resolution, live, through hooks") to the Bionic build, where the game's code runs
// in FEXCore: the detours are guest code only (x86 stubs that touch memory) plus host functions the
// stubs call through guest_cpu_host_function; the host calls guest functions through guest_cpu_call.
//
// The output is BB_OUTPUT_RES (WxH, the final frame and the UI's size); its aspect ratio is the display's.
// Other than 16:9 (the game's own), the hooks give
//  - the camera the output's aspect ratio (wider: more at the sides at the same vertical field of view;
//    taller: the vertical field of view widens, the width stays),
//  - every full-screen Scaleform movie a viewport with the output's pixel aspect ratio (the movie shows
//    its 16:9 screen in the middle of a wider/taller view, squeezed into the game's 1920x1080 UI frame,
//    which the output stretches back: UiComposition::Scale),
//  - the HUD's markers over characters the output's placement,
//  - the HUD's groups the output's edges (BB_HUD_ANCHOR=0: left in the 16:9 middle).
// The game's own scene render target is set by the resolution patch (BB_RENDER_RES: the immediates of
// the patch are rewritten here for any size), so the scene is drawn with the output's aspect ratio.
// Nothing is installed at 16:9.
#pragma once

#include <cstdint>

namespace BbDisplay {
/// The output's size from BB_OUTPUT_RES (0x0 when not set).
void OutputSize(std::uint32_t& width, std::uint32_t& height);
/// The output's aspect ratio (width / height), 16:9 when BB_OUTPUT_RES is not set.
float Aspect();
/// Whether the aspect ratio differs from the game's 16:9 (BB_ASPECT_HOOKS=0 forces false).
bool NonNative();

/// At load, before the game runs (bbgpu_patch_image; the patch files are applied already): the scene
/// size (BB_RENDER_RES) into the resolution patch's immediates and, for another aspect ratio, the hooks.
/// Every hook site is checked before the first byte is written: a site that differs leaves the game's
/// code as it was.
void PatchImage(unsigned char* image, std::uint64_t size);
} // namespace BbDisplay
