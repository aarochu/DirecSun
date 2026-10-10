// HT-2: wheel speed from Hall edges (FD sections 10.4, 11.2, 19.1).
#include <cstdint>
#include <initializer_list>

#include "direcsun/core/wheel_speed.hpp"
#include "doctest.h"

using direcsun::EdgeResult;
using direcsun::WheelSpeed;
using direcsun::WheelSpeedConfig;

namespace {

// Feeds edges separated by the given periods, starting at t0. Returns the time
// of the last edge.
int64_t feed(WheelSpeed &ws, int64_t t0, std::initializer_list<int64_t> periods)
{
    int64_t t = t0;
    ws.add_edge(t);
    for (const int64_t p : periods) {
        t += p;
        ws.add_edge(t);
    }
    return t;
}

}  // namespace

TEST_CASE("HT-2 no edges yet: zero speed and no age")
{
    WheelSpeed ws;
    CHECK(ws.speed_fast_rpm(1000000) == 0.0f);
    CHECK(ws.speed_safe_rpm(1000000) == 0.0f);
    CHECK(ws.last_edge_age_us(1000000) == -1);
    CHECK(ws.edge_count() == 0);
    CHECK(ws.add_edge(500000) == EdgeResult::kFirst);
    CHECK(ws.speed_fast_rpm(500000) == 0.0f);
    CHECK(ws.last_edge_age_us(510000) == 10000);
    CHECK(ws.edge_count() == 1);
}

TEST_CASE("HT-2 rpm = 30 / period at the FD section 6 speeds")
{
    // Two edges per turn: rpm = 30e6 / period_us.
    struct Row {
        int64_t period_us;
        float rpm;
    };
    for (const Row row : {Row{37500, 800.0f}, Row{12500, 2400.0f}, Row{7500, 4000.0f}, Row{7317, 4100.0f}}) {
        WheelSpeed ws;
        const int64_t t = feed(ws, 0, {row.period_us, row.period_us, row.period_us, row.period_us});
        CHECK(ws.speed_fast_rpm(t) == doctest::Approx(row.rpm).epsilon(1e-3));
        CHECK(ws.speed_safe_rpm(t) == doctest::Approx(row.rpm).epsilon(1e-3));
        CHECK(ws.period_count() == 4);
    }
}

TEST_CASE("HT-2 edges_per_rev scales the conversion")
{
    WheelSpeedConfig cfg;
    cfg.edges_per_rev = 1;
    WheelSpeed ws(cfg);
    const int64_t t = feed(ws, 0, {25000});
    CHECK(ws.speed_fast_rpm(t) == doctest::Approx(2400.0f));
}

TEST_CASE("HT-2 plausibility window: 3 ms and 200 ms accepted, 2.9 ms and 201 ms rejected")
{
    WheelSpeed ws;
    ws.add_edge(0);
    CHECK(ws.add_edge(2900) == EdgeResult::kTooShort);   // 2.9 ms from the reference at 0
    CHECK(ws.add_edge(3000) == EdgeResult::kPeriod);     // exactly 3 ms from 0
    CHECK(ws.add_edge(203000) == EdgeResult::kPeriod);   // exactly 200 ms
    CHECK(ws.add_edge(404000) == EdgeResult::kTooLong);  // 201 ms
    CHECK(ws.glitch_count() == 2);
    CHECK(ws.edge_count() == 4);  // 0, 3000, 203000, 404000; the 2900 edge was ignored
}

TEST_CASE("HT-2 a short glitch leaves the reference edge in place")
{
    WheelSpeed ws;
    ws.add_edge(0);
    ws.add_edge(12500);
    CHECK(ws.add_edge(13000) == EdgeResult::kTooShort);  // bounce 0.5 ms after a real edge
    CHECK(ws.add_edge(25000) == EdgeResult::kPeriod);    // still 12.5 ms from the edge at 12500
    CHECK(ws.speed_fast_rpm(25000) == doctest::Approx(2400.0f));
    CHECK(ws.glitch_count() == 1);
    CHECK(ws.last_edge_age_us(25000) == 0);
}

TEST_CASE("HT-2 a timestamp at or before the reference is a glitch")
{
    WheelSpeed ws;
    ws.add_edge(100000);
    CHECK(ws.add_edge(100000) == EdgeResult::kTooShort);
    CHECK(ws.add_edge(90000) == EdgeResult::kTooShort);
    CHECK(ws.glitch_count() == 2);
}

