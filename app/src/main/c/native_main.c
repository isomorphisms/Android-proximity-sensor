#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include "proximity_model.h"
#include "android_proximity.h"

#include "subpixel/font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "Proximity"

struct proximity_state {
    struct android_app *app;
    struct android_proximity android_sensor;
    struct proximity_model model;
};

static void put_pixel(ANativeWindow_Buffer *buffer, int32_t x, int32_t y, uint32_t value)
{
    if (x < 0 || y < 0 || x >= buffer->width || y >= buffer->height) {
        return;
    }
    uint32_t *pixels = (uint32_t *)buffer->bits;
    size_t offset = (size_t)y * (size_t)buffer->stride + (size_t)x;
    pixels[offset] = value;
}

static void blend_pixel(
    ANativeWindow_Buffer *buffer,
    int32_t x,
    int32_t y,
    uint32_t value,
    uint8_t coverage)
{
    if (x < 0 || y < 0 || x >= buffer->width || y >= buffer->height || coverage == 0U) {
        return;
    }

    uint32_t *pixels = (uint32_t *)buffer->bits;
    size_t offset = (size_t)y * (size_t)buffer->stride + (size_t)x;
    if (coverage == 255U) {
        pixels[offset] = value;
        return;
    }

    uint32_t under = pixels[offset];
    uint32_t inverse = 255U - (uint32_t)coverage;
    uint32_t mixed = 0U;
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        uint32_t under_channel = (under >> shift) & 0xffU;
        uint32_t value_channel = (value >> shift) & 0xffU;
        uint32_t channel =
            (under_channel * inverse + value_channel * (uint32_t)coverage + 127U) / 255U;
        mixed |= channel << shift;
    }
    pixels[offset] = mixed;
}

#define GLYPH_CACHE_CAPACITY 96

struct glyph_mask_cache_entry {
    char character;
    int32_t scale;
    uint8_t *coverage;
};

static struct glyph_mask_cache_entry glyph_mask_cache[GLYPH_CACHE_CAPACITY];
static size_t glyph_cache_replacement = 0U;

/* The cache owns masks; all geometry and sampling belong to Subpixel. */
static uint8_t *build_glyph_mask(char character, int32_t scale)
{
    if (scale < 1 || scale > 20) return NULL;
    size_t size = 35U * (size_t)scale * (size_t)scale;
    uint8_t *coverage = (uint8_t *)malloc(size);
    if (coverage == NULL) return NULL;
    if (!sf57_render_coverage((unsigned char)character, (unsigned)scale,
            0.0, 0.0, coverage, size)) {
        free(coverage);
        return NULL;
    }
    return coverage;
}

static const uint8_t *cached_glyph_mask(char character, int32_t scale)
{
    size_t empty = GLYPH_CACHE_CAPACITY;
    for (size_t index = 0U; index < GLYPH_CACHE_CAPACITY; ++index) {
        if (glyph_mask_cache[index].coverage == NULL) {
            if (empty == GLYPH_CACHE_CAPACITY) {
                empty = index;
            }
            continue;
        }
        if (glyph_mask_cache[index].character == character &&
            glyph_mask_cache[index].scale == scale) {
            return glyph_mask_cache[index].coverage;
        }
    }

    size_t slot = empty;
    if (slot == GLYPH_CACHE_CAPACITY) {
        slot = glyph_cache_replacement;
        glyph_cache_replacement =
            (glyph_cache_replacement + 1U) % GLYPH_CACHE_CAPACITY;
        free(glyph_mask_cache[slot].coverage);
        glyph_mask_cache[slot].coverage = NULL;
    }

    uint8_t *coverage = build_glyph_mask(character, scale);
    if (coverage == NULL) {
        return NULL;
    }

    glyph_mask_cache[slot].character = character;
    glyph_mask_cache[slot].scale = scale;
    glyph_mask_cache[slot].coverage = coverage;
    return coverage;
}

static void clear_glyph_mask_cache(void)
{
    for (size_t index = 0U; index < GLYPH_CACHE_CAPACITY; ++index) {
        free(glyph_mask_cache[index].coverage);
        glyph_mask_cache[index].coverage = NULL;
    }
}

static void fill_rect(
    ANativeWindow_Buffer *buffer,
    int32_t left,
    int32_t top,
    int32_t width,
    int32_t height,
    uint32_t value)
{
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            put_pixel(buffer, left + x, top + y, value);
        }
    }
}

