// Native SDL2 video backend — the host side of the libmvos VVC/GD boundary.
// The game (guest, under Unicorn) drives us through trapped cVVC/cGD methods;
// we own an RGB565 framebuffer and present it in an SDL window. This replaces
// the whole `libmvos_vvc_x.so` X11/MIT-SHM plugin (see
// docs/porting/vvc_x-backend.md): the plugin only *presented*, so an SDL
// texture-update + present is the entire contract.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <SDL2/SDL.h>

class Video {
public:
    ~Video();

    // Open a window + RGB565 framebuffer for the requested mode. depth_code is
    // the libmvos cVModeRequest code (5=16bpp RGB565 primary, 4=15bpp, 6/7=24).
    // We always keep the framebuffer 16-bit RGB565 and convert on present.
    bool open(int w, int h, int depth_code);

    // Push the framebuffer to the window and drain the SDL event queue (so the
    // OS keeps the window responsive).
    void present();
    void pump();

    // Keep the window on screen for `seconds`, presenting/pumping, so a human
    // (or a demo run) can see it. Returns early if the window is closed.
    void keep_open_for(int seconds);

    // Optional: called for every SDL_Event during pump() (input → guest).
    using EventHook = std::function<void(const SDL_Event&)>;
    void set_event_hook(EventHook h) { event_hook_ = std::move(h); }

    // Dump the current framebuffer to a BMP (debug: inspecting render bugs
    // like cursor trails, where you need to compare consecutive frames).
    bool save_bmp(const char* path);

    bool is_open() const { return win_ != nullptr; }
    int width() const { return w_; }
    int height() const { return h_; }
    int depth_code() const { return depth_; }
    // RGB565 framebuffer, w*h uint16_t, row-major. The cGD draw traps write here.
    uint16_t* fb() { return fb_.data(); }
    uint32_t fb_bytes() const { return (uint32_t)(fb_.size() * 2); }

private:
    void* win_ = nullptr;   // SDL_Window*
    void* ren_ = nullptr;   // SDL_Renderer*
    void* tex_ = nullptr;   // SDL_Texture*
    int w_ = 0, h_ = 0, depth_ = 0;
    std::vector<uint16_t> fb_;
    EventHook event_hook_;
};
