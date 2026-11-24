/*
 * An example Doodlebold format plugin: farbfeld pictures (.ff), in plain C.
 *
 * Farbfeld, from its public specification (https://tools.suckless.org/farbfeld/): the 8 bytes "farbfeld", the width and
 * height as 32-bit big-endian numbers, then every pixel row by row as four 16-bit big-endian numbers, R, G, B and A, with
 * straight alpha. One picture, no layers: opening gives one layer, saving writes the flattened canvas.
 */
#include <doodlebold/format_plugin.h>

#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 16
#define MAX_SIDE 20000u

static const uint8_t magic[8] = {'f', 'a', 'r', 'b', 'f', 'e', 'l', 'd'};

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int ff_recognizes(const uint8_t* data, size_t size) {
    return size >= sizeof magic && memcmp(data, magic, sizeof magic) == 0;
}

/* The file's big-endian channels into the machine's own 16-bit values. */
static void decode_pixels(const uint8_t* in, uint16_t* out, size_t count) {
    size_t i;
    for (i = 0; i < count; ++i)
        out[i] = (uint16_t)((in[2 * i] << 8) | in[2 * i + 1]);
}

static int ff_read(const doodlebold_read_host* host) {
    static const uint8_t transparent[4] = {0, 0, 0, 0};
    uint32_t width, height;
    size_t channels;
    uint16_t* pixels;
    doodlebold_layer layer;
    int32_t added;

    if (host->size < HEADER_SIZE || !ff_recognizes(host->data, host->size))
        return DOODLEBOLD_ERROR_NOT_THIS_FORMAT;
    width = read_be32(host->data + 8);
    height = read_be32(host->data + 12);
    if (width == 0 || height == 0 || width > MAX_SIDE || height > MAX_SIDE)
        return DOODLEBOLD_ERROR_TOO_LARGE;
    channels = (size_t)width * height * 4;
    if (host->size - HEADER_SIZE < channels * 2) {
        host->set_error(host->context, "(pixels)");
        return DOODLEBOLD_ERROR_TRUNCATED;
    }
    if (!host->set_canvas(host->context, (int32_t)width, (int32_t)height, 0.0, transparent))
        return DOODLEBOLD_ERROR_TOO_LARGE;

    pixels = (uint16_t*)malloc(channels * sizeof *pixels);
    if (!pixels)
        return DOODLEBOLD_ERROR_OUT_OF_MEMORY;
    decode_pixels(host->data + HEADER_SIZE, pixels, channels);

    memset(&layer, 0, sizeof layer);
    layer.struct_size = sizeof layer;
    layer.kind = DOODLEBOLD_LAYER_PIXELS;
    layer.parent = -1;
    layer.opacity = 100;
    layer.visible = 1;
    layer.width = (int32_t)width;
    layer.height = (int32_t)height;
    layer.pixels = pixels;
    layer.stride = (size_t)width * 4 * sizeof *pixels;
    added = host->add_layer(host->context, &layer);
    free(pixels);
    return added < 0 ? DOODLEBOLD_ERROR_CORRUPT : DOODLEBOLD_OK;
}

/* One row of the machine's 16-bit values as the file's big-endian bytes. */
static void encode_row(const uint16_t* in, uint8_t* out, size_t count) {
    size_t i;
    for (i = 0; i < count; ++i) {
        out[2 * i] = (uint8_t)(in[i] >> 8);
        out[2 * i + 1] = (uint8_t)in[i];
    }
}

static int ff_write(const doodlebold_write_host* host) {
    uint8_t header[HEADER_SIZE];
    const size_t row_channels = (size_t)host->width * 4;
    const size_t stride = row_channels * sizeof(uint16_t);
    uint16_t* pixels;
    uint8_t* row;
    int32_t y;
    int result = DOODLEBOLD_OK;

    memcpy(header, magic, sizeof magic);
    write_be32(header + 8, (uint32_t)host->width);
    write_be32(header + 12, (uint32_t)host->height);
    if (!host->write(host->context, header, sizeof header))
        return DOODLEBOLD_ERROR_WRITE_FAILED;

    pixels = (uint16_t*)malloc(stride * (size_t)host->height);
    row = (uint8_t*)malloc(stride);
    if (!pixels || !row) {
        free(pixels);
        free(row);
        return DOODLEBOLD_ERROR_OUT_OF_MEMORY;
    }
    if (!host->get_composite(host->context, pixels, stride))
        result = DOODLEBOLD_ERROR_WRITE_FAILED;
    for (y = 0; result == DOODLEBOLD_OK && y < host->height; ++y) {
        encode_row(pixels + (size_t)y * row_channels, row, row_channels);
        if (!host->write(host->context, row, stride))
            result = DOODLEBOLD_ERROR_WRITE_FAILED;
    }
    free(pixels);
    free(row);
    return result;
}

static const doodlebold_format formats[] = {{
    sizeof(doodlebold_format),
    "farbfeld",
    "Farbfeld image",
    "ff",
    DOODLEBOLD_FORMAT_ALPHA,
    ff_recognizes,
    ff_read,
    ff_write,
}};

static const doodlebold_plugin plugin = {
    sizeof(doodlebold_plugin), DOODLEBOLD_PLUGIN_API_VERSION, "Farbfeld", 1, formats,
};

DOODLEBOLD_PLUGIN_EXPORT const doodlebold_plugin* doodlebold_plugin_entry(uint32_t program_api_version) {
    return program_api_version >= 1 ? &plugin : NULL;
}
