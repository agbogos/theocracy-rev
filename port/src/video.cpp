#include "video.hpp"
#include <cstdio>
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
        SDL_Window* win = SDL_CreateWindow(
            "Theocracy", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            w, h, SDL_WINDOW_SHOWN);
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
        SDL_ShowCursor(SDL_DISABLE);
    } else {
        SDL_SetWindowSize((SDL_Window*)win_, w, h);
    }

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
    fb_.assign((size_t)w * (size_t)h, 0);
    std::printf("  [video] window %dx%d depth-code %d (RGB565 framebuffer)\n",
                w, h, depth_code);
    present();
    return true;
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