static void draw_glyph(
    ANativeWindow_Buffer *buffer,
    char character,
    int32_t left,
    int32_t top,
    int32_t scale,
    uint32_t value)
{
    if (character == ' ') {
        return;
    }

    int32_t width = 5 * scale;
    int32_t height = 7 * scale;
    const uint8_t *coverage = cached_glyph_mask(character, scale);
    if (coverage == NULL) return;

    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            uint8_t pixel_coverage = coverage[(size_t)y * (size_t)width + (size_t)x];
            if (pixel_coverage == 0U) {
                continue;
            }
            blend_pixel(
                buffer,
                left + x,
                top + y,
                value,
                pixel_coverage);
        }
    }
}

static void draw_text(
    ANativeWindow_Buffer *buffer,
    const char *text,
    int32_t left,
    int32_t top,
    int32_t scale,
    uint32_t value)
{
    int32_t x = left;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        draw_glyph(buffer, *cursor, x, top, scale, value);
        x += 6 * scale;
    }
}

static int32_t text_width(const char *text, int32_t scale)
{
    size_t count = strlen(text);
    if (count == 0U) {
        return 0;
    }
    return ((int32_t)count * 6 - 1) * scale;
}

static void draw_text_centered(
    ANativeWindow_Buffer *buffer,
    const char *text,
    int32_t top,
    int32_t scale,
    uint32_t value)
{
    int32_t left = (buffer->width - text_width(text, scale)) / 2;
    draw_text(buffer, text, left, top, scale, value);
}


static void clear_buffer(ANativeWindow_Buffer *buffer, uint32_t value)
{
    uint32_t *pixels = (uint32_t *)buffer->bits;
    for (int32_t y = 0; y < buffer->height; ++y) {
        for (int32_t x = 0; x < buffer->width; ++x) {
            size_t offset = (size_t)y * (size_t)buffer->stride + (size_t)x;
            pixels[offset] = value;
        }
    }
}

static void draw_centered(
    ANativeWindow_Buffer *buffer,
    const char *message,
    int32_t y,
    int32_t preferred_scale,
    uint32_t color)
{
    int32_t limit = (buffer->width - 24) / (int32_t)(6U * strlen(message));
    if (limit < 1) {
        limit = 1;
    }
    int32_t scale = preferred_scale < limit ? preferred_scale : limit;
    draw_text_centered(buffer, message, y, scale, color);
}

static void draw_screen(struct proximity_state *state)
{
    if (state->app->window == NULL) {
        return;
    }
    (void)ANativeWindow_setBuffersGeometry(
        state->app->window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(state->app->window, &buffer, NULL) != 0) {
        return;
    }

    const uint32_t background = 0xff181818U;
    const uint32_t primary = 0xffeeeeeeU;
    const uint32_t secondary = 0xffb0b0b0U;
    const uint32_t near_color = 0xff40c0ffU;
    const uint32_t far_color = 0xff80d0a0U;
    const uint32_t track = 0xff404040U;
    clear_buffer(&buffer, background);

    int32_t scale = buffer.width / 170;
    if (scale < 2) { scale = 2; }
    if (scale > 6) { scale = 6; }
    int32_t title_scale = scale + 3;
    int32_t value_scale = scale + 4;
    int32_t status_scale = scale * 3;

    draw_centered(&buffer, "PROXIMITY", buffer.height / 10, title_scale, primary);

    bool available = android_proximity_is_available(&state->android_sensor);
    const char *status = !available ? "NO SENSOR" : proximity_state_name(&state->model);
    uint32_t status_color = state->model.have_two_levels
        ? (state->model.near ? near_color : far_color)
        : primary;
    draw_centered(
        &buffer, status, (31 * buffer.height) / 100, status_scale, status_color);

    fill_rect(
        &buffer, 3 * scale, (46 * buffer.height) / 100,
        buffer.width - 6 * scale, scale, track);

    draw_centered(
        &buffer, "DISTANCE", (53 * buffer.height) / 100, scale + 1, secondary);
    char distance[64];
    if (state->model.have_sample) {
        (void)snprintf(distance, sizeof(distance), "%.2f cm",
                       (double)state->model.distance_cm);
    } else {
        (void)snprintf(distance, sizeof(distance), "--");
    }
    draw_centered(
        &buffer, distance, (61 * buffer.height) / 100, value_scale, primary);

    draw_centered(
        &buffer, "SEEN HIGH", (73 * buffer.height) / 100, scale + 1, secondary);
    char maximum[64];
    if (state->model.have_sample) {
        (void)snprintf(maximum, sizeof(maximum), "%.2f cm",
                       (double)state->model.maximum_observed_cm);
    } else {
        (void)snprintf(maximum, sizeof(maximum), "--");
    }
    draw_centered(
        &buffer, maximum, (80 * buffer.height) / 100, scale + 2, primary);

    char events[64];
    (void)snprintf(events, sizeof(events), "EVENTS %llu",
                   (unsigned long long)state->model.event_count);
    draw_centered(
        &buffer, events, (91 * buffer.height) / 100, scale, secondary);
    (void)ANativeWindow_unlockAndPost(state->app->window);
}

