#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include "proximity_model.h"
#include "android_proximity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "Proximity"

struct glyph {
    char character;
    uint8_t rows[7];
};

static const struct glyph glyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
    {'1', {0x04, 0x0c, 0x14, 0x04, 0x04, 0x04, 0x1f}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
    {'3', {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
    {'5', {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}},
    {'6', {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
    {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
    {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e}},
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'C', {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f}},
    {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'G', {0x0f, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0f}},
    {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'I', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'a', {0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f}},
    {'d', {0x01, 0x01, 0x0f, 0x11, 0x11, 0x11, 0x0f}},
    {'e', {0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e}},
    {'g', {0x00, 0x00, 0x0f, 0x11, 0x0f, 0x01, 0x0e}},
    {'h', {0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x11}},
    {'i', {0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e}},
    {'m', {0x00, 0x00, 0x1a, 0x15, 0x15, 0x15, 0x15}},
    {'n', {0x00, 0x00, 0x1e, 0x11, 0x11, 0x11, 0x11}},
    {'o', {0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e}},
    {'p', {0x00, 0x00, 0x1e, 0x11, 0x1e, 0x10, 0x10}},
    {'r', {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},
    {'s', {0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e}},
    {'t', {0x04, 0x04, 0x1f, 0x04, 0x04, 0x04, 0x03}},
    {'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d}},
    {'y', {0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x0e}},
    {'N', {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
    {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
    {'c', {0x00, 0x00, 0x0f, 0x10, 0x10, 0x10, 0x0f}},
    {'?', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
};

struct proximity_state {
    struct android_app *app;
    struct android_proximity android_sensor;
    struct proximity_model model;
};

static const uint8_t *glyph_rows(char character)
{
    size_t count = sizeof(glyphs) / sizeof(glyphs[0]);
    for (size_t index = 0; index < count; ++index) {
        if (glyphs[index].character == character) {
            return glyphs[index].rows;
        }
    }
    return glyphs[count - 1U].rows;
}

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

static bool glyph_cell_on(const uint8_t *rows, int32_t row, int32_t column)
{
    if (row < 0 || row >= 7 || column < 0 || column >= 5) {
        return false;
    }
    uint8_t mask = (uint8_t)(1U << (unsigned int)(4 - column));
    return (rows[row] & mask) != 0U;
}

#define GLYPH_SUPERSAMPLE 4
#define GLYPH_CACHE_CAPACITY 96

struct glyph_mask_cache_entry {
    char character;
    int32_t scale;
    uint8_t *coverage;
};

static struct glyph_mask_cache_entry glyph_mask_cache[GLYPH_CACHE_CAPACITY];
static size_t glyph_cache_replacement = 0U;

static int32_t floor_divide(int32_t numerator, int32_t denominator)
{
    if (numerator >= 0) {
        return numerator / denominator;
    }
    return -((-numerator + denominator - 1) / denominator);
}

/*
 * Reconstruct one continuous glyph from the 5x7 samples, then sample that
 * shape on a 4x4 grid inside each framebuffer pixel. The interpolation
 * threshold is exactly 2/5: low enough to bridge diagonal bitmap samples
 * such as X without turning the one-cell strokes into solid blocks.
 */
static bool glyph_subsample_on(
    const uint8_t *rows,
    int32_t pixel_x,
    int32_t pixel_y,
    int32_t sub_x,
    int32_t sub_y,
    int32_t scale)
{
    int32_t denominator = 8 * scale;
    int32_t x_numerator =
        8 * pixel_x + 2 * sub_x + 1 - 4 * scale;
    int32_t y_numerator =
        8 * pixel_y + 2 * sub_y + 1 - 4 * scale;

    int32_t column = floor_divide(x_numerator, denominator);
    int32_t row = floor_divide(y_numerator, denominator);
    int32_t x_remainder = x_numerator - column * denominator;
    int32_t y_remainder = y_numerator - row * denominator;

    int32_t x0_weight = denominator - x_remainder;
    int32_t x1_weight = x_remainder;
    int32_t y0_weight = denominator - y_remainder;
    int32_t y1_weight = y_remainder;

    int32_t weighted = 0;
    if (glyph_cell_on(rows, row, column)) {
        weighted += x0_weight * y0_weight;
    }
    if (glyph_cell_on(rows, row, column + 1)) {
        weighted += x1_weight * y0_weight;
    }
    if (glyph_cell_on(rows, row + 1, column)) {
        weighted += x0_weight * y1_weight;
    }
    if (glyph_cell_on(rows, row + 1, column + 1)) {
        weighted += x1_weight * y1_weight;
    }

    int32_t full_weight = denominator * denominator;
    return 5 * weighted >= 2 * full_weight;
}

static uint8_t glyph_pixel_coverage(
    const uint8_t *rows,
    int32_t pixel_x,
    int32_t pixel_y,
    int32_t scale)
{
    int32_t covered = 0;
    for (int32_t sub_y = 0; sub_y < GLYPH_SUPERSAMPLE; ++sub_y) {
        for (int32_t sub_x = 0; sub_x < GLYPH_SUPERSAMPLE; ++sub_x) {
            if (glyph_subsample_on(
                    rows,
                    pixel_x,
                    pixel_y,
                    sub_x,
                    sub_y,
                    scale)) {
                covered += 1;
            }
        }
    }
    return (uint8_t)((covered * 255 + 8) / 16);
}

static uint8_t *build_glyph_mask(char character, int32_t scale)
{
    int32_t width = 5 * scale;
    int32_t height = 7 * scale;
    size_t size = (size_t)width * (size_t)height;
    uint8_t *coverage = (uint8_t *)malloc(size);
    if (coverage == NULL) {
        return NULL;
    }

    const uint8_t *rows = glyph_rows(character);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            coverage[(size_t)y * (size_t)width + (size_t)x] =
                glyph_pixel_coverage(rows, x, y, scale);
        }
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
    const uint8_t *rows = glyph_rows(character);

    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            uint8_t pixel_coverage =
                coverage != NULL
                    ? coverage[(size_t)y * (size_t)width + (size_t)x]
                    : glyph_pixel_coverage(rows, x, y, scale);
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
    uint32_t status_color = state->model.have_sample
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
        &buffer, "MAX RANGE", (73 * buffer.height) / 100, scale + 1, secondary);
    char maximum[64];
    if (available) {
        (void)snprintf(maximum, sizeof(maximum), "%.2f cm",
                       (double)android_proximity_maximum_range_cm(&state->android_sensor));
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
            &state->model, reading.timestamp_ns, reading.distance_cm,
            android_proximity_maximum_range_cm(&state->android_sensor));
        if (accepted != PROXIMITY_ACCEPTED) {
            __android_log_print(
                ANDROID_LOG_WARN, LOG_TAG,
                "ignored invalid proximity event (status=%d)", (int)accepted);
            continue;
        }
        __android_log_print(
            ANDROID_LOG_INFO, LOG_TAG,
            "timestamp_ns=%lld distance_cm=%.6g max_cm=%.6g state=%s transitions=%llu",
            (long long)state->model.timestamp_ns,
            (double)state->model.distance_cm,
            (double)state->model.maximum_cm,
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
