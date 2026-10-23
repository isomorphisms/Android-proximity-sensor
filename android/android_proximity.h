#ifndef ANDROID_PROXIMITY_H
#define ANDROID_PROXIMITY_H

#include <android/looper.h>
#include <android/sensor.h>

#include <stdbool.h>
#include <stdint.h>

/*
 * Android proximity reading: Android's event timestamp and reported distance
 * in centimeters. This is an Android SensorManager value, not a raw chip value.
 */
struct android_proximity_reading {
    int64_t timestamp_ns;
    float distance_cm;
};

enum android_proximity_status {
    ANDROID_PROXIMITY_OK = 0,
    ANDROID_PROXIMITY_NO_LOOPER,
    ANDROID_PROXIMITY_NO_MANAGER,
    ANDROID_PROXIMITY_NO_SENSOR,
    ANDROID_PROXIMITY_NO_QUEUE,
    ANDROID_PROXIMITY_ENABLE_FAILED
};

enum android_proximity_read_result {
    ANDROID_PROXIMITY_READ_ERROR = -1,
    ANDROID_PROXIMITY_READ_EMPTY = 0,
    ANDROID_PROXIMITY_READ_AVAILABLE = 1
};

struct android_proximity {
    ASensorManager *manager;
    const ASensor *sensor;
    ASensorEventQueue *queue;
    bool enabled;
};

enum android_proximity_status android_proximity_open(
    struct android_proximity *source,
    ALooper *looper,
    int looper_id,
    const char *package_name);

enum android_proximity_status android_proximity_enable(
    struct android_proximity *source);

void android_proximity_disable(struct android_proximity *source);
void android_proximity_close(struct android_proximity *source);

enum android_proximity_read_result android_proximity_next(
    struct android_proximity *source,
    struct android_proximity_reading *reading);

bool android_proximity_is_available(const struct android_proximity *source);
bool android_proximity_is_enabled(const struct android_proximity *source);

const char *android_proximity_name(const struct android_proximity *source);
const char *android_proximity_vendor(const struct android_proximity *source);
int android_proximity_type(const struct android_proximity *source);
float android_proximity_resolution(const struct android_proximity *source);
int android_proximity_minimum_delay_us(const struct android_proximity *source);

const char *android_proximity_status_text(enum android_proximity_status status);

#endif
