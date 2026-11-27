/*
 * Doodlebold file format plugins, interface version 1.
 *
 * A plugin is a shared library (.so, .dll or .dylib) that adds file formats to Doodlebold's Open and Save. It exports one C
 * function, doodlebold_plugin_entry(), and everything that crosses between the plugin and the program is a plain C struct or
 * a C function pointer: no Qt and no C++, so a plugin can be built with any C compiler. plugins/examples/farbfeld/ is a
 * complete example.
 *
 * The rules:
 * - Text is UTF-8 and ends with a 0 byte.
 * - Pixels are RGBA, 16 bits per channel in the machine's own byte order, 0 to 65535, with straight (not premultiplied) alpha.
 *   Rows run top to bottom; `stride` is the number of bytes from the start of one row to the start of the next.
 * - Masks are 16-bit coverage values, one per pixel: 0 hides the layer there, 65535 shows it.
 * - Layers are given bottom to top, and a layer's parent folder is given before the layers inside it.
 * - The program may call read() and write() from any thread, and from several threads at once, so they must not keep state
 *   between calls.
 * - Every struct starts with its own size, `struct_size`, so that a later version can add fields at the end. Whoever fills
 *   a struct sets it to sizeof() the struct as they compiled it.
 * - A plugin runs in a process of its own, never inside Doodlebold, and only once the user has allowed that very file. It
 *   gets a file's bytes in read() and gives everything back through these functions. It may not open files, use the network,
 *   or start programs or threads: Windows refuses (an AppContainer), and on Linux the process ends at the attempt (seccomp).
 *   What it prints is discarded. It is loaded from a copy of itself, so it must be one self-contained file: link what it
 *   needs statically.
 */
#ifndef DOODLEBOLD_FORMAT_PLUGIN_H
#define DOODLEBOLD_FORMAT_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The interface version this header describes. A later version that can't keep working with plugins built for this one gets
 * a new number, and the program says which versions it loads. */
#define DOODLEBOLD_PLUGIN_API_VERSION 1u

#if defined(_WIN32)
#define DOODLEBOLD_PLUGIN_EXPORT __declspec(dllexport)
#else
#define DOODLEBOLD_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* What read() and write() return. */
enum {
    DOODLEBOLD_OK = 0,
    DOODLEBOLD_ERROR_NOT_THIS_FORMAT = 1, /* the bytes are not a file of this format */
    DOODLEBOLD_ERROR_TRUNCATED = 2,       /* the file ends too soon */
    DOODLEBOLD_ERROR_CORRUPT = 3,         /* a size, count or value in the file is wrong */
    DOODLEBOLD_ERROR_UNSUPPORTED = 4,     /* a valid file using something this plugin can't read */
    DOODLEBOLD_ERROR_TOO_LARGE = 5,       /* too big to open, or the program refused a size */
    DOODLEBOLD_ERROR_WRITE_FAILED = 6,    /* the program couldn't take the bytes, or couldn't give the pixels */
    DOODLEBOLD_ERROR_OUT_OF_MEMORY = 7
};

/* doodlebold_layer.kind */
enum {
    DOODLEBOLD_LAYER_PIXELS = 0,
    DOODLEBOLD_LAYER_FOLDER = 1
};

/* doodlebold_format.flags: what the format keeps, so the program can warn before a save that loses something. */
enum {
    DOODLEBOLD_FORMAT_LAYERS = 1u << 0,   /* keeps layers, not only the flattened picture */
    DOODLEBOLD_FORMAT_ALPHA = 1u << 1,    /* keeps transparency */
    DOODLEBOLD_FORMAT_LOSSLESS = 1u << 2  /* keeps everything listed in doodlebold_layer, at 16 bits */
};

/* One layer or folder. */
typedef struct doodlebold_layer {
    uint32_t struct_size;
    int32_t kind;           /* DOODLEBOLD_LAYER_PIXELS or DOODLEBOLD_LAYER_FOLDER */
    int32_t parent;         /* the folder it is in: the number add_layer() gave that folder, or the folder's index in
                               doodlebold_write_host.layers; -1 for the top level */
    const char* name;       /* NULL or "": the program names it */
    const char* blend_mode; /* a key from src/core/layers/BlendMode.cpp, such as "normal" or "multiply"; NULL: "normal" */
    int32_t opacity;        /* 0 to 100 */
    int32_t visible;        /* 0 or 1 */
    int32_t clipping;       /* 1: clipped to the layer below */

    /* The pixels, a rectangle on the canvas; it may reach past the canvas, and what lies outside is cut off. A folder has
     * none: width and height 0. */
    int32_t x, y, width, height;
    const uint16_t* pixels; /* reading: width * height pixels; writing: NULL, fetch them with get_pixels() */
    size_t stride;

    /* The layer mask, if there is one. */
    int32_t has_mask;
    int32_t mask_enabled;          /* 0: the mask is kept but switched off */
    int32_t mask_x, mask_y, mask_width, mask_height;
    uint16_t mask_outside;         /* the coverage everywhere outside the mask's rectangle */
    const uint16_t* mask;          /* reading: mask_width * mask_height values; writing: NULL, fetch with get_mask() */
    size_t mask_stride;
} doodlebold_layer;

