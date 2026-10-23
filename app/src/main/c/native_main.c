#include <jni.h>
#include <android/looper.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/sensor.h>
#include <android_native_app_glue.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "model.h"

#define PACKAGE_NAME "org.isomorphisms.proximity"
#define LOG_TAG "ProximityReader"

struct glyph { char c; uint8_t row[7]; };
static const struct glyph font[] = {
    {' ',{0,0,0,0,0,0,0}}, {'+', {0,4,4,31,4,4,0}},
    {'-',{0,0,0,31,0,0,0}}, {'.',{0,0,0,0,0,12,12}},
    {':',{0,4,4,0,4,4,0}}, {'/',{1,2,2,4,8,8,16}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,20,4,4,4,31}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{15,16,16,16,16,16,15}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{15,16,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{31,4,4,4,4,4,31}}, {'J',{1,1,1,1,17,17,14}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,25,21,19,19,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'?',{14,17,1,2,4,0,4}}
};

struct proximity_app {
    struct android_app *app;
    ASensorManager *manager;
    const ASensor *sensor;
    ASensorEventQueue *queue;
    struct proximity_model model;
    bool enabled;
    bool read_error;
};

static char uppercase(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static const uint8_t *glyph_rows(char c)
{
    c = uppercase(c);
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); ++i) {
        if (font[i].c == c) return font[i].row;
    }
    return font[sizeof(font) / sizeof(font[0]) - 1].row;
}

static void rect(ANativeWindow_Buffer *b, int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    int x0 = x > 0 ? x : 0;
    int y0 = y > 0 ? y : 0;
    int x1 = x + w < b->width ? x + w : b->width;
    int y1 = y + h < b->height ? y + h : b->height;
    uint32_t *pixels = (uint32_t *)b->bits;
    for (int row = y0; row < y1; ++row) {
        for (int col = x0; col < x1; ++col) {
            pixels[(size_t)row * (size_t)b->stride + (size_t)col] = color;
        }
    }
}

static int text_width(const char *text, int scale)
{
    size_t n = strlen(text);
    return n == 0 ? 0 : (int)((6 * n - 1) * (size_t)scale);
}

static int fit_scale(const char *text, int desired, int available_width)
{
    int scale = desired;
    while (scale > 1 && text_width(text, scale) > available_width) --scale;
    return scale;
}

