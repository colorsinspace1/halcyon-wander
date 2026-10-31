#!/usr/bin/env python3
"""Renders Strata's real frames, from tools/shot.cpp, onto its artwork for review.
screens.py <shot binary> <out.html>"""
import html, json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
Q = 1024
ART = (ROOT / 'panel.svg.fragment').read_text()
PALETTE = re.findall(r'id="wander_palette_\d+"[^>]*fill="([^"]+)" stroke="([^"]+)" stroke-width="([^"]+)"', ART)

def apply(frame):
    art = re.sub(r'<circle id="wander_palette[^>]*/>', '', ART)
    def node(m):
        i = int(m.group(2)); tag = m.group(0)
        x, y, scale, c, s, shade, paint = frame['nodes'][i]
        matrix = f'matrix({scale*c/Q/Q:.4f} {scale*s/Q/Q:.4f} {-scale*s/Q/Q:.4f} {scale*c/Q/Q:.4f} {x/Q:.3f} {y/Q:.3f})'
        tag = re.sub(r'transform="[^"]*"', f'transform="{matrix}"', tag)
        if paint:
            fill, stroke, width = PALETTE[shade]
            tag = re.sub(r'stroke="[^"]*"', f'stroke="{stroke}"', tag, count=1)
            tag = re.sub(r'stroke-width="[^"]*"', f'stroke-width="{width}"', tag)
            if paint == 2 and 'fill="none"' not in tag:
                tag = re.sub(r'fill="[^"]*"', f'fill="{fill}"', tag, count=1)
        return tag
    art = re.sub(r'<(path|circle) id="wander_node_(\d+)"[^>]*/>', node, art)
    def text(m):
        i = int(m.group(1)); x, y, scale, chars = frame['texts'][i]
        tag = re.sub(r'transform="[^"]*"', f'transform="matrix({scale/Q:.3f} 0 0 {scale/Q:.3f} {x/Q:.3f} {y/Q:.3f})"', m.group(0))
        return re.sub(r'>[^<]*</text>', f'>{html.escape(chars)}</text>', tag)
    art = re.sub(r'<text id="wander_text_(\d+)"[^>]*>[^<]*</text>', text, art)
    art = art.replace('UniversNextPro-Light', 'Helvetica Neue, Arial')
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220" viewBox="0 0 480 220"><rect width="480" height="220" fill="#000"/>{art}</svg>'

# seconds held sound rate chance scale dejavu length spread octave page
def at(seconds, held=1, page=0, sound=.15, rate=.45, chance=.7, scale=.19, dejavu=.5, length=.47, spread=.375, jitter=.25):
    return (seconds, held, sound, rate, chance, scale, dejavu, length, spread, jitter, page)
script = [at(0.1, held=0), at(2.3), at(1.7), at(3.2, spread=.875, scale=.8, rate=.6), at(1.1, page=1, spread=.125, scale=.1), at(1.5, held=0)]
shots = [1, 2, 3, 4, 5]
captions = ["holding a key, dorian, 2 octaves", "a moment later", "major pentatonic, 4 octaves, faster", "minor, 1 octave, page 2", "after letting go"]
lines = '\n'.join(' '.join(str(x) for x in r) for r in script)
frames = json.loads(subprocess.run([sys.argv[1]], input=lines, capture_output=True, text=True, check=True).stdout)
body = ''.join(f'<div style="margin:0 0 10px">{apply(frames[i])}<div>{c}</div></div>' for i, c in zip(shots, captions))
pathlib.Path(sys.argv[2]).write_text(f'<html><body style="margin:0;padding:8px;background:#111;color:#ccc;font:13px Helvetica;width:480px">{body}</body></html>')
