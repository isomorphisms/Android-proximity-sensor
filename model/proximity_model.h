#ifndef PROXIMITY_MODEL_H
#define PROXIMITY_MODEL_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Proximity is not an accurate continuous distance instrument. For Android
 * binary proximity sensors a value equal to maximum range indicates FAR. */
struct proximity_model {
    int64_t timestamp_ns;
    float distance_cm;
    float maximum_cm;
    uint64_t event_count;
    uint64_t transition_count;
    bool have_sample;
    bool near;
};

enum proximity_accept_result {
    PROXIMITY_ACCEPTED,
    PROXIMITY_INVALID_READING,
    PROXIMITY_INVALID_RANGE,
    PROXIMITY_OLD_TIMESTAMP
};

static inline void proximity_model_initialize(struct proximity_model *model)
{
    *model = (struct proximity_model){0};
}

static inline enum proximity_accept_result proximity_model_accept(
    struct proximity_model *model,
    int64_t timestamp_ns,
    float distance_cm,
    float maximum_cm)
{
    if (!isfinite(distance_cm) || distance_cm < 0.0F) {
        return PROXIMITY_INVALID_READING;
    }
    if (!isfinite(maximum_cm) || maximum_cm <= 0.0F) {
        return PROXIMITY_INVALID_RANGE;
    }
    if (model->have_sample && timestamp_ns < model->timestamp_ns) {
        return PROXIMITY_OLD_TIMESTAMP;
    }

    bool near = distance_cm < maximum_cm;
    if (model->have_sample && near != model->near) {
        model->transition_count += 1U;
    }
    model->near = near;
    model->have_sample = true;
    model->distance_cm = distance_cm;
    model->maximum_cm = maximum_cm;
    model->timestamp_ns = timestamp_ns;
    model->event_count += 1U;
    return PROXIMITY_ACCEPTED;
}

static inline const char *proximity_state_name(const struct proximity_model *model)
{
    if (!model->have_sample) {
        return "WAIT";
    }
    return model->near ? "NEAR" : "FAR";
}

#endif
