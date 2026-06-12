#!/usr/bin/env python3
import math
import os
import sys

SHAPES = ["square", "rectangle", "triangle", "circle", "diamond", "plus", "x_shape", "pentagon", "trapezoid"]

def usage():
    print("Uso:")
    print("  python3 scripts/generate_shape_pgm.py <shape|all> <width> <height> <output.pgm|output_dir>")
    print("Formas:", ", ".join(SHAPES))

def dist_to_segment(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    wx, wy = px - ax, py - ay
    length_sq = vx * vx + vy * vy
    if length_sq == 0:
        return math.hypot(px - ax, py - ay)
    t = (wx * vx + wy * vy) / length_sq
    t = max(0.0, min(1.0, t))
    cx, cy = ax + t * vx, ay + t * vy
    return math.hypot(px - cx, py - cy)

def near_polygon_edge(x, y, points, thickness):
    j = len(points) - 1
    for i in range(len(points)):
        ax, ay = points[j]
        bx, by = points[i]
        if dist_to_segment(x, y, ax, ay, bx, by) <= thickness:
            return True
        j = i
    return False

def active_pixel(shape, x, y, w, h, thickness):
    cx, cy = w / 2.0, h / 2.0

    if shape == "square":
        side = min(w, h) * 0.60
        l, r = cx - side / 2.0, cx + side / 2.0
        t, b = cy - side / 2.0, cy + side / 2.0
        on_border = abs(x - l) <= thickness or abs(x - r) <= thickness or abs(y - t) <= thickness or abs(y - b) <= thickness
        return l <= x <= r and t <= y <= b and on_border

    if shape == "rectangle":
        l, r = w * 0.15, w * 0.85
        t, b = h * 0.30, h * 0.70
        on_border = abs(x - l) <= thickness or abs(x - r) <= thickness or abs(y - t) <= thickness or abs(y - b) <= thickness
        return l <= x <= r and t <= y <= b and on_border

    if shape == "triangle":
        points = [(cx, h * 0.18), (w * 0.16, h * 0.82), (w * 0.84, h * 0.82)]
        return near_polygon_edge(x, y, points, thickness)

    if shape == "circle":
        radius = min(w, h) * 0.30
        return abs(math.hypot(x - cx, y - cy) - radius) <= thickness

    if shape == "diamond":
        points = [(cx, h * 0.14), (w * 0.86, cy), (cx, h * 0.86), (w * 0.14, cy)]
        return near_polygon_edge(x, y, points, thickness)

    if shape == "plus":
        vertical = abs(x - cx) <= thickness * 2.0 and h * 0.18 <= y <= h * 0.82
        horizontal = abs(y - cy) <= thickness * 2.0 and w * 0.18 <= x <= w * 0.82
        return vertical or horizontal

    if shape == "x_shape":
        d1 = dist_to_segment(x, y, w * 0.18, h * 0.18, w * 0.82, h * 0.82)
        d2 = dist_to_segment(x, y, w * 0.82, h * 0.18, w * 0.18, h * 0.82)
        return d1 <= thickness or d2 <= thickness

    if shape == "pentagon":
        points = [(cx, h * 0.14), (w * 0.84, h * 0.42), (w * 0.72, h * 0.84), (w * 0.28, h * 0.84), (w * 0.16, h * 0.42)]
        return near_polygon_edge(x, y, points, thickness)

    if shape == "trapezoid":
        points = [(w * 0.32, h * 0.25), (w * 0.68, h * 0.25), (w * 0.86, h * 0.78), (w * 0.14, h * 0.78)]
        return near_polygon_edge(x, y, points, thickness)

    return False

def generate_shape(shape, w, h, output_path):
    if shape not in SHAPES:
        raise ValueError(f"Forma no soportada: {shape}")

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    thickness = max(2, min(w, h) // 120)

    with open(output_path, "wb") as f:
        f.write(f"P5\n# shape: {shape}\n{w} {h}\n255\n".encode("ascii"))
        for y in range(h):
            row = bytearray(w)
            for x in range(w):
                row[x] = 0 if active_pixel(shape, x, y, w, h, thickness) else 255
            f.write(row)

    print(f"Imagen generada: {output_path} ({w}x{h}, shape={shape})")

def main():
    if len(sys.argv) != 5:
        usage()
        sys.exit(1)

    shape = sys.argv[1].lower()
    w = int(sys.argv[2])
    h = int(sys.argv[3])
    output = sys.argv[4]

    if w <= 0 or h <= 0:
        print("Error: width y height deben ser positivos.")
        sys.exit(1)

    if shape == "all":
        os.makedirs(output, exist_ok=True)
        for s in SHAPES:
            generate_shape(s, w, h, os.path.join(output, f"{s}_{w}.pgm"))
        return

    generate_shape(shape, w, h, output)

if __name__ == "__main__":
    main()
