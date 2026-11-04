#!/usr/bin/env python3
"""Baut Resources/index.html aus ui/wheelhouse.html und ui/tracer.js (Strahlenverfolgung fürs Zelt, läuft als Web Worker)."""
import pathlib
root = pathlib.Path(__file__).resolve().parent.parent
html = (root / 'ui' / 'wheelhouse.html').read_text(encoding='utf-8')
tracer = (root / 'ui' / 'tracer.js').read_text(encoding='utf-8')
assert '</script' not in tracer.lower()
out = html.replace('/*TRACER*/', tracer)
(root / 'Resources' / 'index.html').write_text(out, encoding='utf-8')
print('Resources/index.html', len(out.encode('utf-8')), 'Bytes')
