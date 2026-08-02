#!/usr/bin/env python3
"""Chart the [health] samples from a THEOC_LONGRUN session log.

A multi-hour session produces one three-line `[health]` block per interval
(see `docs/porting/diagnostics.md`). Read as text they all look alike; plotted
against time the trends separate — which matters because the question a long
session exists to answer ("is anything growing without bound?") is a question
about slope, not about any one sample.

Stdlib only, like the rest of `tools/` — it writes a self-contained SVG rather
than depending on matplotlib. Open the SVG in a browser or Preview.

Usage:
    python3 tools/plot_health.py session.log             # -> session-health.svg
    python3 tools/plot_health.py session.log -o out.svg
    python3 tools/plot_health.py session.log --table     # text table, no SVG
"""

import argparse
import re
import sys
from pathlib import Path

# Field-driven rather than line-shaped, so this reads both the current [health]
# block and the original three-line one (which reported growth off the frontier
# — see the harness comment in port/src/traps.cpp for why that was wrong).
HEAD = re.compile(r"^\[health\] (?:(\d\d:\d\d:\d\d) \| )?up ([\d.]+)h \| "
                  r"([\d.]+) fps \(cap (\d+)ms\) \| frames (\d+)"
                  # Guest work per interval — absent from logs written before it
                  # was added, hence optional.
                  r"(?: \| ([\d.]+)M blk/s \(([\d.]+)M/frame\))?")
N = r"([+-]?[\d.]+)"
FIELDS = [
    (re.compile(rf"heap live {N} MB"),                       ["heap_live"]),
    (re.compile(rf"heap {N} MB live / {N} MB frontier"),     ["heap_live", "heap_frontier"]),
    (re.compile(rf"frontier {N} MB of {N} MB arena"),        ["heap_frontier", "arena"]),
    (re.compile(rf"grew {N} MB total \(avg {N} MB/h\)"),     ["grew_total", "avg_mbh"]),
    (re.compile(rf"interval {N} MB/h, {N} MB/1k frames"),    ["interval_mbh", "mb_per_1k"]),
    # Headroom reads "n/a (warm-up)" until the scenario load stops dominating
    # the since-start frontier rate, so it is matched separately from the rate.
    (re.compile(rf"\({N} MB/h -> "),                         ["front_mbh"]),
    (re.compile(rf"-> {N} h headroom\)"),                    ["headroom_h"]),
    (re.compile(rf"rss {N} MB \({N} since start\)"),         ["rss", "rss_delta"]),
    (re.compile(r"esp (0x[0-9a-f]+)"),                       ["esp"]),
    (re.compile(r"stubs (\d+) B \| fds (\d+)"),              ["stubs", "fds"]),
    (re.compile(rf"audio q={N}s underrun-frames (\d+)"),     ["audio_q", "underrun"]),
    (re.compile(r"logs suppressed (\d+)"),                   ["suppressed"]),
]
INT_KEYS = {"stubs", "fds", "underrun", "suppressed", "frames", "cap_ms"}

# A movie starting is the one session event with a visible resource signature
# (SMPEG decodes the whole file up front), so it is worth marking on the axes.
MOVIE = re.compile(r"^\[swscaler .*colorspace conversion")
# Alt+M markers. The operator puts these where the activity changed, which is
# the only thing in the log that knows what the session was actually doing.
# The frame count is what segment boundaries are cut on: `up 0.01h` is a 36 s
# quantum, so an hours-based boundary silently swallowed up to two samples
# before the marker — on trial 6 (2026-08-01) that pulled the +21.9 MB save load
# into the first segment and turned a +6.3 MB result into +30.7.
MARK = re.compile(r"^\[mark\] #(\d+) \S+ \| up [\d.]+h \| frames (\d+)")

# Reference categorical palette, slots 1/2/6/7 — the ones that clear the
# light-surface contrast floor. Dark steps are the same hues re-stepped.
SERIES = {
    "live":     ("#2a78d6", "#3987e5"),   # slot 1 blue
    "frontier": ("#eb6834", "#d95926"),   # slot 2 orange
    "fps":      ("#008300", "#008300"),   # slot 6 green
    "rss":      ("#4a3aa7", "#9085e9"),   # slot 7 violet
    "underrun": ("#e34948", "#e66767"),   # slot 8 red
    "blocks":   ("#0f7d7d", "#2fa8a8"),   # slot 4 teal, re-stepped for contrast
}


