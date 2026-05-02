"""Lightweight academic-style SVG plotting (stdlib only).

Provides 4 helpers used by E2 / E3 / E5 / E6:
  bar(values, labels, path, ...)
  scatter(xs, ys, path, ...)
  cdf(samples_by_label, path, ...)
  heatmap(matrix, row_labels, col_labels, path, ...)
"""
from __future__ import annotations
import math
from pathlib import Path


def _esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


def _open(w, h, title=""):
    return [(
        f'<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
        f'viewBox="0 0 {w} {h}" font-family="DejaVu Serif, serif" '
        f'font-size="11">\n'
        f'<rect width="{w}" height="{h}" fill="white"/>\n'
        f'<title>{_esc(title)}</title>\n'
    )]


def _close(parts, path):
    parts.append("</svg>\n")
    Path(path).write_text("".join(parts))


def _ax(parts, x0, y0, w, h, vmin, vmax, ticks=5, ylabel=""):
    parts.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0+h}" '
                 f'stroke="black" stroke-width="0.8"/>')
    for i in range(ticks + 1):
        v = vmin + (vmax - vmin) * i / ticks
        y = y0 + h - h * i / ticks
        parts.append(f'<line x1="{x0-4}" y1="{y}" x2="{x0+w}" y2="{y}" '
                     f'stroke="#dddddd" stroke-width="0.4" stroke-dasharray="2,2"/>')
        parts.append(f'<text x="{x0-6}" y="{y+3}" text-anchor="end" '
                     f'font-size="9">{v:.3g}</text>')
    if ylabel:
        cy = y0 + h / 2
        parts.append(f'<text x="{x0-44}" y="{cy}" text-anchor="middle" '
                     f'transform="rotate(-90 {x0-44} {cy})" font-size="10">'
                     f'{_esc(ylabel)}</text>')


def bar(values, labels, path, *, title="", ylabel="value",
        colors=None, hlines=None, w=1100, h=460):
    parts = _open(w, h, title)
    if title:
        parts.append(f'<text x="{w/2}" y="22" text-anchor="middle" '
                     f'font-size="13" font-weight="bold">{_esc(title)}</text>')
    L, R, T, B = 70, 30, 40, 140
    pw, ph = w - L - R, h - T - B
    vmin, vmax = 0, max(values) * 1.1 if values else 1
    if any(v < 0 for v in values):
        vmin = min(values) * 1.1
    _ax(parts, L, T, pw, ph, vmin, vmax, 6, ylabel)
    n = max(len(values), 1)
    bw = pw / n
    for i, (v, lab) in enumerate(zip(values, labels)):
        x = L + i * bw
        h_b = (v - vmin) / (vmax - vmin) * ph
        y = T + ph - h_b
        col = (colors[i] if colors else "#4472c4")
        parts.append(f'<rect x="{x+1:.2f}" y="{y:.2f}" width="{bw-2:.2f}" '
                     f'height="{h_b:.2f}" fill="{col}" stroke="black" '
                     f'stroke-width="0.3"/>')
        tx, ty = x + bw / 2, T + ph + 4
        parts.append(f'<text x="{tx:.2f}" y="{ty:.2f}" text-anchor="end" '
                     f'font-size="8" transform="rotate(-70 {tx:.2f} {ty:.2f})">'
                     f'{_esc(lab)}</text>')
    for hl in hlines or []:
        v, color, label = hl
        if v < vmin or v > vmax:
            continue
        y = T + ph - (v - vmin) / (vmax - vmin) * ph
        parts.append(f'<line x1="{L}" y1="{y}" x2="{L+pw}" y2="{y}" '
                     f'stroke="{color}" stroke-width="0.7" stroke-dasharray="4,3"/>')
        parts.append(f'<text x="{L+pw-4}" y="{y-3}" text-anchor="end" '
                     f'font-size="9" fill="{color}">{_esc(label)}</text>')
    _close(parts, path)