/* What the program gives read(). Every function takes `context` as its first argument. */
typedef struct doodlebold_read_host {
    uint32_t struct_size;
    void* context;
    const uint8_t* data; /* the whole file */
    size_t size;

    /* Call first, once. `background` is RGBA 0 to 255; alpha 0 means a transparent canvas. ppi: pixels per inch, 0 for the
     * program's default. Returns 0 if the size is refused (too large or not positive): return DOODLEBOLD_ERROR_TOO_LARGE. */
    int (*set_canvas)(void* context, int32_t width, int32_t height, double ppi, const uint8_t background[4]);
    /* Adds a layer on top of the others in its folder, copying everything, so `layer` and its pixels can be freed after.
     * Returns the layer's number (0, 1, 2 …), used as `parent` by layers inside a folder, or -1 if refused (a bad parent,
     * folders too deep, a size refused): return DOODLEBOLD_ERROR_CORRUPT. */
    int32_t (*add_layer)(void* context, const doodlebold_layer* layer);
    /* Something the reader skipped or changed, shown to the user once the file is open. */
    void (*note)(void* context, const char* text);
    /* What exactly went wrong, shown with the error read() returns. */
    void (*set_error)(void* context, const char* text);
} doodlebold_read_host;

/* What the program gives write(). Every function takes `context` as its first argument. */
typedef struct doodlebold_write_host {
    uint32_t struct_size;
    void* context;
    int32_t width, height;
    double ppi;
    uint8_t background[4];           /* RGBA; alpha 0: a transparent canvas */
    uint32_t layer_count;
    const doodlebold_layer* layers;  /* bottom to top, each folder before the layers inside it */

    /* Fill `out` with a layer's pixels (its width * height) or its mask (mask_width * mask_height). Return 1, or 0 on failure. */
    int (*get_pixels)(void* context, uint32_t layer, uint16_t* out, size_t stride);
    int (*get_mask)(void* context, uint32_t layer, uint16_t* out, size_t stride);
    /* The whole canvas as one picture (width * height): over transparency when the canvas is transparent, otherwise over the
     * background colour, as the canvas shows it. For formats with one picture. */
    int (*get_composite)(void* context, uint16_t* out, size_t stride);
    /* Append bytes to the file. Returns 1, or 0 on failure: return DOODLEBOLD_ERROR_WRITE_FAILED. */
    int (*write)(void* context, const void* bytes, size_t size);
    void (*set_error)(void* context, const char* text);
} doodlebold_write_host;

/* One file format. */
typedef struct doodlebold_format {
    uint32_t struct_size;
    const char* id;          /* unique and never changing, such as "farbfeld" */
    const char* name;        /* for the file dialogs, such as "Farbfeld image" */
    const char* extensions;  /* lower case without dots, separated by ';', the first the default: "ff" */
    uint32_t flags;          /* DOODLEBOLD_FORMAT_* */
    /* The first bytes of a file (up to 4096): 1 if they are this format, for files with a missing or wrong extension. May be
     * NULL. */
    int (*recognizes)(const uint8_t* data, size_t size);
    int (*read)(const doodlebold_read_host* host);    /* NULL: can't open */
    int (*write)(const doodlebold_write_host* host);  /* NULL: can't save */
} doodlebold_format;

/* What the plugin gives the program. It must stay valid while the plugin is loaded: a static const struct is right. */
typedef struct doodlebold_plugin {
    uint32_t struct_size;
    uint32_t api_version;  /* DOODLEBOLD_PLUGIN_API_VERSION, as the plugin was built */
    const char* name;      /* for notes, such as "Farbfeld" */
    uint32_t format_count;
    const doodlebold_format* formats;
} doodlebold_plugin;

/* The one function a plugin exports. `program_api_version` is the newest interface version the program knows; return NULL
 * to decline. */
typedef const doodlebold_plugin* (*doodlebold_plugin_entry_fn)(uint32_t program_api_version);
#define DOODLEBOLD_PLUGIN_ENTRY "doodlebold_plugin_entry"

#ifdef __cplusplus
}
#endif

#endif