def parse(path):
    """-> (rows, movie_times, marks). Each row is one [health] block, flattened."""
    rows, movies, marks, cur, pending_mark = [], [], [], None, None

    def flush():
        # A block needs at least the two series every panel depends on.
        if cur and "heap_live" in cur and "rss" in cur:
            cur.setdefault("heap_frontier", cur["heap_live"])
            rows.append(cur)

    for line in Path(path).read_text(errors="replace").splitlines():
        m = HEAD.match(line)
        if m:
            flush()
            cur = dict(clock=m[1], hours=float(m[2]), fps=float(m[3]),
                       cap_ms=int(m[4]), frames=int(m[5]))
            if m[6]:
                cur["blk_per_s"], cur["blk_per_frame"] = float(m[6]), float(m[7])
            # A marker forces the sample that follows it, so it belongs at that
            # sample's time, not at the one before it.
            if pending_mark is not None:
                marks.append(dict(seq=pending_mark[0], frames=pending_mark[1],
                                  hours=cur["hours"]))
                pending_mark = None
            continue
        mk = MARK.match(line)
        if mk:
            flush()
            cur = None
            pending_mark = (int(mk[1]), int(mk[2]))
            continue
        if cur is not None and line.startswith(" "):
            for rx, keys in FIELDS:
                g = rx.search(line)
                if not g:
                    continue
                for i, k in enumerate(keys, 1):
                    v = g[i]
                    cur[k] = v if k == "esp" else (int(v) if k in INT_KEYS else float(v))
            continue
        flush()
        cur = None
        if MOVIE.match(line) and rows:
            # No timestamp on the line itself; place it at the last sample.
            movies.append(rows[-1]["hours"])
    flush()
    return rows, movies, marks


def reloads(rows, drop_mb=5.0):
    """Samples where the live set fell sharply — a teardown or a save reload.

    Worth calling out separately: a drop is the one thing a leak cannot do, so
    these are the points that partition a session into comparable stretches.
    """
    out = []
    for a, b in zip(rows, rows[1:]):
        if a["heap_live"] - b["heap_live"] >= drop_mb:
            out.append((b["hours"], a["heap_live"] - b["heap_live"]))
    return out


def segments(rows, marks):
    """The stretch after each Alt+M marker, up to the next one.

    This is what the marker exists for. A controlled trial is one segment, and
    the two figures that make trials comparable are its fitted slope and its
    growth per 1k frames — per-hour rates are not comparable across trials run
    at different frame caps, because the engine is frame-tied.

    Returns (kept, dropped). A segment too short to carry a rate is dropped —
    but *reported*, because on the operator's side a dropped row and a segment
    that measured nothing look identical, and the two call for opposite
    responses: re-run it longer, versus believe the null. That confusion is the
    same one defect #7 caused from the other direction, when a zero-duration
    segment printed a row of zeroes that read like a measured null.
    """
    if not marks:
        return [], []
    # Cut on frames, not hours — see the MARK comment.
    edges = [m["frames"] for m in marks] + [float("inf")]
    out, dropped = [], []
    for m, f1 in zip(marks, edges[1:]):
        h0, seq, f0 = m["hours"], m["seq"], m["frames"]
        seg = [r for r in rows if f0 <= r["frames"] < f1]
        secs = (seg[-1]["hours"] - seg[0]["hours"]) * 3600 if seg else 0.0
        if len(seg) < 3:      # two samples fit a line through noise
            dropped.append((seq, h0, len(seg), secs, "under 3 samples"))
            continue
        # A double-tapped marker produces two marks in the same second, and the
        # forced samples between them look like a segment with three samples and
        # no duration. Anything under 30 s cannot carry a rate; drop it rather
        # than print a row of zeroes that reads like a measured null.
        if secs < 30:
            dropped.append((seq, h0, len(seg), secs, "under 30 s"))
            continue
        mbh, _ = fit(seg, "heap_live", 0.0)
        fps = sum(r["fps"] for r in seg) / len(seg)
        # A fitted slope cannot tell "leaks steadily" from "flat, then one step"
        # — and one step at a segment boundary drags the fit hard. Trial 2
        # (2026-08-01) was 38 identical samples plus a single +1.08 MB step as
        # the trial ended, which the slope alone reported as +0.88 MB/h of
        # province idle. So count the moves and show the largest.
        deltas = [b["heap_live"] - a["heap_live"] for a, b in zip(seg, seg[1:])]
        moves = [d for d in deltas if abs(d) >= 0.05]
        out.append(dict(
            seq=seq, t0=h0, mins=(seg[-1]["hours"] - seg[0]["hours"]) * 60,
            n=len(seg), fps=fps, mbh=mbh,
            moves=len(moves),
            big=max(deltas, key=abs, default=0.0),
            mb1k=mbh * 1000 / (fps * 3600) if fps else 0.0,
            net=seg[-1]["heap_live"] - seg[0]["heap_live"],
            blk=(sum(r["blk_per_frame"] for r in seg) / len(seg)
                 if all("blk_per_frame" in r for r in seg) else None)))
    return out, dropped


