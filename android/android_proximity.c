#include "android_proximity.h"

#include <string.h>
#include <sys/types.h>

static ASensorManager *sensor_manager_for(const char *package_name)
{
#if __ANDROID_API__ >= 26
    if (package_name != NULL && package_name[0] != '\0') {
        return ASensorManager_getInstanceForPackage(package_name);
    }
#else
    (void)package_name;
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    ASensorManager *manager = ASensorManager_getInstance();
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return manager;
}

enum android_proximity_status android_proximity_open(
    struct android_proximity *source,
    ALooper *looper,
    int looper_id,
    const char *package_name)
{
    (void)memset(source, 0, sizeof(*source));

    if (looper == NULL) {
        return ANDROID_PROXIMITY_NO_LOOPER;
    }

    source->manager = sensor_manager_for(package_name);
    if (source->manager == NULL) {
        return ANDROID_PROXIMITY_NO_MANAGER;
    }

    source->sensor = ASensorManager_getDefaultSensor(
        source->manager,
        ASENSOR_TYPE_PROXIMITY);
    if (source->sensor == NULL) {
        return ANDROID_PROXIMITY_NO_SENSOR;
    }

    source->queue = ASensorManager_createEventQueue(
        source->manager,
        looper,
        looper_id,
        NULL,
        NULL);
    if (source->queue == NULL) {
        return ANDROID_PROXIMITY_NO_QUEUE;
    }

    return ANDROID_PROXIMITY_OK;
}

enum android_proximity_status android_proximity_enable(
    struct android_proximity *source)
{
    if (source->queue == NULL || source->sensor == NULL) {
        return ANDROID_PROXIMITY_NO_QUEUE;
    }
    if (source->enabled) {
        return ANDROID_PROXIMITY_OK;
    }
    /* This sensor reports only on change. No continuous sampling rate. */
    if (ASensorEventQueue_enableSensor(source->queue, source->sensor) < 0) {
        return ANDROID_PROXIMITY_ENABLE_FAILED;
    }
    source->enabled = true;
    return ANDROID_PROXIMITY_OK;
}

void android_proximity_disable(struct android_proximity *source)
{
    if (source->queue != NULL && source->sensor != NULL && source->enabled) {
        (void)ASensorEventQueue_disableSensor(source->queue, source->sensor);
    }
    source->enabled = false;
}

void android_proximity_close(struct android_proximity *source)
{
    android_proximity_disable(source);
    if (source->manager != NULL && source->queue != NULL) {
        (void)ASensorManager_destroyEventQueue(source->manager, source->queue);
    }
    (void)memset(source, 0, sizeof(*source));
}

enum android_proximity_read_result android_proximity_next(
    struct android_proximity *source,
    struct android_proximity_reading *reading)
{
    if (source->queue == NULL) {
        return ANDROID_PROXIMITY_READ_ERROR;
    }

    for (;;) {
        ASensorEvent event;
        ssize_t received = ASensorEventQueue_getEvents(source->queue, &event, 1);
        if (received < 0) {
            return ANDROID_PROXIMITY_READ_ERROR;
        }
        if (received == 0) {
            return ANDROID_PROXIMITY_READ_EMPTY;
        }
        if (event.type != ASENSOR_TYPE_PROXIMITY) {
            continue;
        }

        reading->timestamp_ns = event.timestamp;
        reading->distance_cm = event.distance;
        return ANDROID_PROXIMITY_READ_AVAILABLE;
    }
}

bool android_proximity_is_available(const struct android_proximity *source)
{
    return source->sensor != NULL && source->queue != NULL;
}

bool android_proximity_is_enabled(const struct android_proximity *source)
{
    return source->enabled;
}

const char *android_proximity_name(const struct android_proximity *source)
{
    return source->sensor == NULL ? NULL : ASensor_getName(source->sensor);
}

const char *android_proximity_vendor(const struct android_proximity *source)
{
    return source->sensor == NULL ? NULL : ASensor_getVendor(source->sensor);
}

int android_proximity_type(const struct android_proximity *source)
{
    return source->sensor == NULL ? 0 : ASensor_getType(source->sensor);
}

float android_proximity_resolution(const struct android_proximity *source)
{
    return source->sensor == NULL ? 0.0F : ASensor_getResolution(source->sensor);
}

float android_proximity_maximum_range_cm(const struct android_proximity *source)
{
    return source->sensor == NULL ? 0.0F : ASensor_getMaxRange(source->sensor);
}

int android_proximity_minimum_delay_us(const struct android_proximity *source)
{
    return source->sensor == NULL ? 0 : ASensor_getMinDelay(source->sensor);
}

const char *android_proximity_status_text(enum android_proximity_status status)
{
    switch (status) {
        case ANDROID_PROXIMITY_OK:
            return "Android proximity is ready";
        case ANDROID_PROXIMITY_NO_LOOPER:
            return "Android event loop is unavailable";
        case ANDROID_PROXIMITY_NO_MANAGER:
            return "Android sensor manager is unavailable";
        case ANDROID_PROXIMITY_NO_SENSOR:
            return "no default proximity is available";
        case ANDROID_PROXIMITY_NO_QUEUE:
            return "Android proximity event queue is unavailable";
        case ANDROID_PROXIMITY_ENABLE_FAILED:
            return "could not enable the Android proximity";
        default:
            return "unknown Android proximity error";
    }
}
