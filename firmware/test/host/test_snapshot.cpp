// SeqlockBuffer (FD section 7.4) and TickStats, the control task's publishing
// and timing helpers.

#include <atomic>
#include <cstdint>
#include <thread>

#include "direcsun/core/snapshot.hpp"
#include "doctest.h"

using direcsun::SeqlockBuffer;
using direcsun::TickStats;

namespace {

// Every field carries the same number, so a torn copy (half old, half new)
// is detectable.
struct Wide {
    uint32_t words[64];
};

Wide make(uint32_t n)
{
    Wide w;
    for (uint32_t &word : w.words) {
        word = n;
    }
    return w;
}

bool consistent(const Wide &w)
{
    for (uint32_t word : w.words) {
        if (word != w.words[0]) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("seqlock: nothing to read before the first write, then the latest value")
{
    SeqlockBuffer<Wide> buf;
    Wide out = make(99);
    CHECK_FALSE(buf.read(out));
    CHECK(buf.sequence() == 0);

    buf.write(make(1));
    REQUIRE(buf.read(out));
    CHECK(out.words[0] == 1);
    CHECK(consistent(out));

    buf.write(make(2));
    buf.write(make(3));
    REQUIRE(buf.read(out));
    CHECK(out.words[63] == 3);
    CHECK(buf.sequence() == 3);
}

TEST_CASE("seqlock: a reader on another thread never sees a torn value")
{
    SeqlockBuffer<Wide> buf;
    buf.write(make(0));
    std::atomic<bool> stop{false};
    std::atomic<uint32_t> written{0};

    std::thread writer([&] {
        for (uint32_t n = 1; n <= 200000; ++n) {
            buf.write(make(n));
            written.store(n, std::memory_order_relaxed);
        }
        stop.store(true);
    });

    uint32_t reads = 0;
    uint32_t torn = 0;
    uint32_t last = 0;
    uint32_t went_back = 0;
    while (!stop.load()) {
        Wide out;
        if (buf.read(out, 64)) {
            ++reads;
            if (!consistent(out)) {
                ++torn;
            }
            if (out.words[0] < last) {
                ++went_back;
            }
            last = out.words[0];
        }
    }
    writer.join();

    CHECK(reads > 1000);  // the reader really ran against a busy writer
    CHECK(torn == 0);
    CHECK(went_back == 0);
}

TEST_CASE("tick stats: the jitter percentile and the worst deviation")
{
    TickStats s;
    for (int i = 0; i < 495; ++i) {
        s.add_period(2000 + (i % 3) - 1, 2000);  // within 1 us
    }
    for (int i = 0; i < 5; ++i) {
        s.add_period(2040, 2000);  // the slowest 1 %
    }
    CHECK(s.percentile_dev_us(0.99f) == 1);
    CHECK(s.percentile_dev_us(1.0f) == 40);
    CHECK(s.period_max_dev_us == 40);

    s.add_period(2500, 2000);  // beyond the last bin
    CHECK(s.period_max_dev_us == 500);
    CHECK(s.percentile_dev_us(1.0f) == TickStats::kJitterBins - 1);

    TickStats empty;
    CHECK(empty.percentile_dev_us(0.99f) == 0);
}