def fit(rows, key, since):
    """Least-squares slope of `key` against hours, over samples past `since`."""
    pts = [(r["hours"], r[key]) for r in rows if r["hours"] >= since]
    n = len(pts)
    if n < 3:
        return 0.0, 0.0
    sx = sum(x for x, _ in pts)
    sy = sum(y for _, y in pts)
    sxy = sum(x * y for x, y in pts)
    sxx = sum(x * x for x, y in pts)
    denom = n * sxx - sx * sx
    if denom == 0:
        return 0.0, 0.0
    slope = (n * sxy - sx * sy) / denom
    return slope, (sy - slope * sx) / n


# ---- SVG -------------------------------------------------------------------

W, PAD_L, PAD_R = 1020, 72, 118
PANEL_H, PANEL_GAP, TOP = 132, 46, 134
LEGEND_Y = 74  # below the subtitle, above the first panel title


def esc(s):
    return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def nice_ticks(lo, hi, want=4):
    """A short list of round values spanning [lo, hi]."""
    if hi <= lo:
        return [lo]
    raw = (hi - lo) / want
    mag = 10 ** (len(str(int(abs(raw)))) - 1) if abs(raw) >= 1 else 0.1
    step = next((m * mag for m in (1, 2, 2.5, 5, 10) if m * mag >= raw), 10 * mag)
    t, out = (int(lo / step)) * step, []
    while t <= hi + step * 0.001:
        if t >= lo - step * 0.001:
            out.append(round(t, 4))
        t += step
    return out or [lo, hi]


def panel(rows, movies, marks, idx, title, note, series, x_of, fmt="{:.0f}"):
    """One stacked panel: shared x, own y. `series` is [(key, label, role)]."""
    top = TOP + idx * (PANEL_H + PANEL_GAP)
    bot = top + PANEL_H
    vals = [r[k] for k, _, _ in series for r in rows]
    lo, hi = min(vals), max(vals)
    if hi == lo:
        hi = lo + 1
    span = hi - lo
    lo, hi = lo - span * 0.12, hi + span * 0.18
    y_of = lambda v: bot - (v - lo) / (hi - lo) * PANEL_H

    o = [f'<text class="ptitle" x="{PAD_L}" y="{top - 30}">{esc(title)}</text>']
    if note:
        o.append(f'<text class="pnote" x="{PAD_L}" y="{top - 12}">{esc(note)}</text>')

    for t in nice_ticks(lo + span * 0.12, hi - span * 0.18):
        y = y_of(t)
        if not (top - 2 <= y <= bot + 2):
            continue
        o.append(f'<line class="grid" x1="{PAD_L}" y1="{y:.1f}" x2="{W - PAD_R}" y2="{y:.1f}"/>')
        o.append(f'<text class="tick" x="{PAD_L - 10}" y="{y + 4:.1f}" text-anchor="end">{fmt.format(t)}</text>')

    for h in movies:  # recessive event rules, behind the data
        o.append(f'<line class="movie" x1="{x_of(h):.1f}" y1="{top}" x2="{x_of(h):.1f}" y2="{bot}"/>')

    # Markers are the operator's own partition of the session, so they read
    # stronger than a movie rule — but still behind the series. Numbered on the
    # top panel only; repeating the label four times is noise.
    for mk in marks:
        x = x_of(mk["hours"])
        o.append(f'<line class="mark" x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{bot}"/>')
        if idx == 0:
            o.append(f'<text class="marklab" x="{x:.1f}" y="{top - 4}" '
                     f'text-anchor="middle">{mk["seq"]}</text>')

    for key, label, role in series:
        c = f"var(--{role})"
        pts = " ".join(f"{x_of(r['hours']):.1f},{y_of(r[key]):.1f}" for r in rows)
        o.append(f'<polyline class="ln" points="{pts}" stroke="{c}"/>')
        last = rows[-1]
        o.append(f'<circle cx="{x_of(last["hours"]):.1f}" cy="{y_of(last[key]):.1f}" r="4" '
                 f'fill="{c}" class="dot"/>')
        o.append(f'<text class="dlabel" x="{W - PAD_R + 12}" y="{y_of(last[key]) + 4:.1f}">'
                 f'{esc(label)} <tspan class="dval">{fmt.format(last[key])}</tspan></text>')
    return "\n".join(o), y_of


