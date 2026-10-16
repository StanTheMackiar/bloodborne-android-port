// bbport: the game window. Created by the VideoOut driver on first open; the
// event pump runs on the port's window thread (see window.cpp).
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include "common/types.h"
#include "mouse_input.h"

#ifndef __ANDROID__
struct SDL_Window;
#endif

namespace Frontend {

enum class WindowSystemType : u8 { Headless, Windows, X11, Wayland, Metal, Android };

struct WindowSystemInfo {
    void* display_connection = nullptr;
    void* render_surface = nullptr;
    float render_surface_scale = 1.0f;
    WindowSystemType type = WindowSystemType::Headless;
};

#ifdef __ANDROID__
/// Bionic: there is no window and no SDL. The game process has no Surface: frames go to the app
/// through the AHB swapchain (vk_swapchain_ahb.cpp). This class only carries the output size
/// (BB_SCREEN_W x BB_SCREEN_H from the launcher, 1920x1080 without them); input comes from the app
/// through the runtime (src/runtime_pad.c), not from window events (window_android.cpp).
class WindowSDL {
public:
    WindowSDL(s32 width, s32 height, const char* title);
    s32 GetWidth() const { return width.load(std::memory_order_relaxed); }
    s32 GetHeight() const { return height.load(std::memory_order_relaxed); }
    WindowSystemInfo GetWindowInfo() const { return window_info; }
    bool IsOpen() const { return true; }
    /// Nothing to pump; returns true (the process ends with the app killing it).
    bool PollEvents();
    /// No keyboard dialog here: the text request is answered "cancelled" (state 2).
    void BeginTextInput(const std::string& initial, const std::string& prompt);
    int PollTextInput(std::string& text);
    BbMouseState GetMouseState() { return {}; }

private:
    std::atomic<s32> width, height;
    WindowSystemInfo window_info{};
};
#else
class WindowSDL {
public:
    WindowSDL(s32 width, s32 height, const char* title);
    ~WindowSDL();
    s32 GetWidth() const { return width.load(std::memory_order_relaxed); }
    s32 GetHeight() const { return height.load(std::memory_order_relaxed); }
    SDL_Window* GetSDLWindow() const { return window; }
    WindowSystemInfo GetWindowInfo() const { return window_info; }
    bool IsOpen() const { return is_open.load(std::memory_order_relaxed); }
    /// Processes pending window events. Returns false once the user closed the window.
    bool PollEvents();
    /// Keyboard text entry for the system IME dialog; typed text shows in the title bar.
    void BeginTextInput(const std::string& initial, const std::string& prompt);
    /// 0 while typing, 1 confirmed (Enter), 2 cancelled (Escape); text is UTF-8.
    int PollTextInput(std::string& text);
    BbMouseState GetMouseState();

private:
    std::atomic<s32> width, height;
    std::atomic<bool> is_open{true};
    std::mutex text_mutex;
    bool text_requested{}, text_active{};
    int text_state{};
    std::string text, text_prompt, base_title;
    void UpdateTextTitle();
    void UpdateCursor();
    void UpdatePointer();
    std::mutex mouse_mutex;
    MouseInput mouse_input;
    bool mouse_look{}, mouse_wanted{true}, mouse_relative{};
    u64 last_mouse_motion_ms{}; ///< SDL_GetTicks of the last mouse motion (UpdateCursor)
    bool cursor_hidden{};
    SDL_Window* window{};
    WindowSystemInfo window_info{};
};
#endif

} // namespace Frontend
