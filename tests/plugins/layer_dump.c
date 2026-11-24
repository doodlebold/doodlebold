/*
 * A test plugin: ".layerdump" files hold everything the plugin interface carries, as the machine's own bytes, so a round trip
 * through it checks every field of the interface. Not a format for real use.
 *
 * The file: "LAYRDUMP", the canvas (width, height, ppi, background), the layer count, then each layer: its numbers, name and
 * blend key (each a length, then the bytes padded to 4), its pixels and its mask.
 */
#include <doodlebold/format_plugin.h>

#include <stdlib.h>
#include <string.h>

static const char magic[8] = {'L', 'A', 'Y', 'R', 'D', 'U', 'M', 'P'};

/* ---- Writing ---- */

static int put(const doodlebold_write_host* host, const void* data, size_t size) {
    return host->write(host->context, data, size);
}

static int put_i32(const doodlebold_write_host* host, int32_t v) {
    return put(host, &v, sizeof v);
}

/* Texts are padded to 4 bytes, so the pixels after them stay aligned. */
static size_t padded(size_t n) {
    return (n + 3) & ~(size_t)3;
}

static int put_text(const doodlebold_write_host* host, const char* text) {
    static const char zeros[4] = {0, 0, 0, 0};
    const int32_t n = text ? (int32_t)strlen(text) : 0;
    return put_i32(host, n) && put(host, text, (size_t)n) && put(host, zeros, padded((size_t)n) - (size_t)n);
}

static int put_numbers(const doodlebold_write_host* host, const doodlebold_layer* l) {
    const int32_t numbers[16] = {l->kind,   l->parent,     l->opacity,     l->visible,     l->clipping,     l->x,
                                 l->y,      l->width,      l->height,      l->has_mask,    l->mask_enabled, l->mask_x,
                                 l->mask_y, l->mask_width, l->mask_height, l->mask_outside};
    return put(host, numbers, sizeof numbers);
}

/* A plane of `width` x `height` values, `channels` per pixel, fetched with `get`. */
static int put_plane(const doodlebold_write_host* host, uint32_t index, int32_t width, int32_t height, size_t channels,
                     int (*get)(void*, uint32_t, uint16_t*, size_t)) {
    const size_t bytes = (size_t)width * (size_t)height * channels * sizeof(uint16_t);
    uint16_t* plane;
    int ok;
    if (bytes == 0)
        return 1;
    plane = (uint16_t*)malloc(bytes);
    ok = plane && get(host->context, index, plane, (size_t)width * channels * sizeof(uint16_t)) && put(host, plane, bytes);
    free(plane);
    return ok;
}

static int put_layer(const doodlebold_write_host* host, uint32_t i) {
    const doodlebold_layer* l = &host->layers[i];
    if (!put_numbers(host, l) || !put_text(host, l->name) || !put_text(host, l->blend_mode))
        return 0;
    if (!put_plane(host, i, l->width, l->height, 4, host->get_pixels))
        return 0;
    return !l->has_mask || put_plane(host, i, l->mask_width, l->mask_height, 1, host->get_mask);
}

static int dump_write(const doodlebold_write_host* host) {
    uint32_t i;
    if (!put(host, magic, sizeof magic) || !put_i32(host, host->width) || !put_i32(host, host->height) ||
        !put(host, &host->ppi, sizeof host->ppi) || !put(host, host->background, 4) || !put_i32(host, (int32_t)host->layer_count))
        return DOODLEBOLD_ERROR_WRITE_FAILED;
    for (i = 0; i < host->layer_count; ++i)
        if (!put_layer(host, i))
            return DOODLEBOLD_ERROR_WRITE_FAILED;
    return DOODLEBOLD_OK;
}

/* ---- Reading ---- */

typedef struct cursor {
    const uint8_t* at;
    size_t left;
} cursor;

static const void* take(cursor* c, size_t size) {
    const void* p = c->at;
    if (size > c->left)
        return NULL;
    c->at += size;
    c->left -= size;
    return p;
}

static int take_i32(cursor* c, int32_t* out) {
    const void* p = take(c, sizeof *out);
    if (p)
        memcpy(out, p, sizeof *out);
    return p != NULL;
}

