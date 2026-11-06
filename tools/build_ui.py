#!/usr/bin/env python3
"""Builds Resources/index.html (the single page the app and the plugin load) from the UI sources:

  ui/wheelhouse.html   markup with the placeholders /*STYLE*/, /*APP*/ and /*TRACER*/
  ui/style.css         styles
  ui/js/*.js           the app, in the order of JS_FILES (one shared scope inside one function)
  ui/tracer.js         ray tracer for the tent, runs as a Web Worker
"""
import pathlib

JS_FILES = ['core.js', 'controls.js', 'header.js', 'cards.js', 'room.js', 'keyboard.js', 'main.js']

root = pathlib.Path(__file__).resolve().parent.parent
ui = root / 'ui'


def read(path):
    """An inlined part must not close the tag it is pasted into."""
    text = path.read_text(encoding='utf-8')
    assert '</script' not in text.lower() and '</style' not in text.lower(), path
    return text


html = (ui / 'wheelhouse.html').read_text(encoding='utf-8')
for name in ('/*STYLE*/', '/*APP*/', '/*TRACER*/'):
    assert html.count(name) == 1, name
app = '\n'.join(read(ui / 'js' / f) for f in JS_FILES)
out = html.replace('/*STYLE*/', read(ui / 'style.css')).replace('/*APP*/', app).replace('/*TRACER*/', read(ui / 'tracer.js'))
(root / 'Resources' / 'index.html').write_text(out, encoding='utf-8')
print('Resources/index.html', len(out.encode('utf-8')), 'bytes')