TEST_CASE("HT-2 the median rejects a single bad period; the fast estimate follows it")
{
    WheelSpeed ws;
    // Three periods at 2400 rpm, then one plausible but wrong 9 ms period.
    const int64_t t = feed(ws, 0, {12500, 12500, 12500, 9000});
    CHECK(ws.speed_safe_rpm(t) == doctest::Approx(2400.0f));
    CHECK(ws.speed_fast_rpm(t) == doctest::Approx(30.0e6f / 9000.0f));
}

TEST_CASE("HT-2 median over fewer than four periods")
{
    WheelSpeed one;
    int64_t t = feed(one, 0, {10000});
    CHECK(one.speed_safe_rpm(t) == doctest::Approx(3000.0f));

    WheelSpeed two;
    t = feed(two, 0, {10000, 15000});  // mean of the two: 12500 us
    CHECK(two.speed_safe_rpm(t) == doctest::Approx(2400.0f));

    WheelSpeed three;
    t = feed(three, 0, {10000, 15000, 12500});  // middle: 12500 us
    CHECK(three.speed_safe_rpm(t) == doctest::Approx(2400.0f));
}

TEST_CASE("HT-2 staleness bound falls as elapsed time grows")
{
    WheelSpeed ws;
    const int64_t t = feed(ws, 0, {12500, 12500, 12500, 12500});  // 2400 rpm
    // Up to twice the last period, the estimate stands.
    CHECK_FALSE(ws.stale(t + 25000));
    CHECK(ws.speed_safe_rpm(t + 25000) == doctest::Approx(2400.0f));
    // Beyond it, both estimates are bounded by one edge per elapsed time.
    CHECK(ws.stale(t + 30000));
    CHECK(ws.speed_safe_rpm(t + 30000) == doctest::Approx(1000.0f));
    CHECK(ws.speed_fast_rpm(t + 30000) == doctest::Approx(1000.0f));
    float previous = ws.speed_safe_rpm(t + 30000);
    for (const int64_t dt : {60000, 120000, 500000, 1000000, 10000000}) {
        const float now = ws.speed_safe_rpm(t + dt);
        CHECK(now < previous);
        previous = now;
    }
    CHECK(ws.speed_safe_rpm(t + 1000000) == doctest::Approx(30.0f));
}

TEST_CASE("HT-2 a gap longer than 200 ms forgets the speed before it")
{
    WheelSpeed ws;
    int64_t t = feed(ws, 0, {12500, 12500, 12500, 12500});
    t += 250000;
    CHECK(ws.add_edge(t) == EdgeResult::kTooLong);
    CHECK(ws.period_count() == 0);
    CHECK(ws.speed_safe_rpm(t) == 0.0f);  // not the 2400 rpm from before the gap
    CHECK(ws.speed_fast_rpm(t) == 0.0f);
    t += 12500;
    CHECK(ws.add_edge(t) == EdgeResult::kPeriod);
    CHECK(ws.speed_safe_rpm(t) == doctest::Approx(2400.0f));
}

TEST_CASE("HT-2 last edge age ignores glitches")
{
    WheelSpeed ws;
    ws.add_edge(0);
    ws.add_edge(1000);  // glitch, 1 ms
    ws.add_edge(2000);  // glitch, 2 ms
    CHECK(ws.last_edge_age_us(400000) == 400000);
    CHECK(ws.edge_count() == 1);
}

TEST_CASE("HT-2 positive control: a run crossing 4100 rpm is reported above 4100")
{
    // The overspeed trip (TD section 6.7) compares speed_safe with 4100 rpm, so
    // the estimate must be able to read both sides of it.
    WheelSpeed ws;
    int64_t t = feed(ws, 0, {7500, 7500, 7500, 7500});  // 4000 rpm
    CHECK(ws.speed_safe_rpm(t) < 4100.0f);
    for (const int64_t p : {7400, 7300, 7200, 7100, 7000, 7000, 7000}) {
        t += p;
        ws.add_edge(t);
    }
    CHECK(ws.speed_safe_rpm(t) > 4100.0f);
    CHECK(ws.speed_safe_rpm(t) == doctest::Approx(30.0e6f / 7000.0f));
}

TEST_CASE("HT-2 reset forgets everything but keeps the configuration")
{
    WheelSpeedConfig cfg;
    cfg.edges_per_rev = 1;
    WheelSpeed ws(cfg);
    feed(ws, 0, {25000, 25000});
    ws.reset();
    CHECK(ws.edge_count() == 0);
    CHECK(ws.last_edge_age_us(0) == -1);
    const int64_t t = feed(ws, 1000000, {25000});
    CHECK(ws.speed_fast_rpm(t) == doctest::Approx(2400.0f));
}