def scatter(series, path, *, title="", xlabel="x", ylabel="y",
            w=900, h=500, diagonal=False):
    """series: list of (label, color, [(x,y), ...])."""
    parts = _open(w, h, title)
    if title:
        parts.append(f'<text x="{w/2}" y="22" text-anchor="middle" '
                     f'font-size="13" font-weight="bold">{_esc(title)}</text>')
    L, R, T, B = 70, 160, 40, 60
    pw, ph = w - L - R, h - T - B
    xs = [p[0] for _, _, pts in series for p in pts]
    ys = [p[1] for _, _, pts in series for p in pts]
    if not xs:
        _close(parts, path); return
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if xmax == xmin:
        xmax = xmin + 1
    if ymax == ymin:
        ymax = ymin + 1
    _ax(parts, L, T, pw, ph, ymin, ymax, 6, ylabel)
    parts.append(f'<line x1="{L}" y1="{T+ph}" x2="{L+pw}" y2="{T+ph}" '
                 f'stroke="black" stroke-width="0.8"/>')
    for i in range(6):
        v = xmin + (xmax - xmin) * i / 5
        x = L + pw * i / 5
        parts.append(f'<text x="{x}" y="{T+ph+14}" text-anchor="middle" '
                     f'font-size="9">{v:.3g}</text>')
    parts.append(f'<text x="{L+pw/2}" y="{T+ph+34}" text-anchor="middle" '
                 f'font-size="10">{_esc(xlabel)}</text>')
    if diagonal:
        v0 = max(xmin, ymin); v1 = min(xmax, ymax)
        x0 = L + (v0 - xmin) / (xmax - xmin) * pw
        y0 = T + ph - (v0 - ymin) / (ymax - ymin) * ph
        x1 = L + (v1 - xmin) / (xmax - xmin) * pw
        y1 = T + ph - (v1 - ymin) / (ymax - ymin) * ph
        parts.append(f'<line x1="{x0}" y1="{y0}" x2="{x1}" y2="{y1}" '
                     f'stroke="black" stroke-width="0.6" stroke-dasharray="3,3"/>')
    legy = T + 10
    for label, color, pts in series:
        for x, y in pts:
            cx = L + (x - xmin) / (xmax - xmin) * pw
            cy = T + ph - (y - ymin) / (ymax - ymin) * ph
            parts.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="2.5" '
                         f'fill="{color}" stroke="black" stroke-width="0.3" '
                         f'fill-opacity="0.85"/>')
        parts.append(f'<rect x="{L+pw+10}" y="{legy-9}" width="10" height="10" '
                     f'fill="{color}" stroke="black" stroke-width="0.3"/>')
        parts.append(f'<text x="{L+pw+24}" y="{legy}" font-size="9">'
                     f'{_esc(label)}</text>')
        legy += 13
    _close(parts, path)


def cdf(series, path, *, title="", xlabel="bound ratio",
        w=900, h=500):
    """series: list of (label, color, [values...])."""
    parts = _open(w, h, title)
    if title:
        parts.append(f'<text x="{w/2}" y="22" text-anchor="middle" '
                     f'font-size="13" font-weight="bold">{_esc(title)}</text>')
    L, R, T, B = 70, 180, 40, 60
    pw, ph = w - L - R, h - T - B
    all_vals = [v for _, _, vs in series for v in vs]
    if not all_vals:
        _close(parts, path); return
    xmin, xmax = min(all_vals), max(all_vals)
    if xmax == xmin: xmax = xmin + 1
    _ax(parts, L, T, pw, ph, 0.0, 1.0, 5, "CDF")
    parts.append(f'<line x1="{L}" y1="{T+ph}" x2="{L+pw}" y2="{T+ph}" '
                 f'stroke="black" stroke-width="0.8"/>')
    for i in range(6):
        v = xmin + (xmax - xmin) * i / 5
        x = L + pw * i / 5
        parts.append(f'<text x="{x}" y="{T+ph+14}" text-anchor="middle" '
                     f'font-size="9">{v:.3g}</text>')
    parts.append(f'<text x="{L+pw/2}" y="{T+ph+34}" text-anchor="middle" '
                 f'font-size="10">{_esc(xlabel)}</text>')
    legy = T + 10
    for label, color, vs in series:
        if not vs:
            continue
        s = sorted(vs)
        n = len(s)
        d = "M "
        for i, v in enumerate(s):
            x = L + (v - xmin) / (xmax - xmin) * pw
            y1 = T + ph - (i / n) * ph
            y2 = T + ph - ((i + 1) / n) * ph
            d += f"{x:.2f} {y1:.2f} L {x:.2f} {y2:.2f} "
        parts.append(f'<path d="{d}" fill="none" stroke="{color}" '
                     f'stroke-width="1.4"/>')
        parts.append(f'<rect x="{L+pw+10}" y="{legy-9}" width="10" '
                     f'height="10" fill="{color}" stroke="black" '
                     f'stroke-width="0.3"/>')
        parts.append(f'<text x="{L+pw+24}" y="{legy}" font-size="9">'
                     f'{_esc(label)}</text>')
        legy += 13
    _close(parts, path)


