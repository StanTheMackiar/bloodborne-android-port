// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the game's frame rate set free by hooks in its code (BB_FPS=uncap, the default), not by
// patches: its frame pacer no longer waits for a 30 FPS frame, its step gets the frame's real time,
// and what it ties to a frame (a 30 FPS frame's time, a per-frame step, smoothing and damping) reads
// values the hooks recompute from that time every frame (bbport_frame_rate.cpp).
#pragma once

#include <cstdint>

namespace BbFrameRate {
/// Installs the hooks (at load, before the game runs: bbgpu_patch_image). Every site is checked
/// before anything is written: if one differs (or BB_FRAME_HOOKS=0, or BB_FPS=30) the game's code
/// is left as it was and Unlocked() stays false. Environment: BB_FRAME_HOOKS=0 (off),
/// BB_FPS (uncap, 60, 90: the most frames a second; 30: off), BB_FRAME_DT_MAX_FPS (the longest
/// step the game gets is 1/this s, 10..30; 30 by default, 20 keeps real time down to 20 FPS),
/// BB_FRAME_PACER_LIMIT=1 (the game's pacer waits for BB_FPS_LIMIT itself, as upstream),
/// BB_FRAME_DT_SMOOTH, BB_CLOTH_STIFFNESS (diagnostics).
void PatchImage(unsigned char* image, std::uint64_t size);
/// Whether the hooks run the game's frame rate: the video out then shows a frame as soon as the
/// game submits it (the game's own flip rate asks for 30 FPS), and the frame rate patches are not
/// needed (bb-probe's --fallback-patches apply only when this is false).
bool Unlocked();
} // namespace BbFrameRate
