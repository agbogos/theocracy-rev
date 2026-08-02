#include "video.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL2/SDL.h>

// Sharp-bilinear presentation, and why it is not just "turn on linear".
//
// The art was authored for a CRT, which blurred adjacent pixels and laid
// scanlines over everything. We presented integer-scaled nearest, which is the
// opposite — perfectly square, perfectly hard pixels that never existed on the
// original display. Plain linear is not the fix either: it samples the 800x600
// source directly and turns the UI to mush, which is why G18 chose
// integer+nearest in the first place.
//
// Sharp-bilinear gets both. Blit the guest framebuffer nearest into an
// intermediate exactly kSuperSample times its size, then blit *that* to the
// screen with linear. The first step keeps every guest pixel an exact block; the
// second only ever softens the boundary between blocks, because it is
// downsampling an image already larger than the output. Edges stay crisp and
// stop being hard squares.
//
// Two things fall out of it:
//   - The final blit can fit *fractionally*, so it also wins back the ~5% of
//     image area that the integer-scale floor was costing (3.00x against 3.08x
//     on a 2940x1846 panel).
//   - Scanlines become nearly free and resolution-independent: darkening one row
//     in every kSuperSample *of the intermediate* is exactly one dark line per
//     guest pixel row, whatever the window is doing.
//
// Cutscenes keep the straight-linear path (crisp_ == false). They are video,
// MpegMovie::fit_frame has already resampled them, and none of the
// pixel-preservation argument applies.
//
// THEOC_LEGACY_SCALE=1 restores integer+nearest. Assessment and the two options
// deliberately rejected (hq3x/xBRZ, a real CRT shader):
// docs/porting/upscale-filtering.md.
Video::~Video() {
    if (rt_)  SDL_DestroyTexture((SDL_Texture*)rt_);
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
    if (rt_) {
        SDL_DestroyTexture((SDL_Texture*)rt_);
        rt_ = nullptr;
        sharp_ = false;
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
        // SDL2 enables text input at window creation, which switches on the
        // platform input method. On macOS that runs the dead-key composer:
        // Option+E/I/N/U (and Option+`) begin a diacritic, so the OS holds the
        // event waiting for the character to accent and no key-down is ever
        // delivered. Three of those are real in-game shortcuts — Alt+I, Alt+U
        // and Alt+N all reach the game's dispatcher on the original — and they
        // simply never arrived here, while Alt+A (plain 'å', not a dead key)
        // always worked. That difference is what named the cause.
        //
        // We never consume SDL_TEXTINPUT: every key is translated from the raw
        // scancode by sdl_scancode_to_ekey and pushed to the Intuition ring, so
        // the input method buys us nothing and costs five keys.
        SDL_StopTextInput();
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
    rebuild_target();
    // Re-apply the presentation policy: the texture is new, and the renderer's
    // integer-scale setting has to be re-asserted against the new logical size.
    // Sharp-bilinear wants the integer floor *off* — the intermediate has
    // already done the pixel-exact part, so the final blit is free to fit
    // fractionally and use the area the floor was throwing away.
    SDL_RenderSetIntegerScale((SDL_Renderer*)ren_,
                              (crisp_ && !sharp_) ? SDL_TRUE : SDL_FALSE);
    SDL_SetTextureScaleMode((SDL_Texture*)tex_,
                            crisp_ ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    fb_.assign((size_t)w * (size_t)h, 0);
    log_geometry(depth_code);
    present();
    return true;
}

void Video::rebuild_target() {
    sharp_ = false;
    scan_rects_.clear();
    if (!ren_ || w_ <= 0 || h_ <= 0) return;

    static const bool legacy = [] {
        const char* e = std::getenv("THEOC_LEGACY_SCALE");
        return e && *e && std::strcmp(e, "0") != 0;
    }();
    if (legacy) return;

    // 0 = off. The value is a percentage of darkening on the scanline row, so
    // 25 is a light CRT hint and 60 is heavy. Clamped, because the knob is a
    // taste control and a typo should not black out the screen.
    static const int scan_pct = [] {
        const char* e = std::getenv("THEOC_SCANLINES");
        if (!e || !*e) return 0;
        int v = std::atoi(e);
        return v < 0 ? 0 : (v > 90 ? 90 : v);
    }();
    scanline_a_ = scan_pct * 255 / 100;

    // Render targets are optional in SDL's renderer contract. Ask rather than
    // assume: without this the RenderCopy below silently draws nothing and the
    // window goes black, which is a miserable thing to debug.
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo((SDL_Renderer*)ren_, &info) != 0 ||
        !(info.flags & SDL_RENDERER_TARGETTEXTURE)) {
        std::fprintf(stderr, "  [video] no render-target support — "
                             "falling back to integer+nearest\n");
        return;
    }

    const int rw = w_ * kSuperSample, rh = h_ * kSuperSample;
    // ARGB8888 rather than the framebuffer's RGB565: 565 is frequently not a
    // valid *target* format, and the intermediate is never read back, so the
    // extra bytes cost nothing but memory we can spare.
    SDL_Texture* rt = SDL_CreateTexture((SDL_Renderer*)ren_, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_TARGET, rw, rh);
    if (!rt) {
        std::fprintf(stderr, "  [video] intermediate %dx%d failed (%s) — "
                             "falling back to integer+nearest\n", rw, rh, SDL_GetError());
        return;
    }
    // Linear on the way *out* is the whole point; nearest on the way in is set
    // on tex_ by the caller.
    SDL_SetTextureScaleMode(rt, SDL_ScaleModeLinear);
    rt_ = rt;
    sharp_ = true;

    if (scanline_a_ > 0) {
        // One darkened row per guest pixel row, precomputed so the per-frame
        // cost is a single SDL_RenderFillRects rather than h_ draw calls.
        scan_rects_.reserve((size_t)h_);
        for (int y = kSuperSample - 1; y < rh; y += kSuperSample)
            scan_rects_.push_back(SDL_Rect{0, y, rw, 1});
    }
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
    // Name the filter, not just crisp/smooth — three paths now reach the screen
    // and "why does it look like that" should be answerable from the log.
    char mode[64];
    if (!crisp_)      std::snprintf(mode, sizeof mode, "smooth (cutscene)");
    else if (sharp_)  std::snprintf(mode, sizeof mode, "sharp-bilinear %dx%s",
                                    kSuperSample, scanline_a_ ? " +scanlines" : "");
    else              std::snprintf(mode, sizeof mode, "crisp (integer+nearest)");

    if (!fullscreen_) {
        std::fprintf(stderr, "  [video] window %dx%d (%dx%d px, hidpi %s, %s) depth-code %d"
                    " (RGB565 framebuffer)\n",
                    w_, h_, ow, oh, (ow > pw ? "on" : "off"), mode, depth_code);
        return;
    }
    double s = (ow && oh) ? std::min((double)ow / w_, (double)oh / h_) : 1.0;
    // Match SDL: the integer floor applies only on the plain crisp path now —
    // sharp-bilinear deliberately fits fractionally, so reporting a floored
    // factor there would understate what is actually drawn.
    if (crisp_ && !sharp_) s = std::max(1.0, std::floor(s));
    int vw = (int)(w_ * s), vh = (int)(h_ * s);
    std::fprintf(stderr, "  [video] FULLSCREEN %dx%d px (%dx%d pt, hidpi %s), guest %dx%d"
                " scaled %.2fx %s -> %dx%d (pillarbox %d px, letterbox %d px) depth-code %d\n",
                ow, oh, pw, ph, (ow > pw ? "on" : "off"), w_, h_, s,
                mode, vw, vh, (ow - vw) / 2, (oh - vh) / 2, depth_code);
}

void Video::set_crisp(bool on) {
    if (on == crisp_) return;
    crisp_ = on;
    if (!ren_) return;
    // Under sharp-bilinear the integer floor stays off in both states: the game
    // path does not need it (the intermediate already made the pixels exact) and
    // the cutscene path never wanted it.
    SDL_RenderSetIntegerScale((SDL_Renderer*)ren_,
                              (on && !sharp_) ? SDL_TRUE : SDL_FALSE);
    // tex_ keys off crisp_ alone: when the sharp path is running, crisp_ is true
    // and pass 1 wants nearest; when it is not, this texture goes straight to
    // the screen and the cutscene path wants linear.
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
    SDL_Renderer* ren = (SDL_Renderer*)ren_;

    if (sharp_ && crisp_) {
        // Pass 1 — guest -> intermediate, nearest, exactly kSuperSample x.
        // Logical size is switched to the intermediate's own size so the copy is
        // 1:1 with it and the scanline rows below can be addressed in real
        // pixels; at the guest logical size a "row" would be kSuperSample rows.
        SDL_SetRenderTarget(ren, (SDL_Texture*)rt_);
        SDL_RenderSetLogicalSize(ren, w_ * kSuperSample, h_ * kSuperSample);
        SDL_RenderCopy(ren, (SDL_Texture*)tex_, nullptr, nullptr);
        if (!scan_rects_.empty()) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)scanline_a_);
            SDL_RenderFillRects(ren, scan_rects_.data(), (int)scan_rects_.size());
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }
        // Pass 2 — intermediate -> screen, linear, fractional 4:3 fit.
        SDL_SetRenderTarget(ren, nullptr);
        SDL_RenderSetLogicalSize(ren, w_, h_);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, (SDL_Texture*)rt_, nullptr, nullptr);
    } else {
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, (SDL_Texture*)tex_, nullptr, nullptr);
    }
    SDL_RenderPresent(ren);
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
