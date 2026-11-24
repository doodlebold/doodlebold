/*
 * Test plugins that the program must skip or limit, one per build, chosen by a define:
 *   NO_ENTRY        a library without the entry function
 *   FUTURE_VERSION  built for an interface version the program doesn't know
 *   DECLINES        the entry function returns NULL
 *   BROKEN_FORMATS  one format without an id, one that can neither open nor save
 *   CLAIMS          one format claiming .png (a built-in format's) and .claimtest
 */
#include <doodlebold/format_plugin.h>

#include <stddef.h>

static int read_one_pixel(const doodlebold_read_host* host) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint16_t pixel[4] = {65535, 0, 0, 65535};
    doodlebold_layer l = {0};
    if (!host->set_canvas(host->context, 1, 1, 0.0, white))
        return DOODLEBOLD_ERROR_TOO_LARGE;
    l.struct_size = sizeof l;
    l.parent = -1;
    l.opacity = 100;
    l.visible = 1;
    l.width = l.height = 1;
    l.pixels = pixel;
    l.stride = sizeof pixel;
    return host->add_layer(host->context, &l) < 0 ? DOODLEBOLD_ERROR_CORRUPT : DOODLEBOLD_OK;
}

#ifdef NO_ENTRY

DOODLEBOLD_PLUGIN_EXPORT int not_the_entry(void) {
    return read_one_pixel(NULL);
}

#else

#ifdef BROKEN_FORMATS
static const doodlebold_format formats[] = {
    {sizeof(doodlebold_format), NULL, "No id", "noid", 0, NULL, read_one_pixel, NULL},
    {sizeof(doodlebold_format), "nothing", "Does nothing", "nothing", 0, NULL, NULL, NULL},
};
#else
static const doodlebold_format formats[] = {
    {sizeof(doodlebold_format), "claims", "Claims test", "png;claimtest", 0, NULL, read_one_pixel, NULL},
};
#endif

#ifdef FUTURE_VERSION
#define VERSION (DOODLEBOLD_PLUGIN_API_VERSION + 1)
#else
#define VERSION DOODLEBOLD_PLUGIN_API_VERSION
#endif

static const doodlebold_plugin plugin = {
    sizeof(doodlebold_plugin), VERSION, "Bad", sizeof formats / sizeof formats[0], formats,
};

DOODLEBOLD_PLUGIN_EXPORT const doodlebold_plugin* doodlebold_plugin_entry(uint32_t program_api_version) {
#ifndef DECLINES
    (void)program_api_version;
#endif
#ifdef DECLINES
    return program_api_version == 0 ? &plugin : NULL;
#else
    return &plugin;
#endif
}

#endif
