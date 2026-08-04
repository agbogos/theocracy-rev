# Upscale filtering / "antialiasing"

**Status: option 1 (sharp-bilinear) and option 2 (scanlines) landed
2026-08-02.** Assessment below written 2026-07-26 in answer to "how much effort
would antialiasing be? it was designed for CRT so it looks aged." The estimate
of ~1–2 hours held. What shipped is at the bottom.

Prerequisite context: [guest-libmvos.md](guest-libmvos.md) G18 (fullscreen,
crisp-UI/smooth-video scaling policy), which is what this would build on.

## There is no true antialiasing available here

Worth stating up front, because it changes what the question even is. AA needs
one of two things, and this port has neither:

- **Geometry to sample.** There is none. libmvos is a software *blitter* — it
  pushes pre-made bitmaps into an RGB565 framebuffer (`cGD_LFB16`, plus our
  native overrides in `port/src/blit.cpp`). There are no edges to compute
  coverage for.
- **Higher-resolution source art to downsample.** There is none. The assets are
  pre-rendered at 800×600 and the UI layout is hard-coded to that mode. You
  cannot supersample detail that was never authored.

So every option below is **upscale filtering**: making the pixels we already have
look less like hard squares. Framing it as AA would send us hunting for something
that cannot exist.

Also note *what* the aged look actually is. The art was authored for a CRT, which
blurred adjacent pixels, bled phosphor, and laid scanlines over everything. We
currently present integer-scaled nearest, which is the *opposite* — perfectly
square, perfectly hard pixels that never existed on the original display. The
complaint is less "no AA" than "no CRT".

## Options, by effort

### 1. Sharp-bilinear — ~1 hour. Start here.

Render nearest into an intermediate 3× render target, then linear-blit that
target to the screen. Edges stay crisp but stop being hard blocks. This is the
standard fix for exactly this complaint, and it avoids the mush that made plain
linear unacceptable (the whole reason G18 went integer+nearest).

Sketch, in `Video`:

```
guest tex (800×600, nearest) ──> SDL_TEXTUREACCESS_TARGET (2400×1800, nearest)
                                      └──> RenderCopy to screen (linear, fractional fit)
```

- ~25 lines, no new dependencies — `SDL_CreateTexture(..., TARGET, ...)`,
  `SDL_SetRenderTarget`, two `RenderCopy`s in `present()`.
- Recreate the intermediate whenever the guest mode changes, alongside the
  existing texture rebuild in `Video::open`.
- **Bonus:** the final blit can fill *fractionally*, so this also wins back the
  ~5% of image area that integer scaling currently costs (2400×1800 → 2461×1846).
- Interacts with the existing `set_crisp()` policy — decide whether cutscenes
  keep their current straight-linear path (probably yes; see below).

Best improvement-per-line by a wide margin. This alone would likely settle it.

### 2. Scanline / CRT-lite — under an hour, on top of 1.

Darken every third output row (×0.7–0.8) and optionally add a slight horizontal
bleed. ~20 lines. Gets most of the way to "looks like it did in 2000", and is
much closer to the actual complaint than any AA technique. Wants to be a taste
knob (env var) rather than a default — scanlines are polarising, and they cost
brightness.

### 3. hq3x / xBRZ pixel-art scalers — half a day to a day. Skip.

Recorded so it does not get re-proposed. These are tuned for chunky low-res
sprite art (SNES-era, 256×224). Theocracy is **800×600 pre-rendered 3D**, already
high enough resolution that these produce a smeary, over-smoothed cartoon look —
they invent curves the source art never had. Wrong tool for this art style. Also
the good implementations carry copyleft licences worth checking before use.

### 4. Real CRT shader — days. Genuinely modernisation-track.

Phosphor mask, curvature, bloom, proper scanline falloff. SDL2's render API has
**no shader support**, so this means moving the present path to raw OpenGL or
Metal. That drags the whole display layer off `SDL_Renderer`, which is a real
architectural change, unlike options 1 and 2.

## Recommendation — as made on 2026-07-26

Options 1 (+2 as a knob) are roughly **1–2 hours total** — not a track item,
despite it having been filed as one. Option 1 alone is probably enough. It does
not block anything and nothing blocks it.

Cutscenes probably keep their current smooth/fractional path either way: they are
video, `MpegMovie::fit_frame` has already bilinear-resampled them, and none of
the pixel-preservation arguments apply.

> **All three predictions held.** Both options shipped on 2026-08-02 inside the
> estimate; cutscenes did keep the smooth path, and a unified path was
> considered and not needed. See [What landed](#what-landed-2026-08-02).

---

## What landed (2026-08-02)

Both cheap options, in `port/src/video.cpp`. Three presentation paths now reach
the screen, and `[video]` names which one is running:

| Path | When | How |
|---|---|---|
| **sharp-bilinear** *(default)* | in-game (`crisp_`) | guest → 3× intermediate, nearest; intermediate → screen, linear, fractional 4:3 fit |
| **crisp** | `THEOC_LEGACY_SCALE=1`, or no render-target support | integer scale + nearest — the G18 behaviour |
| **smooth** | cutscenes (`set_crisp(false)`) | straight linear, unchanged |

`Video::rebuild_target` builds the intermediate alongside the streaming texture
whenever the guest mode changes, and `present()` became two `RenderCopy`s with a
`SDL_SetRenderTarget` between them.

**`kSuperSample = 3` is chosen, not arbitrary.** It puts an 800×600 guest at
2400×1800, at or above every panel we scale to, so the final blit is always a
*downscale* — which is what stops edges shimmering. Higher buys nothing once the
intermediate exceeds the output.

**Logical size is switched during pass 1.** The renderer is set to the
intermediate's own size while it is bound, so the copy is 1:1 with it and the
scanline rows can be addressed in real pixels; at the guest logical size a "row"
would be three rows. It is restored before pass 2.

**The integer floor is off on this path, deliberately.** The intermediate has
already done the pixel-exact part, so the final blit is free to fit fractionally
— which recovers the ~5% of image area the floor was throwing away (3.00× against
3.08× on a 2940×1846 panel), exactly as the assessment predicted.

**`THEOC_SCANLINES=N`** darkens one row in every three *of the intermediate*,
i.e. one dark line per guest pixel row, whatever the window is doing. `N` is a
percentage, clamped to 90 so a typo cannot black out the screen; 25 is a light
hint, 60 is heavy. Off by default — scanlines are polarising and cost
brightness. The rows are precomputed into a `SDL_Rect` vector at mode change, so
the per-frame cost is one `SDL_RenderFillRects` rather than 600 draw calls.

**Render targets are checked, not assumed.** `SDL_RENDERER_TARGETTEXTURE` is
queried and the intermediate's creation is checked; either failing logs and
falls back to integer+nearest. Without that check the `RenderCopy` silently
draws nothing and the window goes black, which is a miserable thing to debug
from a screenshot.

### How it was verified without a display

`theoc` needs a display and the copyrighted data, so the two-pass path was
exercised standalone against SDL's **dummy video driver** — which gives a
software renderer that does support render targets. A 4×4 framebuffer with one
red pixel, through the same sequence:

- **pass 1**: exactly 9 pixels set, 0 mismatched — the nearest 3×3 block is
  pixel-exact, which is the property the whole approach rests on.
- **pass 2**: the block lands dead centre at the geometrically correct position,
  spread from 150 px to 200 px by the linear filter — i.e. correct placement
  with soft edges, which *is* sharp-bilinear.

Worth keeping as a technique: a render change that could plausibly produce a
black screen can be geometry-tested headlessly, and the assertion to make is
about *pixel positions*, not about it "looking right".
