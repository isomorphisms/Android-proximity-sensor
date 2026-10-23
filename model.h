#ifndef PROXIMITY_MODEL_H
#define PROXIMITY_MODEL_H
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define PROXIMITY_HISTORY_SIZE 8

enum proximity_relation {
    PROXIMITY_UNKNOWN = 0,
    PROXIMITY_NEAR = 1,
    PROXIMITY_FAR = 2
};

struct proximity_transition {
    int64_t timestamp_ns;
    float reported_cm;
    enum proximity_relation relation;
};

struct proximity_model {
    bool has_reading;
    float reported_cm;
    float maximum_cm;
    enum proximity_relation relation;
    int64_t first_timestamp_ns;
    int64_t last_timestamp_ns;
    uint32_t events;
    uint32_t changes;
    struct proximity_transition transitions[PROXIMITY_HISTORY_SIZE];
    unsigned next_transition;
    unsigned transition_count;
};

static inline enum proximity_relation proximity_interpret(float reported_cm, float maximum_cm)
{
    if (!isfinite(reported_cm) || !isfinite(maximum_cm) ||
        maximum_cm <= 0.0f || reported_cm < 0.0f) {
        return PROXIMITY_UNKNOWN;
    }
    // Android reports maxRange (not a reliable physical distance) for FAR.
    return reported_cm >= maximum_cm ? PROXIMITY_FAR : PROXIMITY_NEAR;
}

static inline void proximity_model_initialize(struct proximity_model *model, float maximum_cm)
{
    memset(model, 0, sizeof(*model));
    model->maximum_cm = maximum_cm;
}

static inline bool proximity_model_accept(
    struct proximity_model *model, float reported_cm, int64_t timestamp_ns)
{
    enum proximity_relation next = proximity_interpret(reported_cm, model->maximum_cm);
    if (next == PROXIMITY_UNKNOWN) {
        return false;
    }
    bool changed = model->has_reading && model->relation != next;
    if (!model->has_reading) {
        model->first_timestamp_ns = timestamp_ns;
    }
    model->has_reading = true;
    model->reported_cm = reported_cm;
    model->last_timestamp_ns = timestamp_ns;
    model->relation = next;
    if (model->events != UINT32_MAX) {
        model->events++;
    }
    if (changed && model->changes != UINT32_MAX) {
        model->changes++;
    }
    if (changed || model->events == 1u) {
        unsigned position = model->next_transition;
        model->transitions[position].timestamp_ns = timestamp_ns;
        model->transitions[position].reported_cm = reported_cm;
        model->transitions[position].relation = next;
        model->next_transition = (position + 1u) % PROXIMITY_HISTORY_SIZE;
        if (model->transition_count < PROXIMITY_HISTORY_SIZE) {
            model->transition_count++;
        }
    }
    return true;
}

#endif
