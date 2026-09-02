# Upscale filtering / "antialiasing"

Status: option 1 (sharp-bilinear) and option 2 (scanlines) added.

Prerequisite context: [guest-libmvos.md](guest-libmvos.md) G18 (fullscreen,
crisp-UI/smooth-video scaling policy), which is what this was built on.

## There is no true antialiasing available here

AA needs something this port does not have:

- No geometry to sample. libmvos is a software *blitter* — it pushes pre-made
  bitmaps into an RGB565 framebuffer (`cGD_LFB16`, plus our native overrides in
  `port/src/blit.cpp`). There are no edges to compute coverage for.
- No higher-resolution source art to downsample. The assets are pre-rendered at
  800×600 and the UI layout is hard-coded to that mode. You cannot supersample
  detail that was never authored.

So every option below is upscale filtering: making the pixels we already have
look less like hard squares.

The aged look is a CRT look. The art was authored for a display that blurred
adjacent pixels, bled phosphor, and laid scanlines over everything; we present
integer-scaled nearest, which is hard square pixels that never existed on the
original display. What is missing is the CRT, not antialiasing.

## Options, by effort

### 1. Sharp-bilinear — added

Render nearest into an intermediate 3× render target, then linear-blit that
target to the screen. Edges stay crisp but stop being hard blocks, and it avoids
the mush that made plain linear unacceptable — the whole reason G18 went
integer+nearest.

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

### 2. Scanline / CRT-lite — added

Darken every third output row (×0.7–0.8) and add a slight horizontal
bleed. Gets most of the way to "looks like it did in 2000", and is
much closer to the actual complaint than any AA technique. Added a taste
knob (env var) rather than a default — scanlines are polarising, and they
cost brightness.

### 3. hq3x / xBRZ pixel-art scalers — not done

Recorded so it does not get re-proposed. These are tuned for chunky low-res
sprite art (SNES-era, 256×224). Theocracy is 800×600 pre-rendered 3D, already
high enough resolution that these over-smooth it — they invent curves the source
art never had. The good implementations also carry copyleft licences worth
checking before use.

### 4. Real CRT shader — not done

Phosphor mask, curvature, bloom, proper scanline falloff. SDL2's render API has
**no shader support**, so this means moving the present path to raw OpenGL or
Metal. That drags the whole display layer off `SDL_Renderer`, which is a real
architectural change, unlike options 1 and 2.

## Videos

Cutscenes keep their current smooth/fractional path either way: they
are video, `MpegMovie::fit_frame` has already bilinear-resampled them, and none
of the pixel-preservation arguments apply.

---

## Implementation notes

Both cheap (1, and 2), in `port/src/video.cpp`. Three presentation paths now reach
the screen, and `[video]` names which one is running:

| Path | When | How |
|---|---|---|
| **sharp-bilinear** *(default)* | in-game (`crisp_`) | guest → 3× intermediate, nearest; intermediate → screen, linear, fractional 4:3 fit |
| **crisp** | `THEOC_LEGACY_SCALE=1`, or no render-target support | integer scale + nearest — the G18 behaviour |
| **smooth** | cutscenes (`set_crisp(false)`) | straight linear, unchanged |

`Video::rebuild_target` builds the intermediate alongside the streaming texture
whenever the guest mode changes, and `present()` became two `RenderCopy`s with a
`SDL_SetRenderTarget` between them.

`kSuperSample = 3` puts an 800×600 guest at 2400×1800, at or above every panel
we scale to, so the final blit is always a *downscale*, which is what stops
edges shimmering. Higher buys nothing once the intermediate exceeds the output.

The logical size is switched during pass 1. The renderer is set to the
intermediate's own size while it is bound, so the copy is 1:1 with it and the
scanline rows can be addressed in real pixels; at the guest logical size a "row"
would be three rows. It is restored before pass 2.

The integer floor is deliberately off on this path. The intermediate has already
done the pixel-exact part, so the final blit is free to fit fractionally, which
recovers the ~5% of image area the floor was throwing away (3.00× against 3.08×
on a 2940×1846 panel).

`THEOC_SCANLINES=N` darkens one row in every three *of the intermediate*,
i.e. one dark line per guest pixel row, whatever the window is doing. `N` is a
percentage, clamped to 90 so a typo cannot black out the screen; 25 is a light
hint, 60 is heavy. Off by default — scanlines are polarising and cost
brightness. The rows are precomputed into a `SDL_Rect` vector at mode change, so
the per-frame cost is one `SDL_RenderFillRects` rather than 600 draw calls.

Render targets are checked before use: `SDL_RENDERER_TARGETTEXTURE` is queried
and the intermediate's creation is checked, and either failing logs and falls
back to integer+nearest. Without that check the `RenderCopy` silently
draws nothing and the window goes black, which is a miserable thing to debug
from a screenshot.
