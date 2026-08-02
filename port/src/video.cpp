#include "video.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL2/SDL.h>

Video::~Video() {
    if (tex_) SDL_DestroyTexture((SDL_Texture*)tex_);
    if (ren_) SDL_DestroyRenderer((SDL_Renderer*)ren_);
    if (win_) SDL_DestroyWindow((SDL_Window*)win_);
    if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool Video::open(int w, int h, int depth_code) {
    if (w <= 0 || h <= 0) { w = 640; h = 480; }

    // Same mode already live — nothing to do (movies/menu re-open often).
    if (win_ && w_ == w && h_ == h) {
        depth_ = depth_code;
        return true;
    }

    if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "  [video] SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return false;
    }

    // Mode switch (640×480 intros → 800×600 menu): rebuild texture + host FB.
    // Leaving the old 640×480 pitch while the guest paints at w×2 causes the
    // classic CRT-streak / multi-tile tear the menu was showing.
    if (tex_) {
        SDL_DestroyTexture((SDL_Texture*)tex_);
        tex_ = nullptr;
    }

    if (!win_) {
        // THEOC_FULLSCREEN=1 → borderless fullscreen at the desktop resolution.
        // FULLSCREEN_DESKTOP (not exclusive FULLSCREEN) because we never want a
        // real display-mode switch: the guest paints 640×480 / 800×600 and
        // RenderSetLogicalSize scales it, so changing the panel's mode would buy
        // nothing and costs a jarring resync on every movie↔menu transition.
        static const bool want_fs = [] {
            const char* e = std::getenv("THEOC_FULLSCREEN");
            return e && *e && std::strcmp(e, "0") != 0;
        }();

        // The guest is 4:3 and modern panels are not, so the logical-size letterbox
        // leaves pillarbox bars — deliberate; stretching would distort the art.
        // Default sampling is nearest, which pairs with the integer scale factor
        // applied below: together they make every guest pixel an exact NxN block.
        // Note this matters even at an exact 2x (windowed on Retina) — bilinear
        // samples at ±0.25 of a texel there and still blends. set_crisp(false)
        // switches to linear per-texture for cutscenes.
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

        // ALLOW_HIGHDPI: without it macOS hands SDL the window's *point* size
        // (e.g. 1470×923 on a Retina panel), we render there, and the OS upscales
        // again to the physical pixels — two resamples and a soft image. With it,
        // the renderer's output is the real backing store (~2× those points), so
        // the guest's 800×600 is scaled once, straight to native.
        //
        // Applied to BOTH modes, unlike the first cut. This is a creation-time-only
        // flag — SDL_SetWindowFullscreen cannot add it later — so if the window were
        // built windowed-without-HiDPI, Alt+Enter would land in a *blurrier*
        // fullscreen than THEOC_FULLSCREEN=1 gives, which is a latent bug report.
        // Windowed gains a little sharpness on Retina from it too (it renders at the
        // backing store rather than at point size). THEOC_NO_HIDPI=1 reverts, same
        // escape-hatch convention as THEOC_LEGACY_SPRITE / THEOC_LEGACY_KEYMB.
        static const bool no_hidpi = [] {
            const char* e = std::getenv("THEOC_NO_HIDPI");
            return e && *e && std::strcmp(e, "0") != 0;
        }();

        Uint32 flags = SDL_WINDOW_SHOWN;
        if (!no_hidpi) flags |= SDL_WINDOW_ALLOW_HIGHDPI;
        if (want_fs)   flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_Window* win = SDL_CreateWindow(
            "Theocracy", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
        if (!win && want_fs) {
            // Never let a fullscreen failure cost us the game — fall back windowed.
            std::fprintf(stderr, "  [video] fullscreen failed (%s) — falling back to windowed\n",
                         SDL_GetError());
            win = SDL_CreateWindow("Theocracy", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, w, h,
                                   flags & ~(Uint32)SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        if (!win) {
            std::fprintf(stderr, "  [video] SDL_CreateWindow failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_Renderer* ren = SDL_CreateRenderer(win, -1, 0);
        if (!ren) {
            std::fprintf(stderr, "  [video] SDL_CreateRenderer failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(win);
            return false;
        }
        win_ = win;
        ren_ = ren;
        // Ask SDL what we actually got, rather than assuming the request stuck.
        fullscreen_ = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        SDL_ShowCursor(SDL_DISABLE);
    } else if (!fullscreen_) {
        SDL_SetWindowSize((SDL_Window*)win_, w, h);
    }
    // In fullscreen the window keeps the desktop size across a guest mode switch;
    // only the texture + logical size below change. Calling SDL_SetWindowSize here
    // would fight the fullscreen state instead of rescaling.

    SDL_RenderSetLogicalSize((SDL_Renderer*)ren_, w, h);
    SDL_Texture* tex = SDL_CreateTexture((SDL_Renderer*)ren_, SDL_PIXELFORMAT_RGB565,
                                         SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!tex) {
        std::fprintf(stderr, "  [video] SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    tex_ = tex;
    w_ = w;
    h_ = h;
    depth_ = depth_code;
    // Re-apply the presentation policy: the texture is new, and the renderer's
    // integer-scale setting has to be re-asserted against the new logical size.
    SDL_RenderSetIntegerScale((SDL_Renderer*)ren_, crisp_ ? SDL_TRUE : SDL_FALSE);
    SDL_SetTextureScaleMode((SDL_Texture*)tex_,
                            crisp_ ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    fb_.assign((size_t)w * (size_t)h, 0);
    log_geometry(depth_code);
    present();
    return true;
}

void Video::log_geometry(int depth_code) {
    if (!win_ || !ren_) return;
    // Report the actual letterbox so a "why are there bars" question is answerable
    // from the log alone, and so a wrong scale shows up here rather than only on
    // screen. Points vs pixels also makes the HiDPI state obvious: equal means we
    // are NOT on the backing store and the image is being resampled twice.
    int ow = 0, oh = 0, pw = 0, ph = 0;
    SDL_GetRendererOutputSize((SDL_Renderer*)ren_, &ow, &oh);  // pixels
    SDL_GetWindowSize((SDL_Window*)win_, &pw, &ph);            // points
    if (!fullscreen_) {
        std::fprintf(stderr, "  [video] window %dx%d (%dx%d px, hidpi %s, %s) depth-code %d"
                    " (RGB565 framebuffer)\n",
                    w_, h_, ow, oh, (ow > pw ? "on" : "off"),
                    crisp_ ? "crisp" : "smooth", depth_code);
        return;
    }
    double s = (ow && oh) ? std::min((double)ow / w_, (double)oh / h_) : 1.0;
    // Match SDL: integer scale floors the factor, so report what is actually drawn.
    if (crisp_) s = std::max(1.0, std::floor(s));
    int vw = (int)(w_ * s), vh = (int)(h_ * s);
    std::fprintf(stderr, "  [video] FULLSCREEN %dx%d px (%dx%d pt, hidpi %s), guest %dx%d"
                " scaled %.2fx %s -> %dx%d (pillarbox %d px, letterbox %d px) depth-code %d\n",
                ow, oh, pw, ph, (ow > pw ? "on" : "off"), w_, h_, s,
                crisp_ ? "crisp/integer+nearest" : "smooth/fit+linear", vw, vh,
                (ow - vw) / 2, (oh - vh) / 2, depth_code);
}

void Video::set_crisp(bool on) {
    if (on == crisp_) return;
    crisp_ = on;
    if (!ren_) return;
    SDL_RenderSetIntegerScale((SDL_Renderer*)ren_, on ? SDL_TRUE : SDL_FALSE);
    if (tex_)
        SDL_SetTextureScaleMode((SDL_Texture*)tex_,
                                on ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    log_geometry(depth_);
}

bool Video::toggle_fullscreen() {
    if (!win_ || !ren_) return false;
    bool want = !fullscreen_;
    if (SDL_SetWindowFullscreen((SDL_Window*)win_,
                                want ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        std::fprintf(stderr, "  [video] fullscreen toggle failed: %s\n", SDL_GetError());
        return false;
    }
    fullscreen_ = (SDL_GetWindowFlags((SDL_Window*)win_) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    if (!fullscreen_) {
        // Coming back from fullscreen the window keeps whatever size it had before,
        // which need not match the guest mode (movies are 640×480, the menu 800×600
        // and a toggle can happen in either). Restore it explicitly so windowed is
        // always 1:1 with the framebuffer.
        SDL_SetWindowSize((SDL_Window*)win_, w_, h_);
    }
    // The renderer's output size just changed, and the 4:3 letterbox is derived
    // from it — re-assert rather than trusting SDL to have recomputed.
    SDL_RenderSetLogicalSize((SDL_Renderer*)ren_, w_, h_);
    log_geometry(depth_);
    present();
    return true;
}

bool Video::save_bmp(const char* path) {
    if (fb_.empty()) return false;
    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        fb_.data(), w_, h_, 16, w_ * 2, 0xF800, 0x07E0, 0x001F, 0);
    if (!surf) return false;
    bool ok = SDL_SaveBMP(surf, path) == 0;
    SDL_FreeSurface(surf);
    return ok;
}

void Video::present() {
    if (!win_ || !tex_) return;
    // Pitch must match the guest LFB (w × 2 for RGB565). A stale smaller pitch
    // after a mode switch is what tears the menu background into streaks.
    SDL_UpdateTexture((SDL_Texture*)tex_, nullptr, fb_.data(), w_ * 2);
    SDL_RenderClear((SDL_Renderer*)ren_);
    SDL_RenderCopy((SDL_Renderer*)ren_, (SDL_Texture*)tex_, nullptr, nullptr);
    SDL_RenderPresent((SDL_Renderer*)ren_);
    pump();
}

void Video::pump() {
    if (!win_) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (event_hook_) event_hook_(e);
    }
}

void Video::keep_open_for(int seconds) {
    if (!win_ || seconds <= 0) return;
    for (int i = 0; i < seconds * 60; ++i) {
        present();
        SDL_Delay(16);
    }
}
