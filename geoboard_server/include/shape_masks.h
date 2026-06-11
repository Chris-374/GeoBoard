#ifndef SHAPE_MASKS_H
#define SHAPE_MASKS_H

#include <stdint.h>

typedef enum {
    GEO_SHAPE_UNKNOWN = 0,
    GEO_SHAPE_SQUARE,
    GEO_SHAPE_RECTANGLE,
    GEO_SHAPE_TRIANGLE,
    GEO_SHAPE_CIRCLE,
    GEO_SHAPE_DIAMOND,
    GEO_SHAPE_PLUS,
    GEO_SHAPE_X,
    GEO_SHAPE_PENTAGON,
    GEO_SHAPE_TRAPEZOID
} GeoShapeType;

const char *geoboard_shape_name(GeoShapeType shape);
GeoShapeType geoboard_detect_shape_from_filename(const char *filename);
GeoShapeType geoboard_infer_shape_from_metrics(uint64_t active_pixels,
                                               int32_t bbox_min_x,
                                               int32_t bbox_min_y,
                                               int32_t bbox_max_x,
                                               int32_t bbox_max_y);
int geoboard_build_pretty_mask(GeoShapeType shape,
                               uint8_t mask[8],
                               int32_t bbox_min_x,
                               int32_t bbox_min_y,
                               int32_t bbox_max_x,
                               int32_t bbox_max_y);

#endif