/* A length and its bytes, as a new 0-terminated string; NULL when the file ends. */
static char* take_text(cursor* c) {
    int32_t n;
    const void* p;
    char* text;
    if (!take_i32(c, &n) || n < 0 || !(p = take(c, padded((size_t)n))))
        return NULL;
    text = (char*)malloc((size_t)n + 1);
    if (text) {
        memcpy(text, p, (size_t)n);
        text[n] = 0;
    }
    return text;
}

static const uint16_t* take_plane(cursor* c, int32_t width, int32_t height, size_t channels) {
    if (width < 0 || height < 0)
        return NULL;
    return (const uint16_t*)take(c, (size_t)width * (size_t)height * channels * sizeof(uint16_t));
}

static int read_layer(const doodlebold_read_host* host, cursor* c) {
    int32_t n[16];
    int i, result = DOODLEBOLD_ERROR_TRUNCATED;
    doodlebold_layer l;
    char *name, *blend = NULL;
    for (i = 0; i < 16; ++i)
        if (!take_i32(c, &n[i]))
            return DOODLEBOLD_ERROR_TRUNCATED;
    memset(&l, 0, sizeof l);
    l.struct_size = sizeof l;
    l.kind = n[0], l.parent = n[1], l.opacity = n[2], l.visible = n[3], l.clipping = n[4];
    l.x = n[5], l.y = n[6], l.width = n[7], l.height = n[8];
    l.has_mask = n[9], l.mask_enabled = n[10], l.mask_x = n[11], l.mask_y = n[12], l.mask_width = n[13], l.mask_height = n[14];
    l.mask_outside = (uint16_t)n[15];
    name = take_text(c);
    if (name && (blend = take_text(c))) {
        l.name = name;
        l.blend_mode = blend;
        l.pixels = take_plane(c, l.width, l.height, 4);
        l.stride = (size_t)l.width * 8;
        l.mask = l.has_mask ? take_plane(c, l.mask_width, l.mask_height, 1) : NULL;
        l.mask_stride = (size_t)l.mask_width * 2;
        if ((l.pixels || l.width * l.height == 0) && (!l.has_mask || l.mask))
            result = host->add_layer(host->context, &l) < 0 ? DOODLEBOLD_ERROR_CORRUPT : DOODLEBOLD_OK;
    }
    free(name);
    free(blend);
    return result;
}

static int dump_read(const doodlebold_read_host* host) {
    cursor c = {host->data, host->size};
    const void* head = take(&c, sizeof magic);
    const void* ppi;
    const uint8_t* background;
    int32_t width, height, count, i;
    double ppi_value;
    if (!head || memcmp(head, magic, sizeof magic) != 0)
        return DOODLEBOLD_ERROR_NOT_THIS_FORMAT;
    if (!take_i32(&c, &width) || !take_i32(&c, &height) || !(ppi = take(&c, sizeof(double))) ||
        !(background = (const uint8_t*)take(&c, 4)) || !take_i32(&c, &count))
        return DOODLEBOLD_ERROR_TRUNCATED;
    memcpy(&ppi_value, ppi, sizeof ppi_value);
    if (!host->set_canvas(host->context, width, height, ppi_value, background))
        return DOODLEBOLD_ERROR_TOO_LARGE;
    for (i = 0; i < count; ++i) {
        const int result = read_layer(host, &c);
        if (result != DOODLEBOLD_OK)
            return result;
    }
    host->note(host->context, "read by the layer dump plugin");
    return DOODLEBOLD_OK;
}

static int dump_recognizes(const uint8_t* data, size_t size) {
    return size >= sizeof magic && memcmp(data, magic, sizeof magic) == 0;
}

static const doodlebold_format formats[] = {{
    sizeof(doodlebold_format),
    "layerdump",
    "Layer dump (test)",
    "layerdump",
    DOODLEBOLD_FORMAT_LAYERS | DOODLEBOLD_FORMAT_ALPHA,
    dump_recognizes,
    dump_read,
    dump_write,
}};

static const doodlebold_plugin plugin = {sizeof(doodlebold_plugin), DOODLEBOLD_PLUGIN_API_VERSION, "Layer dump", 1, formats};

DOODLEBOLD_PLUGIN_EXPORT const doodlebold_plugin* doodlebold_plugin_entry(uint32_t program_api_version) {
    (void)program_api_version;
    return &plugin;
}