def render(rows, movies, marks, slope, since):
    span = rows[-1]["hours"] or 1.0
    # Not hardcoded 60: a trial is run at THEOC_LONGRUN=15. Averaged over the
    # whole span rather than taken per-sample, because uptime prints to 0.01h —
    # a 36s quantum, which makes every individual delta 36 or 72.
    interval_s = span * 3600 / max(1, len(rows) - 1)
    x_of = lambda h: PAD_L + (h / span) * (W - PAD_L - PAD_R)
    panels = [
        ("Guest heap", "live vs frontier (high-water) — MB", [
            ("heap_live", "live", "live"), ("heap_frontier", "frontier", "frontier")], "{:.0f}"),
        ("Host RSS", "resident set — MB", [("rss", "rss", "rss")], "{:.0f}"),
        ("Frame rate", "fps against the 50 ms cap", [("fps", "fps", "fps")], "{:.1f}"),
        ("Audio underruns", "frames per interval", [("underrun", "underruns", "underrun")], "{:.0f}"),
    ]
    # Guest work sits under the frame rate: together they say whether a slow
    # stretch was the guest doing more work or the host falling behind. Only
    # plotted if every sample has it — a log that predates the field would
    # otherwise draw a panel with holes in it.
    if all("blk_per_frame" in r for r in rows):
        panels.insert(3, ("Guest work", "million basic blocks per frame",
                          [("blk_per_frame", "blk/frame", "blocks")], "{:.2f}"))
    body, last_y = [], None
    for i, (title, note, series, fmt) in enumerate(panels):
        svg, y_of = panel(rows, movies, marks, i, title, note, series, x_of, fmt)
        body.append(svg)
        if i == 0:  # trend line on the headline panel only
            a, b = slope, fit(rows, "heap_live", since)[1]
            x1, x2 = since, rows[-1]["hours"]
            body.append(f'<line class="trend" x1="{x_of(x1):.1f}" y1="{y_of(a * x1 + b):.1f}" '
                        f'x2="{x_of(x2):.1f}" y2="{y_of(a * x2 + b):.1f}"/>')
            body.append(f'<text class="trendlab" x="{x_of(x1) + 10:.1f}" '
                        f'y="{y_of(a * x1 + b) - 28:.1f}">fit from {since:g}h: '
                        f'+{a:.2f} MB/h</text>')
        last_y = y_of

    h_total = TOP + len(panels) * (PANEL_H + PANEL_GAP) + 24
    bot = TOP + (len(panels) - 1) * (PANEL_H + PANEL_GAP) + PANEL_H
    axis = [f'<line class="axis" x1="{PAD_L}" y1="{bot}" x2="{W - PAD_R}" y2="{bot}"/>']
    for t in nice_ticks(0, span, 6):
        if t > span:
            continue
        axis.append(f'<text class="tick" x="{x_of(t):.1f}" y="{bot + 20}" text-anchor="middle">{t:g}h</text>')
    axis.append(f'<text class="pnote" x="{(PAD_L + W - PAD_R) / 2:.0f}" y="{bot + 40}" '
                f'text-anchor="middle">session time</text>')

    legend = (f'<g><rect x="{PAD_L}" y="{LEGEND_Y}" width="10" height="10" rx="2" fill="var(--live)"/>'
              f'<text class="leg" x="{PAD_L + 16}" y="{LEGEND_Y + 9}">heap live</text>'
              f'<rect x="{PAD_L + 92}" y="{LEGEND_Y}" width="10" height="10" rx="2" fill="var(--frontier)"/>'
              f'<text class="leg" x="{PAD_L + 108}" y="{LEGEND_Y + 9}">frontier</text>'
              f'<line x1="{PAD_L + 186}" y1="{LEGEND_Y + 5}" x2="{PAD_L + 206}" y2="{LEGEND_Y + 5}" class="movie"/>'
              f'<text class="leg" x="{PAD_L + 212}" y="{LEGEND_Y + 9}">movie start</text>'
              f'<line x1="{PAD_L + 290}" y1="{LEGEND_Y + 5}" x2="{PAD_L + 310}" y2="{LEGEND_Y + 5}" class="mark"/>'
              f'<text class="leg" x="{PAD_L + 316}" y="{LEGEND_Y + 9}">Alt+M marker</text></g>')

    css = """
  .bg{fill:var(--surface-1)}
  text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif}
  .title{font-size:19px;font-weight:600;fill:var(--text-primary)}
  .sub{font-size:12.5px;fill:var(--text-secondary)}
  .ptitle{font-size:14px;font-weight:600;fill:var(--text-primary)}
  .pnote,.leg{font-size:11.5px;fill:var(--text-secondary)}
  .tick{font-size:11px;fill:var(--text-muted)}
  .dlabel{font-size:11.5px;fill:var(--text-secondary)}
  .dval{font-weight:600;fill:var(--text-primary)}
  .grid{stroke:var(--grid);stroke-width:1}
  .axis{stroke:var(--grid);stroke-width:1.5}
  .ln{fill:none;stroke-width:2;stroke-linejoin:round;stroke-linecap:round}
  .dot{stroke:var(--surface-1);stroke-width:2}
  .movie{stroke:var(--text-muted);stroke-width:1;stroke-dasharray:2 4;opacity:.75}
  .mark{stroke:var(--text-secondary);stroke-width:1.25;opacity:.55}
  .marklab{font-size:10.5px;font-weight:600;fill:var(--text-secondary)}
  .trend{stroke:var(--text-secondary);stroke-width:1.5;stroke-dasharray:5 4;opacity:.9}
  .trendlab{font-size:11.5px;font-weight:600;fill:var(--text-secondary)}
"""
    light = ("--surface-1:#fcfcfb;--text-primary:#0b0b0b;--text-secondary:#52514e;"
             "--text-muted:#7a7973;--grid:#e4e3df;"
             + ";".join(f"--{k}:{v[0]}" for k, v in SERIES.items()))
    dark = ("--surface-1:#1a1a19;--text-primary:#ffffff;--text-secondary:#c3c2b7;"
            "--text-muted:#8d8c84;--grid:#333331;"
            + ";".join(f"--{k}:{v[1]}" for k, v in SERIES.items()))

    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{h_total}"
     viewBox="0 0 {W} {h_total}" role="img"
     aria-label="THEOC_LONGRUN session health over {span:.2f} hours">
