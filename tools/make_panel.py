#!/usr/bin/env python3
"""Writes Wander's screen artwork, panel.svg.fragment, and its indices, panel.h:
an orrery in greys. python tools/make_panel.py"""
import math, pathlib, random

ROOT = pathlib.Path(__file__).resolve().parents[1]
P = "hwander"
FONT = 'font-family="UniversNextPro-Light"'
CX, CY = 240, 130
RINGS = [(68, 30), (106, 45), (144, 60), (182, 75)]       # rx, ry per octave
out = [f'<g id="{P}_panel">']
nodes = {}
def node(name):
    nodes[name] = len(nodes)
    return f'{P}_node_{nodes[name]}'
def f(v): return f"{v:.4f}".rstrip("0").rstrip(".")
def position(ring, semitone):
    a = -math.pi / 2 + semitone * 2 * math.pi / 12
    rx, ry = RINGS[ring]
    return CX + rx * math.cos(a), CY + ry * math.sin(a)

# Stars first, behind everything (static).
r = random.Random(3)
for _ in range(70):
    out.append(f'<circle cx="{f(r.uniform(2, 478))}" cy="{f(r.uniform(42, 218))}" r="{r.choice([0.5, 0.6, 0.8])}" '
               f'fill="{r.choice(["#2E2E2E", "#5A5A5A", "#8C8C8C"])}" stroke="none" stroke-width="0"/>')

# --- The two-page header: eight bar nodes, text slots 0..15 --------------------
out.append('<!-- SDK two-page header: keep this block before machine artwork below y=38. -->')
out.append('<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>')
greys = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i in range(8):
    out.append(f'<path id="{node(f"bar{i}")}" transform="matrix(1 0 0 1 {60 * i} 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="{greys[i]}" stroke="{greys[i]}" stroke-width="0.5"/>')
texts = []
def text(x, y, fill, size):
    i = len(texts); texts.append(i)
    out.append(f'<text id="{P}_text_{i}" transform="matrix(1 0 0 1 {f(x)} {f(y)})" fill="{fill}" {FONT} font-size="{size}px">-</text>')
for col in range(4): text(5 + 120 * col, 10, "#CBCBCB" if col < 2 else "#000000", 10)
for col in range(4): text(65 + 120 * col, 17.4, "#CBCBCB" if col < 2 else "#000000", 20)
for col in range(4): text(5 + 120 * col, 33, "#5A5A5A", 10)
for col in range(4): text(65 + 120 * col, 33, "#707070", 13)

# --- The orrery -------------------------------------------------------------------
# Rings: one per octave of Spread, shown or hidden.
for ring, (rx, ry) in enumerate(RINGS):
    pts = []
    for k in range(72):
        a = 2 * math.pi * k / 72
        pts.append((rx * math.cos(a), ry * math.sin(a)))
    d = "M" + " L".join(f"{f(x)},{f(y)}" for x, y in pts) + " Z"
    out.append(f'<path id="{node(f"ring{ring}")}" transform="matrix(1 0 0 1 {CX} {CY})" d="{d}" fill="none" stroke="#5A5A5A" stroke-width="0.6"/>')
# The sun: the key you hold, with a halo that pulses as notes sound.
out.append(f'<circle cx="{CX}" cy="{CY}" r="22" fill="none" stroke="#2E2E2E" stroke-width="0.8"/>')
out.append(f'<circle id="{node("halo")}" transform="matrix(1 0 0 1 {CX} {CY})" cx="0" cy="0" r="17" fill="none" stroke="#8C8C8C" stroke-width="1"/>')
out.append(f'<circle id="{node("sun")}" transform="matrix(1 0 0 1 {CX} {CY})" cx="0" cy="0" r="11" fill="#E6E6E6" stroke="#E6E6E6" stroke-width="0.5"/>')
# The twelve places on each ring; the scale's notes light up.
for ring in range(4):
    for s in range(12):
        x, y = position(ring, s)
        out.append(f'<circle id="{node(f"dot{ring}_{s}")}" transform="matrix(1 0 0 1 {f(x)} {f(y)})" cx="0" cy="0" r="3.2" fill="#000000" stroke="#B4B4B4" stroke-width="1"/>')
# Echo ripples, the marble's trail, then the marble.
for k in range(3):
    out.append(f'<circle id="{node(f"ripple{k}")}" transform="matrix(1 0 0 1 {CX} {CY})" cx="0" cy="0" r="6" fill="none" stroke="#8C8C8C" stroke-width="0.6"/>')
for k in range(4):
    out.append(f'<circle id="{node(f"trail{k}")}" transform="matrix(1 0 0 1 {CX} {CY})" cx="0" cy="0" r="3.5" fill="#8C8C8C" stroke="#8C8C8C" stroke-width="0.4"/>')
out.append(f'<circle id="{node("marble")}" transform="matrix(1 0 0 1 {CX} {CY})" cx="0" cy="0" r="7" fill="#FFFFFF" stroke="#FFFFFF" stroke-width="0.5"/>')

palette = [("black", "#000000", "#000000", 0.6), ("lit", "#000000", "#E6E6E6", 1.0), ("dim", "#000000", "#2E2E2E", 0.6),
           ("ring", "#000000", "#5A5A5A", 0.6)] + \
          [(f"grey{i}", c, c, 0.6) for i, c in enumerate(["#E6E6E6", "#B4B4B4", "#8C8C8C", "#5A5A5A", "#3A3A3A"])]
for i, (_, fill, stroke, width) in enumerate(palette):
    out.append(f'<circle id="{P}_palette_{i}" cx="640" cy="0" r="1" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>')
out.append('</g>')
(ROOT / "panel.svg.fragment").write_text("\n".join(out) + "\n")

h = ["#pragma once", "// Written by tools/make_panel.py: the artwork's node indices and ring positions.",
     "#include <opxy/types.hpp>", "namespace hwander { namespace panel {"]
for name in ("bar0", "ring0", "halo", "sun", "dot0_0", "ripple0", "trail0", "marble"):
    h.append(f"const unsigned {name} = {nodes[name]};")
h.append(f"const unsigned node_count = {len(nodes)}, text_count = {len(texts)}, palette_count = {len(palette)};")
for i, (name, *_ ) in enumerate(palette): h.append(f"const unsigned shade_{name} = {i};")
h.append(f"const opxy::i32 centre_x = {CX}, centre_y = {CY};")
xs = [[round(position(rg, s)[0] * 1024) for s in range(12)] for rg in range(4)]
ys = [[round(position(rg, s)[1] * 1024) for s in range(12)] for rg in range(4)]
h.append("// Each ring's twelve places, Q10 pixels.")
h.append("const opxy::i32 place_x[4][12] = {" + ",".join("{" + ",".join(map(str, row)) + "}" for row in xs) + "};")
h.append("const opxy::i32 place_y[4][12] = {" + ",".join("{" + ",".join(map(str, row)) + "}" for row in ys) + "};")
h.append("}}")
(ROOT / "panel.h").write_text("\n".join(h) + "\n")
print(len(nodes), "nodes", len(texts), "texts", len(palette), "palette")
