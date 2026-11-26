/*
 * Test plugins that misbehave, each built with one of these defined, for io::PluginHost's tests. Each reads ".misbehaving" files.
 *   CRASH  read() writes through a null pointer
 *   HANG   read() never returns
 *   PRY    read() prints to its standard output (which mustn't reach the messages), opens the file its bytes name (one of the
 *          user's, in the test), and notes "opened" or "refused"; then it gives a
 *          one-pixel picture, so the read itself succeeds when the restrictions refuse quietly
 */
#include <doodlebold/format_plugin.h>

#include <stdio.h>
#include <string.h>

static int read_misbehaving(const doodlebold_read_host* host) {
#if defined(CRASH)
    volatile int* nowhere = NULL;
    *nowhere = 1;
    (void)host;
    return DOODLEBOLD_ERROR_CORRUPT;
#elif defined(HANG)
    volatile unsigned long spins = 0;
    (void)host;
    for (;;)
        ++spins;
#else
    char path[1024];
    const size_t n = host->size < sizeof path - 1 ? host->size : sizeof path - 1;
    const uint8_t white[4] = {255, 255, 255, 255};
    const uint16_t pixel[4] = {0xFFFF, 0, 0, 0xFFFF};
    doodlebold_layer layer;
    FILE* file;
    printf("noise from the plugin\n");
    fflush(stdout);
    memcpy(path, host->data, n);
    path[n] = '\0';
    file = fopen(path, "rb");
    host->note(host->context, file ? "opened" : "refused");
    if (file)
        fclose(file);
    host->set_canvas(host->context, 1, 1, 0.0, white);
    memset(&layer, 0, sizeof layer);
    layer.struct_size = sizeof layer;
    layer.kind = DOODLEBOLD_LAYER_PIXELS;
    layer.parent = -1;
    layer.opacity = 100;
    layer.visible = 1;
    layer.width = layer.height = 1;
    layer.pixels = pixel;
    layer.stride = sizeof pixel;
    return host->add_layer(host->context, &layer) < 0 ? DOODLEBOLD_ERROR_CORRUPT : DOODLEBOLD_OK;
#endif
}

static const doodlebold_format formats[] = {
    {sizeof(doodlebold_format), "misbehaving", "Misbehaving test", "misbehaving", 0, NULL, read_misbehaving, NULL},
};

static const doodlebold_plugin plugin = {sizeof(doodlebold_plugin), DOODLEBOLD_PLUGIN_API_VERSION, "Misbehaving", 1, formats};

DOODLEBOLD_PLUGIN_EXPORT const doodlebold_plugin* doodlebold_plugin_entry(uint32_t program_api_version) {
    (void)program_api_version;
    return &plugin;
}
