#!/usr/bin/env python3
"""
generate_shapes.py  —  Genera PGMs de figuras con CONTORNO visible al escalar a 8x8.

Uso:
    python3 generate_shapes.py [carpeta_destino]
    python3 generate_shapes.py images/

Tamaño fijo: 256x256 px (óptimo para downscale a 8x8 con grosor de contorno adecuado).
"""

import sys, math, os

DEST = sys.argv[1] if len(sys.argv) > 1 else "images"
os.makedirs(DEST, exist_ok=True)

W, H   = 256, 256   # 256×256: suficiente para procesar y escalar bien
BG     = 230
FG     = 20
THICK  = 10          # grosor de contorno en píxeles (visible al escalar a 8x8)

def blank():
    return bytearray([BG] * (W * H))

def thick_px(img, x, y, t=THICK):
    for dy in range(-t//2, t//2+1):
        for dx in range(-t//2, t//2+1):
            nx, ny = x+dx, y+dy
            if 0 <= nx < W and 0 <= ny < H:
                img[ny * W + nx] = FG

def line_thick(img, ax, ay, bx, by, t=THICK):
    steps = max(abs(bx-ax), abs(by-ay)) * 4 + 1
    for i in range(steps+1):
        f = i / steps
        thick_px(img, int(ax + f*(bx-ax)), int(ay + f*(by-ay)), t)

def write_pgm(path, img):
    header = f"P5\n{W} {H}\n255\n".encode('ascii')
    with open(path, 'wb') as f:
        f.write(header + img)
    # Muestra la máscara 8x8 esperada en consola
    print(f"  -> {path}")
    preview_mask(img)

def preview_mask(img):
    """Muestra cómo quedará la máscara 8x8 con umbral del 15%."""
    print("     Mascara esperada:")
    print("     +--------+")
    for row in range(8):
        r0 = row * H // 8; r1 = (row+1) * H // 8
        line = "     |"
        for col in range(8):
            c0 = col * W // 8; c1 = (col+1) * W // 8
            total = active = 0
            for r in range(r0, r1):
                for c in range(c0, c1):
                    if img[r*W+c] < 128:
                        active += 1
                    total += 1
            line += '#' if (total > 0 and active * 100 // total >= 15) else '.'
        print(line + "|")
    print("     +--------+\n")

# ------------------------------------------------------------------
# Círculo — solo contorno
# ------------------------------------------------------------------
def gen_circle():
    img = blank()
    cx, cy = W//2, H//2
    r = int(W * 0.40)
    for angle_tenth in range(3600):
        a = math.radians(angle_tenth / 10.0)
        thick_px(img, int(cx + r*math.cos(a)), int(cy + r*math.sin(a)))
    write_pgm(f"{DEST}/circulo.pgm", img)

# ------------------------------------------------------------------
# Cuadrado
# ------------------------------------------------------------------
def gen_square():
    img = blank()
    m = 30
    for x in range(m, W-m):
        thick_px(img, x, m); thick_px(img, x, H-m-1)
    for y in range(m, H-m):
        thick_px(img, m, y); thick_px(img, W-m-1, y)
    write_pgm(f"{DEST}/cuadrado.pgm", img)

# ------------------------------------------------------------------
# Rectángulo (más ancho que alto)
# ------------------------------------------------------------------
def gen_rectangle():
    img = blank()
    x0, x1, y0, y1 = 20, W-20, 60, H-60
    for x in range(x0, x1):
        thick_px(img, x, y0); thick_px(img, x, y1-1)
    for y in range(y0, y1):
        thick_px(img, x0, y); thick_px(img, x1-1, y)
    write_pgm(f"{DEST}/rectangulo.pgm", img)

# ------------------------------------------------------------------
# Triángulo
# ------------------------------------------------------------------
def gen_triangle():
    img = blank()
    p = [(W//2, 20), (20, H-25), (W-20, H-25)]
    for i in range(3):
        line_thick(img, *p[i], *p[(i+1)%3])
    write_pgm(f"{DEST}/triangulo.pgm", img)

# ------------------------------------------------------------------
# Rombo
# ------------------------------------------------------------------
def gen_diamond():
    img = blank()
    cx, cy = W//2, H//2
    pts = [(cx, 20), (W-20, cy), (cx, H-20), (20, cy)]
    for i in range(4):
        line_thick(img, *pts[i], *pts[(i+1)%4])
    write_pgm(f"{DEST}/rombo.pgm", img)

# ------------------------------------------------------------------
# Cruz / Plus
# ------------------------------------------------------------------
def gen_cross():
    img = blank()
    cx, cy = W//2, H//2
    arm, half = 90, 22
    pts = [
        (cx-half, cy-arm), (cx+half, cy-arm),
        (cx+half, cy-half),(cx+arm,  cy-half),
        (cx+arm,  cy+half),(cx+half, cy+half),
        (cx+half, cy+arm), (cx-half, cy+arm),
        (cx-half, cy+half),(cx-arm,  cy+half),
        (cx-arm,  cy-half),(cx-half, cy-half),
    ]
    for i in range(len(pts)):
        line_thick(img, *pts[i], *pts[(i+1)%len(pts)], 6)
    write_pgm(f"{DEST}/cruz.pgm", img)

# ------------------------------------------------------------------
# Pentágono
# ------------------------------------------------------------------
def gen_pentagon():
    img = blank()
    cx, cy, r = W//2, H//2+10, int(W*0.40)
    pts = [(int(cx+r*math.cos(math.radians(-90+i*72))),
            int(cy+r*math.sin(math.radians(-90+i*72)))) for i in range(5)]
    for i in range(5):
        line_thick(img, *pts[i], *pts[(i+1)%5])
    write_pgm(f"{DEST}/pentagono.pgm", img)

# ------------------------------------------------------------------
# Hexágono
# ------------------------------------------------------------------
def gen_hexagon():
    img = blank()
    cx, cy, r = W//2, H//2, int(W*0.40)
    pts = [(int(cx+r*math.cos(math.radians(i*60))),
            int(cy+r*math.sin(math.radians(i*60)))) for i in range(6)]
    for i in range(6):
        line_thick(img, *pts[i], *pts[(i+1)%6])
    write_pgm(f"{DEST}/hexagono.pgm", img)

# ------------------------------------------------------------------
# Flecha (extra)
# ------------------------------------------------------------------
def gen_arrow():
    img = blank()
    cx, cy = W//2, H//2
    # Cuerpo horizontal
    line_thick(img, 30, cy, cx+10, cy)
    # Punta de flecha
    pts = [(cx+10, cy-50), (W-25, cy), (cx+10, cy+50)]
    for i in range(3):
        line_thick(img, *pts[i], *pts[(i+1)%3])
    write_pgm(f"{DEST}/flecha.pgm", img)

# ------------------------------------------------------------------

print(f"Generando figuras en '{DEST}/' (256x256 px, grosor={THICK}px)...\n")
gen_circle()
gen_square()
gen_rectangle()
gen_triangle()
gen_diamond()
gen_cross()
gen_pentagon()
gen_hexagon()
gen_arrow()
print("Listo. 9 figuras generadas.")
