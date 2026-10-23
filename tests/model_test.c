#include "../model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    struct proximity_model m;
    proximity_model_initialize(&m, 1.0f);
    assert(proximity_interpret(1.0f, 1.0f) == PROXIMITY_FAR);
    assert(proximity_interpret(0.0f, 1.0f) == PROXIMITY_NEAR);
    assert(proximity_interpret(4.0f, 5.0f) == PROXIMITY_NEAR);
    assert(proximity_interpret(5.0f, 5.0f) == PROXIMITY_FAR);
    assert(proximity_interpret(-1.0f, 1.0f) == PROXIMITY_UNKNOWN);
    assert(proximity_interpret(NAN, 1.0f) == PROXIMITY_UNKNOWN);
    assert(proximity_interpret(INFINITY, 1.0f) == PROXIMITY_UNKNOWN);
    assert(proximity_interpret(1.0f, 0.0f) == PROXIMITY_UNKNOWN);
    assert(proximity_model_accept(&m, 1.0f, 1000000000LL));
    assert(m.relation == PROXIMITY_FAR && m.changes == 0u);
    assert(proximity_model_accept(&m, 1.0f, 1500000000LL));
    assert(m.events == 2u && m.transition_count == 1u);
    assert(proximity_model_accept(&m, 0.0f, 2000000000LL));
    assert(m.relation == PROXIMITY_NEAR && m.changes == 1u);
    assert(!proximity_model_accept(&m, NAN, 2000000001LL));
    assert(m.events == 3u && m.changes == 1u);
    assert(proximity_model_accept(&m, 1.0f, 3000000000LL));
    assert(m.changes == 2u && m.transition_count == 3u);
    for (int i = 0; i < 20; ++i) {
        float value = (i & 1) ? 1.0f : 0.0f;
        assert(proximity_model_accept(&m, value, 4000000000LL + i * 100000000LL));
    }
    assert(m.transition_count == PROXIMITY_HISTORY_SIZE);
    assert(m.next_transition < PROXIMITY_HISTORY_SIZE);
    puts("proximity model: PASS");
    return 0;
}
