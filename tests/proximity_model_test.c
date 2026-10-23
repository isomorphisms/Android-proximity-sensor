#include "proximity_model.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int failures;
static void check(bool ok, const char *description)
{
    if (!ok) {
        (void)fprintf(stderr, "FAIL: %s\n", description);
        failures += 1;
    }
}

int main(void)
{
    struct proximity_model model;
    proximity_model_initialize(&model);
    check(!model.have_sample && !model.have_two_levels, "initially waiting");
    check(strcmp(proximity_state_name(&model), "WAIT") == 0, "waiting label");

    check(proximity_model_accept(&model, 10, 5.0F) == PROXIMITY_ACCEPTED,
          "first reading accepted");
    check(!model.have_two_levels && model.transition_count == 0U,
          "one distance cannot establish near/far");
    check(strcmp(proximity_state_name(&model), "WAIT") == 0, "first sample WAIT");
    check(proximity_model_accept(&model, 15, 5.0F) == PROXIMITY_ACCEPTED,
          "repeat accepted");
    check(!model.have_two_levels && model.transition_count == 0U,
          "same distance does not calibrate");

    check(proximity_model_accept(&model, 20, 0.0F) == PROXIMITY_ACCEPTED,
          "covered reading accepted");
    check(model.have_two_levels && model.near &&
          model.maximum_observed_cm == 5.0F &&
          model.minimum_observed_cm == 0.0F,
          "two observed levels classify lower as near");
    check(model.transition_count == 1U, "first transition recorded");
    check(proximity_model_accept(&model, 30, 1.0F) == PROXIMITY_ACCEPTED,
          "intermediate reading accepted");
    check(model.near && model.transition_count == 1U,
          "another lower reading does not invent a transition");
    check(proximity_model_accept(&model, 40, 5.0F) == PROXIMITY_ACCEPTED,
          "uncovered sensor accepted");
    check(!model.near && model.transition_count == 2U, "near to far transition");

    struct proximity_model saved = model;
    check(proximity_model_accept(&model, 50, NAN) == PROXIMITY_INVALID_READING,
          "nonfinite distance rejected");
    check(proximity_model_accept(&model, 50, -1.0F) == PROXIMITY_INVALID_READING,
          "negative distance rejected");
    check(proximity_model_accept(&model, 39, 1.0F) == PROXIMITY_OLD_TIMESTAMP,
          "older event rejected");
    check(model.event_count == saved.event_count &&
          model.transition_count == saved.transition_count &&
          model.distance_cm == saved.distance_cm &&
          model.timestamp_ns == saved.timestamp_ns &&
          model.minimum_observed_cm == saved.minimum_observed_cm &&
          model.maximum_observed_cm == saved.maximum_observed_cm &&
          model.near == saved.near, "rejections leave state unchanged");
    check(proximity_model_accept(&model, 50, 6.0F) == PROXIMITY_ACCEPTED,
          "new observed high accepted");
    check(!model.near && model.maximum_observed_cm == 6.0F,
          "higher observed level tentatively far");

    if (failures != 0) {
        (void)fprintf(stderr, "%d proximity model tests failed\n", failures);
        return 1;
    }
    (void)puts("PASS proximity relative levels, transitions and invalid readings");
    return 0;
}
