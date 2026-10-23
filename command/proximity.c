#include "android_proximity.h"
#include "proximity_model.h"

#include <android/looper.h>

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *info_path = "/hardware/sensors/proximity/info";
static const char *sample_path = "/hardware/sensors/proximity/sample";
static const char *events_path = "/hardware/sensors/proximity/events";
static const char *inspect_path = "/hardware/sensors/proximity/inspect";
static const char *inspect_events_path = "/hardware/sensors/proximity/inspect-events";
static const char *control_path = "/hardware/sensors/proximity/control";
static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void usage(const char *command)
{
    (void)fprintf(stderr,
        "usage: %s read PATH [event_count]\n"
        "paths:\n"
        "  %s\n  %s\n  %s\n  %s\n  %s\n  %s\n",
        command, info_path, sample_path, events_path,
        inspect_path, inspect_events_path, control_path);
}

static bool parse_count(const char *text, long *count)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (text[0] == '\0' || end == text || *end != '\0' ||
        errno != 0 || parsed <= 0) {
        return false;
    }
    *count = parsed;
    return true;
}

static int64_t elapsed_ms(const struct timespec *start)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return ((int64_t)now.tv_sec - (int64_t)start->tv_sec) * 1000 +
        ((int64_t)now.tv_nsec - (int64_t)start->tv_nsec) / 1000000;
}

static int write_info(const struct android_proximity *sensor)
{
    const char *name = android_proximity_name(sensor);
    const char *vendor = android_proximity_vendor(sensor);
    (void)printf("name\t%s\n", name == NULL ? "unknown" : name);
    (void)printf("vendor\t%s\n", vendor == NULL ? "unknown" : vendor);
    (void)printf("type\t%d\n", android_proximity_type(sensor));
    (void)printf("units\tcm\n");
    (void)printf("maximum_range_cm\t%.9g\n",
                 (double)android_proximity_maximum_range_cm(sensor));
    (void)printf("resolution_cm\t%.9g\n",
                 (double)android_proximity_resolution(sensor));
    (void)printf("minimum_delay_us\t%d\n",
                 android_proximity_minimum_delay_us(sensor));
    (void)printf("mode\ton-change\n");
    return 0;
}

static void write_inspection(
    const struct proximity_model *model, long index)
{
    (void)printf("record.index\t%ld\n", index);
    (void)printf("android.timestamp_ns\t%" PRId64 "\n", model->timestamp_ns);
    (void)printf("android.distance_cm\t%.9g\n", (double)model->distance_cm);
    (void)printf("sensor.maximum_range_cm\t%.9g\n", (double)model->maximum_cm);
    (void)printf("state\t%s\n", proximity_state_name(model));
    (void)printf("events\t%" PRIu64 "\n", model->event_count);
    (void)printf("transitions\t%" PRIu64 "\n", model->transition_count);
}

static int stream(
    struct android_proximity *source,
    bool show_timestamp,
    bool inspect,
    long limit,
    bool single_sample)
{
    enum android_proximity_status status = android_proximity_enable(source);
    if (status != ANDROID_PROXIMITY_OK) {
        (void)fprintf(stderr, "%s\n", android_proximity_status_text(status));
        return 1;
    }

    struct timespec started = {0, 0};
    if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) {
        (void)perror("clock_gettime");
        android_proximity_disable(source);
        return 1;
    }

    struct proximity_model model;
    proximity_model_initialize(&model);
    long printed = 0;
    int exit_status = 0;
    while (!stop_requested && (limit == 0 || printed < limit)) {
        int poll_result = ALooper_pollOnce(1000, NULL, NULL, NULL);
        if (poll_result == ALOOPER_POLL_ERROR) {
            (void)fprintf(stderr, "Android looper error\n");
            exit_status = 1;
            break;
        }

        for (;;) {
            struct android_proximity_reading reading;
            enum android_proximity_read_result result =
                android_proximity_next(source, &reading);
            if (result == ANDROID_PROXIMITY_READ_EMPTY) {
                break;
            }
            if (result == ANDROID_PROXIMITY_READ_ERROR) {
                (void)fprintf(stderr, "Android proximity event read failed\n");
                exit_status = 1;
                goto end;
            }
            enum proximity_accept_result accepted = proximity_model_accept(
                &model, reading.timestamp_ns, reading.distance_cm,
                android_proximity_maximum_range_cm(source));
            if (accepted != PROXIMITY_ACCEPTED) {
                (void)fprintf(stderr, "invalid proximity event: %d\n", (int)accepted);
                continue;
            }

            if (inspect) {
                if (printed != 0) {
                    (void)putchar('\n');
                }
                write_inspection(&model, printed);
            } else if (show_timestamp) {
                (void)printf("%" PRId64 "\t%.9g\t%s\n",
                    reading.timestamp_ns, (double)reading.distance_cm,
                    proximity_state_name(&model));
            } else {
                (void)printf("%.9g\n", (double)reading.distance_cm);
            }
            (void)fflush(stdout);
            printed += 1;
            if (limit != 0 && printed >= limit) {
                break;
            }
        }

        /* A one-shot reading must terminate when a sensor never reports. */
        if (single_sample && elapsed_ms(&started) >= 10000 && printed == 0) {
            (void)fprintf(stderr, "no proximity event after 10 seconds\n");
            exit_status = 1;
            break;
        }
    }
end:
    android_proximity_disable(source);
    return exit_status;
}

int main(int argc, char **argv)
{
    if (argc < 3 || strcmp(argv[1], "read") != 0) {
        usage(argv[0]);
        return 2;
    }
    const char *path = argv[2];
    if (strcmp(path, control_path) == 0) {
        (void)fprintf(stderr, "control writes are not implemented\n");
        return 3;
    }

    bool events = strcmp(path, events_path) == 0;
    bool inspect_events = strcmp(path, inspect_events_path) == 0;
    bool sample = strcmp(path, sample_path) == 0;
    bool inspect = strcmp(path, inspect_path) == 0;
    bool info = strcmp(path, info_path) == 0;
    if (!events && !inspect_events && !sample && !inspect && !info) {
        usage(argv[0]);
        return 2;
    }
    long limit = (sample || inspect) ? 1 : 0;
    if (argc == 4 && (events || inspect_events)) {
        if (!parse_count(argv[3], &limit)) {
            (void)fprintf(stderr, "event_count must be a positive integer\n");
            return 2;
        }
    } else if (argc != 3) {
        usage(argv[0]);
        return 2;
    }

    (void)signal(SIGINT, request_stop);
    (void)signal(SIGTERM, request_stop);
    ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    if (looper == NULL) {
        (void)fprintf(stderr, "Android event loop unavailable\n");
        return 1;
    }

    struct android_proximity source;
    enum android_proximity_status status =
        android_proximity_open(&source, looper, 1, NULL);
    if (status != ANDROID_PROXIMITY_OK) {
        (void)fprintf(stderr, "%s\n", android_proximity_status_text(status));
        return 1;
    }
    int result = info ? write_info(&source)
        : stream(&source, events || inspect_events,
                 inspect || inspect_events, limit, sample || inspect);
    android_proximity_close(&source);
    return result;
}
