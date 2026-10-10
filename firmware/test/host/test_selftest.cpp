// Positive control for the harness itself (CONTEXT.md: never delete or weaken
// a positive control). Compiled only with -DDIRECSUN_HT_SELFTEST=ON, which
// tools/host-test.sh --selftest uses to prove a failing test fails the run.
#include "doctest.h"

#ifdef DIRECSUN_HT_SELFTEST
TEST_CASE("selftest_must_fail")
{
    CHECK_MESSAGE(1 == 2, "deliberate failure: the harness must report this");
}
#endif