static void set_sensor_enabled(struct proximity_state *state, bool enabled)
{
    if (!enabled) {
        android_proximity_disable(&state->android_sensor);
        return;
    }
    enum android_proximity_status status =
        android_proximity_enable(&state->android_sensor);
    if (status != ANDROID_PROXIMITY_OK) {
        __android_log_print(
            ANDROID_LOG_ERROR, LOG_TAG, "%s", android_proximity_status_text(status));
    }
}

static void handle_app_command(struct android_app *app, int32_t command)
{
    struct proximity_state *state = (struct proximity_state *)app->userData;
    switch (command) {
        case APP_CMD_INIT_WINDOW:
            draw_screen(state);
            break;
        case APP_CMD_GAINED_FOCUS:
            set_sensor_enabled(state, true);
            draw_screen(state);
            break;
        case APP_CMD_LOST_FOCUS:
        case APP_CMD_PAUSE:
            set_sensor_enabled(state, false);
            break;
        default:
            break;
    }
}

static void consume_sensor_events(struct proximity_state *state)
{
    if (!android_proximity_is_available(&state->android_sensor)) {
        return;
    }
    for (;;) {
        struct android_proximity_reading reading;
        enum android_proximity_read_result read_result =
            android_proximity_next(&state->android_sensor, &reading);
        if (read_result == ANDROID_PROXIMITY_READ_EMPTY) {
            break;
        }
        if (read_result == ANDROID_PROXIMITY_READ_ERROR) {
            __android_log_print(
                ANDROID_LOG_ERROR, LOG_TAG, "could not read a proximity event");
            break;
        }
        enum proximity_accept_result accepted = proximity_model_accept(
            &state->model, reading.timestamp_ns, reading.distance_cm);
        if (accepted != PROXIMITY_ACCEPTED) {
            __android_log_print(
                ANDROID_LOG_WARN, LOG_TAG,
                "ignored invalid proximity event (status=%d)", (int)accepted);
            continue;
        }
        __android_log_print(
            ANDROID_LOG_INFO, LOG_TAG,
            "timestamp_ns=%lld distance_cm=%.6g observed_high_cm=%.6g state=%s transitions=%llu",
            (long long)state->model.timestamp_ns,
            (double)state->model.distance_cm,
            (double)state->model.maximum_observed_cm,
            proximity_state_name(&state->model),
            (unsigned long long)state->model.transition_count);
    }
    draw_screen(state);
}

void android_main(struct android_app *app)
{
    struct proximity_state state;
    (void)memset(&state, 0, sizeof(state));
    proximity_model_initialize(&state.model);
    state.app = app;
    app->userData = &state;
    app->onAppCmd = handle_app_command;

    enum android_proximity_status status =
        android_proximity_open(
            &state.android_sensor, app->looper, LOOPER_ID_USER,
            "org.isomorphisms.proximity");
    if (status != ANDROID_PROXIMITY_OK) {
        __android_log_print(
            ANDROID_LOG_ERROR, LOG_TAG, "%s", android_proximity_status_text(status));
    }
    for (;;) {
        int events = 0;
        struct android_poll_source *source = NULL;
        int ident = ALooper_pollOnce(
            android_proximity_is_enabled(&state.android_sensor) ? -1 : 100,
            NULL, &events, (void **)&source);
        if (source != NULL) {
            source->process(app, source);
        }
        if (ident == LOOPER_ID_USER) {
            consume_sensor_events(&state);
        }
        if (app->destroyRequested != 0) {
            break;
        }
    }

    set_sensor_enabled(&state, false);
    android_proximity_close(&state.android_sensor);
    clear_glyph_mask_cache();
}
