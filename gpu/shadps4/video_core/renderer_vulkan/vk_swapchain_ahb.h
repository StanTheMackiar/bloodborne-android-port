// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Bionic "swapchain": N VkImages backed by AHardwareBuffers, shown by the Android app.
//
// No VkSurfaceKHR / VkSwapchainKHR exists in the game process (a directly loaded Turnip has no WSI,
// and the process has no Surface). The presenter's interface is unchanged (acquire -> render -> present);
// behind it:
//  - the buffers are allocated here (R8G8B8A8_UNORM, GPU_COLOR_OUTPUT | GPU_SAMPLED_IMAGE |
//    COMPOSER_OVERLAY), imported with VK_ANDROID_external_memory_android_hardware_buffer, and sent
//    once to the app (bb_present_client.h);
//  - AcquireNextImage takes a free buffer (waiting for the app's RELEASE when all are out) and turns
//    its release fence into a semaphore the presenter's submission waits on;
//  - Present exports the present-ready semaphore as a sync file (VK_KHR_external_semaphore_fd) and sends
//    it with the frame: the app's acquire fence.
// At most image_count - 1 frames are in the app's hands.
#pragma once

#ifdef __ANDROID__
#include <deque>
#include <vector>
#include "bb_present_client.h"
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

struct AHardwareBuffer;

namespace Frontend {
class WindowSDL;
}

namespace Vulkan {

class Instance;

class Swapchain {
public:
    explicit Swapchain(const Instance& instance, const Frontend::WindowSDL& window);
    ~Swapchain();

    /// Creates (or recreates) the buffers with a given size.
    void Create(u32 width, u32 height);
    void Recreate(u32 width, u32 height);

    /// Takes the next free buffer; false when the buffers must be recreated (0x0 size).
    bool AcquireNextImage();
    /// Hands the current buffer to the app.
    bool Present();

    vk::Image Image() const { return buffers[image_index].image; }
    vk::ImageView ImageView() const { return buffers[image_index].view; }
    vk::SurfaceFormatKHR GetSurfaceFormat() const { return surface_format; }
    bool IsPresentable() const { return !zero_extent && !buffers.empty(); }
    u32 GetWidth() const { return width; }
    u32 GetHeight() const { return height; }
    u32 GetImageCount() const { return image_count; }
    u32 GetFrameIndex() const { return image_index; }
    vk::Extent2D GetExtent() const { return extent; }

    /// The semaphore the frame's submission waits on; null when the buffer needs no wait.
    [[nodiscard]] vk::Semaphore GetImageAcquiredSemaphore() const {
        return acquire_imported ? image_acquired[image_index] : vk::Semaphore{};
    }
    [[nodiscard]] vk::Semaphore GetPresentReadySemaphore() const {
        return present_ready[image_index];
    }

    // No HDR through AHardwareBuffers (RGBA8 only).
    bool HasHDR() const { return false; }
    void SetHDR(bool) {}
    bool GetHDR() const { return false; }

    /// The layout and queue-family ownership the image has when handed to the app.
    vk::ImageLayout PresentLayout() const { return vk::ImageLayout::eGeneral; }
    /// {src, dst} of the barrier that takes the image back from the app / gives it to the app.
    std::pair<u32, u32> AcquireQueues() const;
    std::pair<u32, u32> ReleaseQueues() const;
    /// The presenter must have the submission with the present-ready signal queued before Present
    /// (the sync file of a semaphore needs its signal submitted).
    static constexpr bool NeedsSubmittedBeforePresent() { return true; }

private:
    struct Buffer {
        AHardwareBuffer* ahb{};
        vk::Image image{};
        vk::DeviceMemory memory{};
        vk::ImageView view{};
        int release_fd{-1}; ///< the app's release fence of the last frame, until it is waited on
    };

    void Destroy();
    void CreateBuffer(u32 index);
    void HandleRelease(BbPresent::Release& release);
    void PeerGone() const;
    int ExportPresentReady();
    void ImportAcquire(u32 index);

private:
    const Instance& instance;
    const Frontend::WindowSDL& window;
    BbPresent::Client client;
    vk::SurfaceFormatKHR surface_format{vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear};
    vk::Extent2D extent{};
    std::vector<Buffer> buffers;
    std::vector<vk::Semaphore> image_acquired;
    std::vector<vk::Semaphore> present_ready;
    std::deque<u32> free_queue; ///< buffers the game may render into, in release order
    u32 width = 0;
    u32 height = 0;
    u32 image_count = 3;
    u32 image_index = 0;
    u32 generation = 0;
    u64 frame_counter = 0;
    bool acquire_imported = false;
    bool zero_extent = false;
    bool needs_recreation = true;
    bool sync_import_ok = true; ///< importing sync files as semaphores works (else poll them)
};

} // namespace Vulkan
#endif // __ANDROID__
