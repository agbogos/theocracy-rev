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
    //
    // THEOC_FULLSCREEN=1 opens borderless-fullscreen at the desktop resolution.
    // The framebuffer stays at the guest's mode (640×480 / 800×600) and SDL's
    // logical size scales it, preserving 4:3 with pillarbox bars. Everything
    // downstream — the cGD blit traps, save_bmp, and the mouse coordinates the
    // guest sees — stays in guest space, so nothing else needs to know.
    bool open(int w, int h, int depth_code);

    // Push the framebuffer to the window and drain the SDL event queue (so the
    // OS keeps the window responsive).
    void present();
    void pump();

    // Flip between borderless fullscreen and windowed, preserving the guest mode
    // (the framebuffer and logical size are untouched — only the presentation
    // scale changes). Returns false if SDL refused, leaving the mode as it was.
    bool toggle_fullscreen();

    // Presentation filtering. **Crisp (the default)** = integer scale factor +
    // nearest sampling, so every guest pixel becomes an exact NxN block and the UI
    // is pixel-perfect. **Smooth** = fractional fit + linear, which fills more of
    // the screen at the cost of resampling.
    //
    // Crisp is right for the game: at 800x600 the integer floor costs ~5% of the
    // image area (3.00x vs 3.08x on a 2940x1846 panel) and removes all blur.
    // Smooth is right for cutscenes: the movie mode is 640x480, where the floor is
    // 3.00x against a 3.85x fit — a 39% area loss — and the content is video that
    // our bilinear aspect-fit has already resampled, so pixel-exactness buys
    // nothing. No-op when already in the requested state.
    void set_crisp(bool on);
    bool is_crisp() const { return crisp_; }

    // Supersample factor for the sharp-bilinear intermediate. 3 puts an 800x600
    // guest at 2400x1800, which is at or above every panel we scale to, so the
    // final blit is always a *downscale* — that is what keeps edges from
    // shimmering. Higher buys nothing once the intermediate exceeds the output.
    static constexpr int kSuperSample = 3;

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
    bool is_fullscreen() const { return fullscreen_; }
    // Guest-space dimensions (the framebuffer / logical size), NOT the window's
    // pixel size — in fullscreen those differ. Callers that clamp mouse input
    // want these, since SDL hands us coordinates already mapped into this space.
    int width() const { return w_; }
    int height() const { return h_; }
    int depth_code() const { return depth_; }
    // RGB565 framebuffer, w*h uint16_t, row-major. The cGD draw traps write here.
    uint16_t* fb() { return fb_.data(); }
    uint32_t fb_bytes() const { return (uint32_t)(fb_.size() * 2); }

private:
    // Report the present geometry (px vs pt, scale, bar widths) for the current
    // mode. Shared by open() and toggle_fullscreen() so both paths log alike.
    void log_geometry(int depth_code);

    // Build/tear down the sharp-bilinear intermediate for the current mode.
    // Leaves sharp_ false if the renderer cannot do render targets, which is
    // the whole fallback: everything else keys off sharp_.
    void rebuild_target();

    void* win_ = nullptr;   // SDL_Window*
    void* ren_ = nullptr;   // SDL_Renderer*
    void* tex_ = nullptr;   // SDL_Texture*
    void* rt_ = nullptr;    // SDL_Texture*, the kSuperSample intermediate
    int w_ = 0, h_ = 0, depth_ = 0;
    bool fullscreen_ = false;   // what we actually got, per SDL_GetWindowFlags
    bool crisp_ = true;         // pixel-preserving policy (see set_crisp)
    bool sharp_ = false;        // sharp-bilinear active (crisp_ && rt_ built)
    int scanline_a_ = 0;        // THEOC_SCANLINES alpha, 0 = off
    std::vector<uint16_t> fb_;
    std::vector<SDL_Rect> scan_rects_;
    EventHook event_hook_;
};
