#!/usr/bin/env python3
"""Wheelhouse app icon (design "E - Leslie above drawbars").

Writes the vector sources and renders the PNGs the build uses:
  Resources/icon/wheelhouse-icon.svg        detailed (large sizes, 48 px and up)
  Resources/icon/wheelhouse-icon-small.svg  simplified for 16 / 32 px
  Resources/icon/icon-1024.png, icon-small-256.png   (ICON_BIG / ICON_SMALL in CMakeLists.txt)
and prints the header logo (medium detail) for ui/wheelhouse.html.

Rendering needs Playwright with Chrome:  python3 tools/make_icon.py
"""
import math, os, asyncio, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'Resources', 'icon')
GOLD, ACCENT = '#f2c879', '#f2a33a'

KIND = ['brown', 'brown', 'white', 'white', 'black', 'white', 'black', 'black', 'white']
COL = {'brown': ('#a8662f', '#7a4519'), 'white': ('#efe6d2', '#b9ab92'), 'black': ('#26201b', '#8a7a64')}
REG = [8, 8, 8, 6, 0, 0, 3, 0, 4]          # a registration that looks like playing
SLATS = [55, 67, 79, 91, 103, 115]


def bars(detail):
    out = []
    for i, n in enumerate(REG):
        cx, ln = 30 + i * 24.5, 4 + n * 7
        f, s = COL[KIND[i]]
        out.append(f'<rect x="{cx-3.5:.1f}" y="160" width="7" height="{ln}" fill="#d9ccb4"/>')
        if detail:
            out.append(f'<rect x="{cx-3.5:.1f}" y="160" width="2" height="{ln}" fill="#ffffff" opacity="0.5"/>')
        out.append(f'<rect x="{cx-9.5:.1f}" y="{160+ln}" width="19" height="24" rx="5" fill="{f}" stroke="{s}" stroke-width="1.5"/>')
        if detail:
            out.append(f'<rect x="{cx-9.5:.1f}" y="{160+ln}" width="19" height="5" rx="2.5" fill="#ffffff" opacity="0.18"/>')
    return '\n'.join(out)


def horn(detail):
    if not detail:
        return ('<rect x="124" y="86" width="8" height="42" fill="#3a2a1c"/>'
                '<path d="M128 80 L100 77 L86 72 L86 96 L100 91 L128 88 Z" fill="#4a3a2a"/>'
                f'<path d="M128 79 L162 74 L178 64 L184 64 L184 104 L178 104 L162 94 L128 89 Z" fill="{GOLD}"/>'
                '<rect x="120" y="76" width="16" height="16" rx="3" fill="#b8913f"/>')
    return ('<rect x="124" y="86" width="8" height="42" fill="#3a2a1c"/>'
            '<rect x="124" y="86" width="2.5" height="42" fill="#7a5a3a" opacity="0.8"/>'
            '<path d="M128 80 L100 77 Q92 76 90 72 L86 72 L86 96 L90 96 Q92 92 100 91 L128 88 Z" fill="#4a3a2a"/>'
            '<ellipse cx="86" cy="84" rx="3.5" ry="12" fill="#2a2018"/>'
            f'<path d="M128 79 L162 74 Q174 72 178 64 L184 64 L184 104 L178 104 Q174 96 162 94 L128 89 Z" fill="{GOLD}"/>'
            '<path d="M128 79 L162 74 Q174 72 178 64 L184 64 L184 68 L178 68 Q172 76 162 77 L128 81 Z" fill="#ffe6b0" opacity="0.45"/>'
            '<ellipse cx="184" cy="84" rx="6" ry="20" fill="#9a6a24"/>'
            '<ellipse cx="185" cy="84" rx="3.5" ry="16" fill="#2a1a0c"/>'
            '<rect x="120" y="76" width="16" height="16" rx="3" fill="#b8913f"/>'
            f'<rect x="120" y="76" width="16" height="4" rx="2" fill="{GOLD}"/>')


def grain():
    out = []
    for i in range(9):
        x0 = 54 + i * 18 + (i % 3) * 3
        d = f'M{x0} 40'
        for y in range(52, 161, 12):
            d += f' Q{x0 + math.sin(y * 0.07 + i) * 4:.1f} {y-6} {x0 + math.sin(y * 0.05 + i * 1.7) * 3:.1f} {y}'
        c, w = ('#3e2210', 1.2) if i % 2 else ('#9a6538', 0.8)
        out.append(f'<path d="{d}" fill="none" stroke="{c}" stroke-width="{w}" opacity="0.55"/>')
    return '\n'.join(out)