<style>
  :root{{{light}}}
  @media (prefers-color-scheme: dark){{:root:where(:not([data-theme="light"])){{{dark}}}}}
  :root[data-theme="dark"]{{{dark}}}
{css}</style>
<rect class="bg" x="0" y="0" width="{W}" height="{h_total}"/>
<text class="title" x="{PAD_L}" y="34">Theocracy long-session health — {span:.2f} h, {len(rows)} samples</text>
<text class="sub" x="{PAD_L}" y="54">{len(rows)} × {interval_s:.0f} s [health] intervals · {rows[-1]['frames']:,} frames · cap {rows[-1]['cap_ms']} ms</text>
{legend}
{chr(10).join(body)}
{chr(10).join(axis)}
</svg>
"""


# ---- text ------------------------------------------------------------------

def table(rows, marks, slope, since, fslope):
    out = [f"{len(rows)} samples over {rows[-1]['hours']:.2f} h "
           f"({rows[-1]['frames']:,} frames, cap {rows[-1]['cap_ms']} ms)", ""]
    stamped = any(r.get("clock") for r in rows)
    blocked = all("blk_per_frame" in r for r in rows)
    at_mark = {m["frames"] for m in marks}
    hdr = (f"{'clock':>9} " if stamped else "") + \
          f"{'t/h':>6} {'fps':>6} " + (f"{'blk/f':>7} " if blocked else "") + \
          f"{'live':>8} {'front':>8} {'rss':>8} {'under':>8} {'esp':>12}"
    out += [hdr, "-" * len(hdr)]
    for r in rows:
        # A marked sample is called out in the table too — the segment summary
        # below is the point, but you still want to find the boundary by eye.
        tag = " <" if r["frames"] in at_mark else ""
        out.append((f"{r.get('clock') or '':>9} " if stamped else "") +
                   f"{r['hours']:6.2f} {r['fps']:6.1f} " +
                   (f"{r['blk_per_frame']:7.3f} " if blocked else "") +
                   f"{r['heap_live']:8.2f} "
                   f"{r['heap_frontier']:8.2f} {r['rss']:8.1f} {r['underrun']:8d} "
                   f"{r['esp']:>12}{tag}")
    last = rows[-1]
    # Headroom is set by the frontier, not by live: OOM is the bump frontier
    # running out of arena, and the frontier is what the live set drags upward.
    headroom = (128 - last["heap_frontier"]) / fslope if fslope > 0 else float("inf")
    out += ["",
            f"heap fit from {since:g}h: live {slope:+.3f} MB/h, frontier {fslope:+.3f} MB/h",
            f"frontier {last['heap_frontier']:.2f} MB of the 128 MB arena "
            f"-> {headroom:.1f} h of headroom at the frontier rate",
            f"stubs {min(r['stubs'] for r in rows)}-{max(r['stubs'] for r in rows)} B, "
            f"fds {min(r['fds'] for r in rows)}-{max(r['fds'] for r in rows)}, "
            f"suppressed {max(r['suppressed'] for r in rows)}",
            f"distinct esp: {', '.join(sorted({r['esp'] for r in rows}))}"]
    rl = reloads(rows)
    if rl:
        out.append("live-set drops (teardown / save reload): " +
                   ", ".join(f"{h:.2f}h -{d:.1f} MB" for h, d in rl))
    segs, dropped = segments(rows, marks)
    if dropped:
        out += ["", "dropped segments (too short to carry a rate — re-run these "
                    "longer; they are not nulls):"]
        for seq, t0, n, secs, why in dropped:
            out.append(f"  #{seq} at {t0:.2f}h: {n} sample(s), {secs:.0f} s — {why}")
    if segs:
        out += ["", "marked segments (Alt+M -> next marker):",
                f"  {'#':>3} {'from':>6} {'mins':>6} {'n':>4} {'fps':>6} " +
                (f"{'blk/f':>7} " if all(s["blk"] is not None for s in segs) else "") +
                f"{'net MB':>8} {'MB/h':>8} {'MB/1k fr':>9} {'moves':>6} {'max Δ':>7}"]
        for s in segs:
            out.append(f"  {s['seq']:>3} {s['t0']:6.2f} {s['mins']:6.1f} {s['n']:4d} "
                       f"{s['fps']:6.1f} " +
                       (f"{s['blk']:7.3f} " if s["blk"] is not None else "") +
                       f"{s['net']:+8.2f} {s['mbh']:+8.3f} {s['mb1k']:+9.4f} "
                       f"{s['moves']:6d} {s['big']:+7.2f}")
        out.append("  MB/1k frames is the cross-trial figure; MB/h only compares "
                   "trials at the same frame cap.")
        out.append("  moves = samples that changed live by >=0.05 MB. Few moves with a "
                   "big max Δ means steps, not a leak — read the column, not the slope.")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?", default="session.log")
    ap.add_argument("-o", "--out", help="SVG path (default: <log stem>-health.svg)")
    ap.add_argument("--table", action="store_true", help="print the table only")
    ap.add_argument("--since", type=float, default=0.35,
                    help="hours to skip before fitting the trend, past warm-up "
                         "(default 0.35 — the scenario load dominates before that)")
    a = ap.parse_args()

    rows, movies, marks = parse(a.log)
    if len(rows) < 2:
        sys.exit(f"{a.log}: found {len(rows)} [health] samples — run with THEOC_LONGRUN=60")

    slope, _ = fit(rows, "heap_live", a.since)
    fslope, _ = fit(rows, "heap_frontier", a.since)
    print(table(rows, marks, slope, a.since, fslope))
    if a.table:
        return
    out = Path(a.out or (Path(a.log).with_suffix("").name + "-health.svg"))
    out.write_text(render(rows, movies, marks, slope, a.since))
    print(f"\nwrote {out}")


if __name__ == "__main__":
    main()
