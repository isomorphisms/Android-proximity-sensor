#ifndef PROXIMITY_MODEL_H
#define PROXIMITY_MODEL_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * NDK SensorManager exposes event.distance but not Sensor.getMaximumRange.
 * Learn the upper/lower observed distances, never invent a maximum range.
 * Classification stays WAIT until two distinct levels have been observed.
 * It is only a relative estimate for continuously varying sensors.
 */
struct proximity_model {
    int64_t timestamp_ns;
    float distance_cm;
    float minimum_observed_cm;
    float maximum_observed_cm;
    uint64_t event_count;
    uint64_t transition_count;
    bool have_sample;
    bool have_two_levels;
    bool near;
};

enum proximity_accept_result {
    PROXIMITY_ACCEPTED,
    PROXIMITY_INVALID_READING,
    PROXIMITY_OLD_TIMESTAMP
};

static inline void proximity_model_initialize(struct proximity_model *model)
{
    *model = (struct proximity_model){0};
}

static inline enum proximity_accept_result proximity_model_accept(
    struct proximity_model *model,
    int64_t timestamp_ns,
    float distance_cm)
{
    if (!isfinite(distance_cm) || distance_cm < 0.0F) {
        return PROXIMITY_INVALID_READING;
    }
    if (model->have_sample && timestamp_ns < model->timestamp_ns) {
        return PROXIMITY_OLD_TIMESTAMP;
    }

    if (!model->have_sample) {
        model->minimum_observed_cm = distance_cm;
        model->maximum_observed_cm = distance_cm;
    } else {
        if (distance_cm < model->minimum_observed_cm) {
            model->minimum_observed_cm = distance_cm;
        }
        if (distance_cm > model->maximum_observed_cm) {
            model->maximum_observed_cm = distance_cm;
        }
    }
    bool two_levels = model->minimum_observed_cm < model->maximum_observed_cm;
    bool near = two_levels && distance_cm < model->maximum_observed_cm;

    /* The first distinct pair of on-change readings is itself a transition. */
    if ((model->have_two_levels && near != model->near) ||
        (model->have_sample && !model->have_two_levels && two_levels)) {
        model->transition_count += 1U;
    }
    model->near = near;
    model->have_sample = true;
    model->have_two_levels = two_levels;
    model->distance_cm = distance_cm;
    model->timestamp_ns = timestamp_ns;
    model->event_count += 1U;
    return PROXIMITY_ACCEPTED;
}

static inline const char *proximity_state_name(const struct proximity_model *model)
{
    if (!model->have_two_levels) {
        return "WAIT";
    }
    return model->near ? "NEAR" : "FAR";
}

#endif