def icon(level, pid='w'):
    """level 2 = detailed, 1 = medium (header logo), 0 = small (16/32 px)."""
    if level == 0:
        small = [(8, '#a8662f'), (8, '#a8662f'), (3, '#efe6d2'), (6, '#efe6d2'), (2, GOLD)]
        sb = ''.join(f'<rect x="{34+i*40}" y="150" width="32" height="{30+n*6}" rx="9" fill="{c}"/>' for i, (n, c) in enumerate(small))
        return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">'
                '<rect x="4" y="4" width="248" height="248" rx="56" fill="#1e1611"/>'
                '<rect x="36" y="24" width="184" height="24" rx="5" fill="#8a552d"/>'
                '<rect x="44" y="44" width="168" height="96" fill="#6b3f20"/>'
                '<rect x="60" y="58" width="136" height="66" fill="#0e0a08"/>'
                '<rect x="60" y="72" width="136" height="14" fill="#8a552d"/>'
                '<rect x="60" y="98" width="136" height="14" fill="#8a552d"/>' + sb + '</svg>')
    det = level == 2
    defs = (f'<defs><clipPath id="{pid}T"><rect x="4" y="4" width="248" height="248" rx="56"/></clipPath>'
            f'<radialGradient id="{pid}G" cx="50%" cy="34%" r="60%"><stop offset="0" stop-color="#3a2a1c"/><stop offset="1" stop-color="#16110d"/></radialGradient>'
            f'<linearGradient id="{pid}W" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#5c3319"/><stop offset="0.5" stop-color="#7c4a26"/><stop offset="1" stop-color="#4e2a14"/></linearGradient>'
            f'<linearGradient id="{pid}S" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#8a5a33"/><stop offset="1" stop-color="#4a2914"/></linearGradient></defs>')
    slat = ''.join((f'<rect x="62" y="{y}" width="132" height="7" fill="url(#{pid}S)"/>' +
                    (f'<rect x="62" y="{y}" width="132" height="1.5" fill="#b07a48" opacity="0.7"/>' if det else '')) for y in SLATS)
    body = (f'<g clip-path="url(#{pid}T)">'
            f'<rect x="4" y="4" width="248" height="248" fill="url(#{pid}G)"/>'
            '<rect x="40" y="26" width="176" height="14" rx="3" fill="#86532c"/>'
            '<rect x="40" y="38" width="176" height="3" fill="#3a2010" opacity="0.6"/>'
            f'<rect x="48" y="40" width="160" height="120" fill="url(#{pid}W)"/>' + (grain() if det else '') +
            '<rect x="62" y="52" width="132" height="76" rx="3" fill="#0e0a08"/>' +
            (f'<path d="M76 92 A54 12 0 0 0 176 96" fill="none" stroke="{ACCENT}" stroke-width="3" stroke-linecap="round" opacity="0.8"/>' if det else '') +
            horn(det) + slat +
            '<rect x="62" y="52" width="132" height="76" rx="3" fill="none" stroke="#2e180a" stroke-width="3"/>'
            '<rect x="4" y="150" width="248" height="20" fill="#1c1410"/>'
            f'<rect x="4" y="150" width="248" height="3" fill="{GOLD}" opacity="0.85"/>' + bars(det) + '</g>'
            '<rect x="4" y="4" width="248" height="248" rx="56" fill="none" stroke="#3a2f25" stroke-width="2"/>')
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">{defs}{body}</svg>'


async def render(svg, path, px):
    from playwright.async_api import async_playwright
    async with async_playwright() as p:
        b = await p.chromium.launch(executable_path='/usr/bin/google-chrome', args=['--no-sandbox'])
        pg = await b.new_page(viewport={'width': px, 'height': px})
        await pg.set_content(f'<html><body style="margin:0;background:transparent">{svg.replace("<svg ", f"<svg width={px} height={px} ", 1)}</body></html>')
        await pg.screenshot(path=path, omit_background=True, clip={'x': 0, 'y': 0, 'width': px, 'height': px})
        await b.close()


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    big, small = icon(2), icon(0)
    open(os.path.join(OUT, 'wheelhouse-icon.svg'), 'w').write(big)
    open(os.path.join(OUT, 'wheelhouse-icon-small.svg'), 'w').write(small)
    if '--logo' in sys.argv:
        print(icon(1, 'hl'))
        sys.exit()
    asyncio.run(render(big, os.path.join(OUT, 'icon-1024.png'), 1024))
    asyncio.run(render(small, os.path.join(OUT, 'icon-small-256.png'), 256))
    print('written to', OUT)
