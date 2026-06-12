#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "shape_masks.h"

static void clear_mask(uint8_t mask[8]) {
    for (int i = 0; i < 8; i++) mask[i] = 0;
}

static void copy_template(uint8_t mask[8], const uint8_t src[8]) {
    for (int i = 0; i < 8; i++) mask[i] = src[i];
}

static void to_lower_copy(const char *src, char *dst, int dst_size) {
    int i;
    if (dst_size <= 0) return;
    for (i = 0; src && src[i] != '\0' && i + 1 < dst_size; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

const char *geoboard_shape_name(GeoShapeType shape) {
    switch (shape) {
        case GEO_SHAPE_SQUARE: return "cuadrado";
        case GEO_SHAPE_RECTANGLE: return "rectangulo";
        case GEO_SHAPE_TRIANGLE: return "triangulo";
        case GEO_SHAPE_CIRCLE: return "circulo";
        case GEO_SHAPE_DIAMOND: return "rombo";
        case GEO_SHAPE_PLUS: return "cruz";
        case GEO_SHAPE_X: return "equis";
        case GEO_SHAPE_PENTAGON: return "pentagono";
        case GEO_SHAPE_TRAPEZOID: return "trapecio";
        default: return "desconocida";
    }
}

GeoShapeType geoboard_detect_shape_from_filename(const char *filename) {
    char name[256];
    if (!filename) return GEO_SHAPE_UNKNOWN;
    to_lower_copy(filename, name, (int)sizeof(name));

    if (strstr(name, "triangle") || strstr(name, "triangulo")) return GEO_SHAPE_TRIANGLE;
    if (strstr(name, "circle") || strstr(name, "circulo")) return GEO_SHAPE_CIRCLE;
    if (strstr(name, "diamond") || strstr(name, "rombo")) return GEO_SHAPE_DIAMOND;
    if (strstr(name, "plus") || strstr(name, "cruz")) return GEO_SHAPE_PLUS;
    if (strstr(name, "pentagon") || strstr(name, "pentagono")) return GEO_SHAPE_PENTAGON;
    if (strstr(name, "trapezoid") || strstr(name, "trapecio")) return GEO_SHAPE_TRAPEZOID;
    if (strstr(name, "rectangle") || strstr(name, "rectangulo") || strstr(name, "rect")) return GEO_SHAPE_RECTANGLE;
    if (strstr(name, "square") || strstr(name, "cuadrado")) return GEO_SHAPE_SQUARE;
    if (strstr(name, "x_shape") || strstr(name, "equis")) return GEO_SHAPE_X;

    return GEO_SHAPE_UNKNOWN;
}

GeoShapeType geoboard_infer_shape_from_metrics(uint64_t active_pixels,
                                               int32_t bbox_min_x,
                                               int32_t bbox_min_y,
                                               int32_t bbox_max_x,
                                               int32_t bbox_max_y) {
    int32_t bbox_w, bbox_h;
    double aspect, bbox_area, fill_ratio;

    if (active_pixels == 0 || bbox_min_x < 0 || bbox_min_y < 0) return GEO_SHAPE_UNKNOWN;

    bbox_w = bbox_max_x - bbox_min_x + 1;
    bbox_h = bbox_max_y - bbox_min_y + 1;
    if (bbox_w <= 0 || bbox_h <= 0) return GEO_SHAPE_UNKNOWN;

    aspect = (double)bbox_w / (double)bbox_h;
    bbox_area = (double)bbox_w * (double)bbox_h;
    fill_ratio = (double)active_pixels / bbox_area;

    if (aspect >= 0.85 && aspect <= 1.15 && fill_ratio >= 0.45) return GEO_SHAPE_SQUARE;
    if ((aspect < 0.85 || aspect > 1.15) && fill_ratio >= 0.45) return GEO_SHAPE_RECTANGLE;
    if (fill_ratio >= 0.20 && fill_ratio < 0.45) return GEO_SHAPE_TRIANGLE;

    return GEO_SHAPE_UNKNOWN;
}

static int rectangle_is_vertical(int32_t bbox_min_x, int32_t bbox_min_y,
                                 int32_t bbox_max_x, int32_t bbox_max_y) {
    int32_t bbox_w = bbox_max_x - bbox_min_x + 1;
    int32_t bbox_h = bbox_max_y - bbox_min_y + 1;
    return bbox_h > bbox_w;
}

int geoboard_build_pretty_mask(GeoShapeType shape,
                               uint8_t mask[8],
                               int32_t bbox_min_x,
                               int32_t bbox_min_y,
                               int32_t bbox_max_x,
                               int32_t bbox_max_y) {
    static const uint8_t square_template[8] = {
        0b00000000, 0b01111110, 0b01000010, 0b01000010,
        0b01000010, 0b01000010, 0b01111110, 0b00000000
    };
    static const uint8_t rect_h_template[8] = {
        0b00000000, 0b00000000, 0b01111110, 0b01000010,
        0b01000010, 0b01111110, 0b00000000, 0b00000000
    };
    static const uint8_t rect_v_template[8] = {
        0b00000000, 0b00111100, 0b00100100, 0b00100100,
        0b00100100, 0b00100100, 0b00111100, 0b00000000
    };
    static const uint8_t triangle_template[8] = {
        0b00000000, 0b00010000, 0b00101000, 0b01000100,
        0b10000010, 0b11111110, 0b00000000, 0b00000000
    };
    static const uint8_t circle_template[8] = {
        0b00000000, 0b00111100, 0b01000010, 0b10000001,
        0b10000001, 0b01000010, 0b00111100, 0b00000000
    };
    static const uint8_t diamond_template[8] = {
        0b00000000, 0b00010000, 0b00101000, 0b01000100,
        0b10000010, 0b01000100, 0b00101000, 0b00010000
    };
    static const uint8_t plus_template[8] = {
        0b00000000, 0b00011000, 0b00011000, 0b01111110,
        0b01111110, 0b00011000, 0b00011000, 0b00000000
    };
    static const uint8_t x_template[8] = {
        0b10000001, 0b01000010, 0b00100100, 0b00011000,
        0b00011000, 0b00100100, 0b01000010, 0b10000001
    };
    static const uint8_t pentagon_template[8] = {
        0b00000000, 0b00011000, 0b00100100, 0b01000010,
        0b10000001, 0b10000001, 0b11111111, 0b00000000
    };
    static const uint8_t trapezoid_template[8] = {
        0b00000000, 0b00000000, 0b00111100, 0b01000010,
        0b10000001, 0b11111111, 0b00000000, 0b00000000
    };

    clear_mask(mask);

    switch (shape) {
        case GEO_SHAPE_SQUARE:
            copy_template(mask, square_template); return 0;
        case GEO_SHAPE_RECTANGLE:
            if (rectangle_is_vertical(bbox_min_x, bbox_min_y, bbox_max_x, bbox_max_y))
                copy_template(mask, rect_v_template);
            else
                copy_template(mask, rect_h_template);
            return 0;
        case GEO_SHAPE_TRIANGLE:
            copy_template(mask, triangle_template); return 0;
        case GEO_SHAPE_CIRCLE:
            copy_template(mask, circle_template); return 0;
        case GEO_SHAPE_DIAMOND:
            copy_template(mask, diamond_template); return 0;
        case GEO_SHAPE_PLUS:
            copy_template(mask, plus_template); return 0;
        case GEO_SHAPE_X:
            copy_template(mask, x_template); return 0;
        case GEO_SHAPE_PENTAGON:
            copy_template(mask, pentagon_template); return 0;
        case GEO_SHAPE_TRAPEZOID:
            copy_template(mask, trapezoid_template); return 0;
        default:
            return -1;
    }
}