static void glyph(ANativeWindow_Buffer *b, char ch, int x, int y, int scale, uint32_t color)
{
    const uint8_t *rows = glyph_rows(ch);
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if ((rows[row] & (1u << (4 - col))) != 0) {
                rect(b, x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(ANativeWindow_Buffer *b, const char *text,
                      int x, int y, int scale, uint32_t color)
{
    for (const char *p = text; *p; ++p) {
        glyph(b, *p, x, y, scale, color);
        x += 6 * scale;
    }
}

static void draw_centered(ANativeWindow_Buffer *b, const char *text,
                          int y, int desired_scale, uint32_t color)
{
    int scale = fit_scale(text, desired_scale, b->width - b->width / 10);
    draw_text(b, text, (b->width - text_width(text, scale)) / 2, y, scale, color);
}

static void render(struct proximity_app *s)
{
    if (!s->app->window) return;
    ANativeWindow_setBuffersGeometry(s->app->window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer b;
    if (ANativeWindow_lock(s->app->window, &b, NULL) != 0) return;

    const uint32_t background = 0xff161b20u;
    const uint32_t foreground = 0xfff2f2f2u;
    const uint32_t muted = 0xffa4b0b8u;
    const uint32_t near_color = 0xff79d4a4u;
    const uint32_t far_color = 0xffa8c8e7u;
    rect(&b, 0, 0, b.width, b.height, background);

    int w = b.width, h = b.height;
    int small = w / 230;
    if (small < 2) small = 2;
    if (small > 4) small = 4;
    int medium = w / 120;
    if (medium < 3) medium = 3;
    if (medium > 7) medium = 7;

    draw_centered(&b, "PROXIMITY", h * 7 / 100, medium + 1, foreground);
    draw_centered(&b, "MOVE HAND OVER TOP OF PHONE", h * 16 / 100, small, muted);

    const char *state_label = "WAITING";
    uint32_t state_color = foreground;
    if (!s->sensor) {
        state_label = "NO SENSOR";
    } else if (s->read_error) {
        state_label = "SENSOR ERROR";
    } else if (s->model.has_reading) {
        state_label = s->model.relation == PROXIMITY_NEAR ? "NEAR" : "FAR";
        state_color = s->model.relation == PROXIMITY_NEAR ? near_color : far_color;
    }
    int large_scale = w / 29;
    if (large_scale > 24) large_scale = 24;
    if (large_scale < 8) large_scale = 8;
    draw_centered(&b, state_label, h * 26 / 100, large_scale, state_color);

    char line[128];
    if (s->model.has_reading) {
        snprintf(line, sizeof(line), "REPORTED %.2f CM", (double)s->model.reported_cm);
    } else {
        snprintf(line, sizeof(line), "AWAITING FIRST EVENT");
    }
    draw_centered(&b, line, h * 43 / 100, medium, foreground);

    if (s->sensor) {
        snprintf(line, sizeof(line), "MAX RANGE %.2f CM", (double)s->model.maximum_cm);
        draw_centered(&b, line, h * 50 / 100, small, muted);
        const char *name = ASensor_getName(s->sensor);
        const char *vendor = ASensor_getVendor(s->sensor);
        snprintf(line, sizeof(line), "SENSOR %.30s", name ? name : "UNKNOWN");
        draw_centered(&b, line, h * 55 / 100, small, muted);
        snprintf(line, sizeof(line), "VENDOR %.24s", vendor ? vendor : "UNKNOWN");
        draw_centered(&b, line, h * 59 / 100, small, muted);
    }

    rect(&b, w / 12, h * 65 / 100, w * 5 / 6, 2, muted);
    snprintf(line, sizeof(line), "CHANGES %u", (unsigned)s->model.changes);
    draw_text(&b, line, w / 12, h * 68 / 100, small, foreground);
    int top = h * 73 / 100;
    int row_pitch = h * 25 / 100 / PROXIMITY_HISTORY_SIZE;
    if (row_pitch < 7 * small + 2) row_pitch = 7 * small + 2;
    for (unsigned i = 0; i < s->model.transition_count; ++i) {
        unsigned pos = (s->model.next_transition + PROXIMITY_HISTORY_SIZE - 1u - i)
                        % PROXIMITY_HISTORY_SIZE;
        struct proximity_transition *t = &s->model.transitions[pos];
        double seconds = (double)(t->timestamp_ns - s->model.first_timestamp_ns) / 1000000000.0;
        snprintf(line, sizeof(line), "+%.1fS %s %.2fCM", seconds,
                 t->relation == PROXIMITY_NEAR ? "NEAR" : "FAR", (double)t->reported_cm);
        draw_text(&b, line, w / 12, top + (int)i * row_pitch, small, muted);
    }
    ANativeWindow_unlockAndPost(s->app->window);
}

static void set_enabled(struct proximity_app *s, bool enabled)
{
    if (!s->queue || !s->sensor) return;
    if (enabled && !s->enabled) {
        if (ASensorEventQueue_enableSensor(s->queue, s->sensor) < 0) {
            s->read_error = true;
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Unable to enable sensor");
        } else {
            s->enabled = true;
        }
    } else if (!enabled && s->enabled) {
        ASensorEventQueue_disableSensor(s->queue, s->sensor);
        s->enabled = false;
    }
}

static void command(struct android_app *app, int32_t cmd)
{
    struct proximity_app *s = (struct proximity_app *)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
        case APP_CMD_WINDOW_RESIZED:
            render(s);
            break;
        case APP_CMD_GAINED_FOCUS:
            set_enabled(s, true);
            render(s);
            break;
        case APP_CMD_LOST_FOCUS:
        case APP_CMD_PAUSE:
            set_enabled(s, false);
            break;
        default:
            break;
    }
}

static void read_events(struct proximity_app *s)
{
    if (!s->queue) return;
    ASensorEvent events[16];
    ssize_t n;
    bool redrawn = false;
    while ((n = ASensorEventQueue_getEvents(s->queue, events, 16)) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            if (events[i].type != ASENSOR_TYPE_PROXIMITY) continue;
            enum proximity_relation previous = s->model.relation;
            if (proximity_model_accept(&s->model, events[i].distance, events[i].timestamp)) {
                redrawn = true;
                if (previous != s->model.relation) {
                    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s %.3f cm",
                        s->model.relation == PROXIMITY_NEAR ? "NEAR" : "FAR",
                        (double)s->model.reported_cm);
                }
            }
        }
    }
    if (n < 0) {
        s->read_error = true;
        redrawn = true;
    }
    if (redrawn) render(s);
}

/*
 * Android NDK exposes proximity samples but not ASensor_getMaxRange().
 * Read only this one metadata number through the system Sensor object.
 * The application has no Java/Kotlin source, and all event acquisition
 * remains in the NDK.
 */
static float android_maximum_range(struct android_app *app)
{
    JNIEnv *env = NULL;
    JavaVM *vm = app->activity->vm;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return 0.0f;

    float range = 0.0f;
    jclass activity_class = (*env)->GetObjectClass(env, app->activity->clazz);
    jmethodID get_service = activity_class ?
        (*env)->GetMethodID(env, activity_class, "getSystemService",
                            "(Ljava/lang/String;)Ljava/lang/Object;") : NULL;
    jstring sensor_service = get_service ? (*env)->NewStringUTF(env, "sensor") : NULL;
    jobject service = sensor_service ?
        (*env)->CallObjectMethod(env, app->activity->clazz, get_service, sensor_service) : NULL;
    jclass manager_class = service ? (*env)->GetObjectClass(env, service) : NULL;
    jmethodID get_sensor = manager_class ?
        (*env)->GetMethodID(env, manager_class, "getDefaultSensor",
                            "(I)Landroid/hardware/Sensor;") : NULL;
    jobject sensor_object = get_sensor ?
        (*env)->CallObjectMethod(env, service, get_sensor, (jint)ASENSOR_TYPE_PROXIMITY) : NULL;
    jclass sensor_class = sensor_object ? (*env)->GetObjectClass(env, sensor_object) : NULL;
    jmethodID get_maximum = sensor_class ?
        (*env)->GetMethodID(env, sensor_class, "getMaximumRange", "()F") : NULL;
    if (get_maximum) {
        range = (*env)->CallFloatMethod(env, sensor_object, get_maximum);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        range = 0.0f;
    }
    if (sensor_class) (*env)->DeleteLocalRef(env, sensor_class);
    if (sensor_object) (*env)->DeleteLocalRef(env, sensor_object);
    if (manager_class) (*env)->DeleteLocalRef(env, manager_class);
    if (service) (*env)->DeleteLocalRef(env, service);
    if (sensor_service) (*env)->DeleteLocalRef(env, sensor_service);
    if (activity_class) (*env)->DeleteLocalRef(env, activity_class);
    (*vm)->DetachCurrentThread(vm);
    return range;
}

void android_main(struct android_app *app)
{
    struct proximity_app s = {0};
    s.app = app;
    app->userData = &s;
    app->onAppCmd = command;
    s.manager = ASensorManager_getInstanceForPackage(PACKAGE_NAME);
    if (s.manager) {
        s.sensor = ASensorManager_getDefaultSensor(s.manager, ASENSOR_TYPE_PROXIMITY);
        if (s.sensor) {
            s.queue = ASensorManager_createEventQueue(s.manager, app->looper,
                                                        LOOPER_ID_USER, NULL, NULL);
            if (!s.queue) s.read_error = true;
        }
    }
    proximity_model_initialize(&s.model, s.sensor ? android_maximum_range(app) : 0.0f);
    for (;;) {
        int events;
        struct android_poll_source *source = NULL;
        int identifier = ALooper_pollOnce(-1, NULL, &events, (void **)&source);
        if (source) source->process(app, source);
        if (identifier == LOOPER_ID_USER) read_events(&s);
        if (app->destroyRequested) break;
    }
    set_enabled(&s, false);
    if (s.queue) ASensorManager_destroyEventQueue(s.manager, s.queue);
}