def heatmap(matrix, row_labels, col_labels, path, *,
            title="", vmin=None, vmax=None, cell_w=46, cell_h=12):
    R = len(matrix)
    C = len(matrix[0]) if R else 0
    L, RR, T, B = 150, 90, 40, 20
    w = L + cell_w * C + RR
    h = T + cell_h * R + B
    parts = _open(w, h, title)
    if title:
        parts.append(f'<text x="{w/2}" y="22" text-anchor="middle" '
                     f'font-size="13" font-weight="bold">{_esc(title)}</text>')
    flat = [v for row in matrix for v in row if v == v]
    if vmin is None: vmin = min(flat) if flat else 0
    if vmax is None: vmax = max(flat) if flat else 1
    if vmax == vmin: vmax = vmin + 1
    def color(v):
        if v != v:
            return "#eeeeee"
        t = max(0.0, min(1.0, (v - vmin) / (vmax - vmin)))
        if t < 0.5:
            tt = t / 0.5
            r = int(49 + (255 - 49) * tt); g = int(117 + (255 - 117) * tt); b = int(181 + (191 - 181) * tt)
        else:
            tt = (t - 0.5) / 0.5
            r = int(255 + (165 - 255) * tt); g = int(255 + (0 - 255) * tt); b = int(191 + (38 - 191) * tt)
        return f'rgb({r},{g},{b})'
    for j, c in enumerate(col_labels):
        x = L + j * cell_w + cell_w / 2
        parts.append(f'<text x="{x}" y="{T-6}" text-anchor="middle" '
                     f'font-size="9" transform="rotate(-30 {x} {T-6})">'
                     f'{_esc(c)}</text>')
    for i, lab in enumerate(row_labels):
        y = T + i * cell_h
        parts.append(f'<text x="{L-4}" y="{y+cell_h-3}" text-anchor="end" '
                     f'font-size="8">{_esc(lab)}</text>')
        for j in range(C):
            v = matrix[i][j]
            parts.append(f'<rect x="{L+j*cell_w}" y="{y}" '
                         f'width="{cell_w}" height="{cell_h}" '
                         f'fill="{color(v)}" stroke="white" '
                         f'stroke-width="0.4"/>')
    cbx = L + cell_w * C + 12
    cbw = 14
    cbh = cell_h * R
    steps = 64
    for k in range(steps):
        v = vmax - (vmax - vmin) * (k / (steps - 1))
        y = T + cbh * k / steps
        parts.append(f'<rect x="{cbx}" y="{y}" width="{cbw}" '
                     f'height="{cbh/steps + 0.5}" fill="{color(v)}" '
                     f'stroke="none"/>')
    parts.append(f'<rect x="{cbx}" y="{T}" width="{cbw}" height="{cbh}" '
                 f'fill="none" stroke="black" stroke-width="0.5"/>')
    for k in range(5):
        v = vmin + (vmax - vmin) * (1 - k / 4)
        y = T + cbh * k / 4
        parts.append(f'<text x="{cbx+cbw+3}" y="{y+3}" font-size="8">'
                     f'{v:.3g}</text>')
    _close(parts, path)
