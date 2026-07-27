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
                  r"([\d.]+) fps \(cap (\d+)ms\) \| frames (\d+)")
N = r"([+-]?[\d.]+)"
FIELDS = [
    (re.compile(rf"heap live {N} MB"),                       ["heap_live"]),
    (re.compile(rf"heap {N} MB live / {N} MB frontier"),     ["heap_live", "heap_frontier"]),
    (re.compile(rf"frontier {N} MB of {N} MB arena"),        ["heap_frontier", "arena"]),
    (re.compile(rf"grew {N} MB total \(avg {N} MB/h\)"),     ["grew_total", "avg_mbh"]),
    (re.compile(rf"interval {N} MB/h, {N} MB/1k frames"),    ["interval_mbh", "mb_per_1k"]),
    (re.compile(rf"\({N} MB/h -> {N} h headroom\)"),         ["front_mbh", "headroom_h"]),
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

# Reference categorical palette, slots 1/2/6/7 — the ones that clear the
# light-surface contrast floor. Dark steps are the same hues re-stepped.
SERIES = {
    "live":     ("#2a78d6", "#3987e5"),   # slot 1 blue
    "frontier": ("#eb6834", "#d95926"),   # slot 2 orange
    "fps":      ("#008300", "#008300"),   # slot 6 green
    "rss":      ("#4a3aa7", "#9085e9"),   # slot 7 violet
    "underrun": ("#e34948", "#e66767"),   # slot 8 red
}


def parse(path):
    """-> (rows, movie_times). Each row is one [health] block, flattened."""
    rows, movies, cur = [], [], None

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
    return rows, movies


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


def panel(rows, movies, idx, title, note, series, x_of, fmt="{:.0f}"):
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


def render(rows, movies, slope, since):
    span = rows[-1]["hours"] or 1.0
    x_of = lambda h: PAD_L + (h / span) * (W - PAD_L - PAD_R)
    panels = [
        ("Guest heap", "live vs frontier (high-water) — MB", [
            ("heap_live", "live", "live"), ("heap_frontier", "frontier", "frontier")], "{:.0f}"),
        ("Host RSS", "resident set — MB", [("rss", "rss", "rss")], "{:.0f}"),
        ("Frame rate", "fps against the 50 ms cap", [("fps", "fps", "fps")], "{:.1f}"),
        ("Audio underruns", "frames per interval", [("underrun", "underruns", "underrun")], "{:.0f}"),
    ]
    body, last_y = [], None
    for i, (title, note, series, fmt) in enumerate(panels):
        svg, y_of = panel(rows, movies, i, title, note, series, x_of, fmt)
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
              f'<text class="leg" x="{PAD_L + 212}" y="{LEGEND_Y + 9}">movie start</text></g>')

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
<text class="sub" x="{PAD_L}" y="54">{len(rows)} × 60 s [health] intervals · {rows[-1]['frames']:,} frames · cap {rows[-1]['cap_ms']} ms</text>
{legend}
{chr(10).join(body)}
{chr(10).join(axis)}
</svg>
"""


# ---- text ------------------------------------------------------------------

def table(rows, slope, since, fslope):
    out = [f"{len(rows)} samples over {rows[-1]['hours']:.2f} h "
           f"({rows[-1]['frames']:,} frames, cap {rows[-1]['cap_ms']} ms)", ""]
    stamped = any(r.get("clock") for r in rows)
    hdr = (f"{'clock':>9} " if stamped else "") + \
          f"{'t/h':>6} {'fps':>6} {'live':>8} {'front':>8} {'rss':>8} {'under':>8} {'esp':>12}"
    out += [hdr, "-" * len(hdr)]
    for r in rows:
        out.append((f"{r.get('clock') or '':>9} " if stamped else "") +
                   f"{r['hours']:6.2f} {r['fps']:6.1f} {r['heap_live']:8.2f} "
                   f"{r['heap_frontier']:8.2f} {r['rss']:8.1f} {r['underrun']:8d} {r['esp']:>12}")
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

    rows, movies = parse(a.log)
    if len(rows) < 2:
        sys.exit(f"{a.log}: found {len(rows)} [health] samples — run with THEOC_LONGRUN=60")

    slope, _ = fit(rows, "heap_live", a.since)
    fslope, _ = fit(rows, "heap_frontier", a.since)
    print(table(rows, slope, a.since, fslope))
    if a.table:
        return
    out = Path(a.out or (Path(a.log).with_suffix("").name + "-health.svg"))
    out.write_text(render(rows, movies, slope, a.since))
    print(f"\nwrote {out}")


if __name__ == "__main__":
    main()
