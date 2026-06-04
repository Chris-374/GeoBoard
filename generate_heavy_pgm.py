import sys

if len(sys.argv) != 4:
    print("Uso: python3 generate_heavy_pgm.py <ancho> <alto> <salida.pgm>")
    sys.exit(1)

w = int(sys.argv[1])
h = int(sys.argv[2])
out = sys.argv[3]

# PGM P5 binario: más liviano que P2 ASCII
with open(out, "wb") as f:
    f.write(f"P5\n{w} {h}\n255\n".encode())

    for y in range(h):
        row = bytearray()

        for x in range(w):
            # Fondo blanco
            pixel = 255

            # Figura grande tipo rectángulo/cuadrado
            if w * 0.20 < x < w * 0.80 and h * 0.20 < y < h * 0.80:
                pixel = 0

            # Hueco interno para que haya más bordes
            if w * 0.35 < x < w * 0.65 and h * 0.35 < y < h * 0.65:
                pixel = 255

            row.append(pixel)

        f.write(row)

print(f"Imagen generada: {out} ({w}x{h})")