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
    check(!model.have_sample && model.event_count == 0U, "initially waiting");
    check(strcmp(proximity_state_name(&model), "WAIT") == 0, "waiting label");

    check(proximity_model_accept(&model, 10, 5.0F, 5.0F) == PROXIMITY_ACCEPTED,
          "max-range reading accepted");
    check(!model.near && model.transition_count == 0U, "first far reading");
    check(strcmp(proximity_state_name(&model), "FAR") == 0, "far label");

    check(proximity_model_accept(&model, 20, 0.0F, 5.0F) == PROXIMITY_ACCEPTED,
          "covered sensor accepted");
    check(model.near && model.transition_count == 1U, "far to near transition");
    check(proximity_model_accept(&model, 30, 1.0F, 5.0F) == PROXIMITY_ACCEPTED,
          "second near reading");
    check(model.transition_count == 1U && model.event_count == 3U,
          "no false transition on repeated near");
    check(proximity_model_accept(&model, 40, 5.0F, 5.0F) == PROXIMITY_ACCEPTED,
          "uncovered sensor accepted");
    check(!model.near && model.transition_count == 2U, "near to far transition");

    struct proximity_model saved = model;
    check(proximity_model_accept(&model, 50, NAN, 5.0F) == PROXIMITY_INVALID_READING,
          "nonfinite distance rejected");
    check(proximity_model_accept(&model, 50, -1.0F, 5.0F) == PROXIMITY_INVALID_READING,
          "negative distance rejected");
    check(proximity_model_accept(&model, 50, 1.0F, 0.0F) == PROXIMITY_INVALID_RANGE,
          "zero maximum range rejected");
    check(proximity_model_accept(&model, 50, 1.0F, INFINITY) == PROXIMITY_INVALID_RANGE,
          "nonfinite maximum range rejected");
    check(proximity_model_accept(&model, 39, 1.0F, 5.0F) == PROXIMITY_OLD_TIMESTAMP,
          "older event rejected");
    check(memcmp(&saved, &model, sizeof(model)) == 0,
          "invalid events leave retained state intact");

    check(proximity_model_accept(&model, 50, 6.0F, 5.0F) == PROXIMITY_ACCEPTED,
          "reading beyond maximum does not count as near");
    check(!model.near && model.transition_count == 2U,
          "maximum and above both count as far");

    if (failures != 0) {
        (void)fprintf(stderr, "%d proximity model tests failed\n", failures);
        return 1;
    }
    (void)puts("PASS proximity state, transitions, timestamps and invalid values");
    return 0;
}
