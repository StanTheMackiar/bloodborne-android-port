// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cmath>
#include <cstdio>
#include "gpu/shadps4/video_core/renderer_vulkan/ui_composition.h"

int main() {
    using namespace Vulkan::UiComposition;
    // Title/loading menus have no depth/camera. UI must still go to the native target.
    assert(Choose(true, true, false, true) == Background::Copy);
    assert(Choose(true, true, false, false) == Background::Copy);
    // In-game HUD: first reconstruct the scene, then draw the native UI over it.
    assert(Choose(true, true, true, true) == Background::Temporal);
    // Runtime FSR disable/failure must not change UI resolution.
    assert(Choose(true, true, true, false) == Background::Copy);
    assert(Choose(true, false, true, true) == Background::None);
    assert(Choose(false, true, true, true) == Background::None);
    assert(NativeViewport(1920, -1080));
    assert(!NativeViewport(960, -540));
    // The game's 16:9 screen in an output of another aspect ratio (BbDisplay): its edges, and the
    // push constant EmitUiEdgeStretch reads.
    assert(ScreenEdges(1920, 1080)[0] == 1.0f && ScreenEdges(1920, 1080)[1] == 1.0f);
    assert(std::abs(ScreenEdges(2560, 1080)[0] - (16.0f / 9.0f) / (2560.0f / 1080.0f)) < 1e-5f &&
           ScreenEdges(2560, 1080)[1] == 1.0f);
    assert(ScreenEdges(1920, 1200)[0] == 1.0f && ScreenEdges(1920, 1200)[1] < 1.0f);
    assert(EdgeParam(1.0f, 1.0f, false) == (65535u << 16 | 32767u << 1));
    assert((EdgeParam(0.75f, 1.0f, true) & 1u) == 1u && EdgeParam(0.0f, 0.0f, false) == 2u);
    assert(MovieShader(0x34e8a281) && MovieShader(0x24042a9b));
    assert(!MovieShader(0x0b0acf50)); // fullscreen post/tonemap also has a native viewport
    assert(!MovieShader(0x6c62a79f)); // shadow geometry
    const auto display = Scale(960, 540, 1920, 1080, false);
    assert(display[0] == 2 && display[1] == 2);
    const auto ui = Scale(960, 540, 1920, 1080, true);
    assert(ui[0] == 1 && ui[1] == 1); // don't double an already native UI viewport
    const auto larger = Scale(960, 540, 3840, 2160, true);
    assert(larger[0] == 2 && larger[1] == 2);
    std::puts("UI composition: PASS (menu without camera, HUD, FSR off, viewport scaling)");
}
