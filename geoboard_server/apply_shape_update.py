#!/usr/bin/env python3
from pathlib import Path
import sys

def fail(msg):
    print(f"[ERROR] {msg}")
    sys.exit(1)

server_path = Path("src/server_orchestrator.c")
makefile_path = Path("Makefile")

if not server_path.exists():
    fail("No se encontro src/server_orchestrator.c. Ejecuta esto desde geoboard_server.")
if not makefile_path.exists():
    fail("No se encontro Makefile. Ejecuta esto desde geoboard_server.")

server = server_path.read_text(encoding="utf-8")

if '#include "shape_masks.h"' not in server:
    marker = '#include "server_orchestrator.h"'
    if marker in server:
        server = server.replace(marker, marker + '\n#include "shape_masks.h"')
    else:
        marker = '#include "pgm_image.h"'
        if marker in server:
            server = server.replace(marker, marker + '\n#include "shape_masks.h"')
        else:
            fail('No se encontro donde insertar #include "shape_masks.h".')

pretty_block = '''
    /*
     * La mascara raw puede quedar gruesa al reducir una imagen grande a 8x8.
     * Para el hardware se genera una mascara limpia usando plantillas.
     */
    {
        uint8_t raw_mask[8];
        GeoShapeType shape_type;

        memcpy(raw_mask, final_mask, sizeof(raw_mask));

        shape_type = geoboard_detect_shape_from_filename(filename);

        if (shape_type == GEO_SHAPE_UNKNOWN) {
            shape_type = geoboard_infer_shape_from_metrics(total_active,
                                                           global_min_x,
                                                           global_min_y,
                                                           global_max_x,
                                                           global_max_y);
        }

        if (geoboard_build_pretty_mask(shape_type,
                                       final_mask,
                                       global_min_x,
                                       global_min_y,
                                       global_max_x,
                                       global_max_y) == 0) {
            printf("[SERVIDOR] Forma elegida para mascara limpia: %s\\n",
                   geoboard_shape_name(shape_type));
        } else {
            memcpy(final_mask, raw_mask, sizeof(raw_mask));
            printf("[SERVIDOR] No se pudo generar mascara limpia; se usa mascara raw distribuida.\\n");
        }
    }

'''

if "Forma elegida para mascara limpia" not in server:
    target = "    print_mask(final_mask);"
    if target not in server:
        fail("No se encontro print_mask(final_mask); para insertar la mascara limpia.")
    server = server.replace(target, pretty_block + target)

server_path.write_text(server, encoding="utf-8")

makefile = makefile_path.read_text(encoding="utf-8")

if "src/shape_masks.c" not in makefile:
    if "src/file_utils.c \\" in makefile:
        makefile = makefile.replace("src/file_utils.c \\", "src/file_utils.c \\\n\tsrc/shape_masks.c \\")
    elif "src/pgm_image.c \\" in makefile:
        makefile = makefile.replace("src/pgm_image.c \\", "src/pgm_image.c \\\n\tsrc/shape_masks.c \\")
    else:
        fail("No se encontro lugar en Makefile para agregar src/shape_masks.c.")

makefile_path.write_text(makefile, encoding="utf-8")

print("[OK] Actualizacion aplicada.")
print("Ejecuta ahora:")
print("  make clean")
print("  make")
