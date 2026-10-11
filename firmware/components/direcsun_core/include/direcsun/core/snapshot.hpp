#pragma once

// Data the control task publishes for other contexts (FD section 7.4), and the
// lock-free double buffer it publishes through.
//
//   TelemetrySnapshot  everything a `tlm` frame needs (FD section 15.3), written
//                      every 10th tick (50 Hz) and on every tick that posts an
//                      ack or event (snapshot_due). Comms (R3.5) encodes it.
//   TickStats          per-second timing for the bench checks (BC-3, F-4).
//   SeqlockBuffer<T>   single writer, any number of readers; the writer never
//                      waits, a reader retries if the writer overtook its copy.
//
// Pure C++: no ESP-IDF or FreeRTOS headers, so the seqlock is host-tested.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "direcsun/core/commands.hpp"
#include "direcsun/core/interlocks.hpp"

// Mutation-test switch (host selftest builds only): snapshots follow the
// 50 Hz cadence alone, as before R4.1, so HT-13 (c) must see a stale tlm.
#if defined(ESP_PLATFORM) && defined(DIRECSUN_MUTANT_SNAPSHOT_CADENCE_ONLY)
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

// Telemetry cadence: a snapshot every 10th control tick, 50 Hz.
constexpr uint32_t kTicksPerSnapshot = 10;

// The ordering guarantee (FD section 7.4, R4.1): the first tlm a client
// receives after an ack or event reflects the control tick that produced it,
// or a later one. The control task writes a snapshot on the cadence and on
// every tick that posts an ack or event (`outbound_count` > 0), and writes it
// before it posts them. Comms pops the posted items before it reads the
// snapshot, so any item it sends already has a snapshot at least that new,
// which its flush sends after the item. Comms still publishes at most one
// snapshot per 20 ms period, so the tlm rate stays 50 Hz.
constexpr bool snapshot_due(uint32_t tick, std::size_t outbound_count)
{
#ifdef DIRECSUN_MUTANT_SNAPSHOT_CADENCE_ONLY
    (void)outbound_count;
    return tick % kTicksPerSnapshot == 0;
#else
    return tick % kTicksPerSnapshot == 0 || outbound_count > 0;
#endif
}

// Single-writer double buffer with a sequence counter (FD section 7.4, V-12).
//
// write() fills the buffer that is not currently published, then bumps the
// sequence, which publishes it. read() copies the published buffer and checks
// the sequence again: if the writer published twice during the copy (the only
// way it can reach the buffer being copied), the copy is retried. The writer
// never waits on readers. Readers may be on another core.
template <typename T>
class SeqlockBuffer {
public:
    // Single writer only (the control task).
    void write(const T &value)
    {
        const uint32_t seq = seq_.load(std::memory_order_relaxed);
        buffers_[(seq + 1) & 1u] = value;
        seq_.store(seq + 1, std::memory_order_release);
    }

    // Copies the latest published value into `out`. Returns false if nothing
    // has been published yet, or if the writer kept overtaking the copy for
    // `max_tries` attempts (impossible at 50 Hz against a microsecond copy, but
    // bounded so a reader can never spin forever).
    bool read(T &out, int max_tries = 8) const
    {
        for (int attempt = 0; attempt < max_tries; ++attempt) {
            const uint32_t before = seq_.load(std::memory_order_acquire);
            if (before == 0) {
                return false;
            }
            out = buffers_[before & 1u];
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == before) {
                return true;
            }
        }
        return false;
    }

    // Number of writes so far.
    uint32_t sequence() const { return seq_.load(std::memory_order_acquire); }

private:
    T buffers_[2] = {};
    std::atomic<uint32_t> seq_{0};
};

// One telemetry frame's worth of box state (FD section 15.3). Comms turns it
// into JSON; fields the firmware cannot produce yet are flagged rather than
// faked (heading has no magnetometer fusion until Set 5).
struct TelemetrySnapshot {
    uint32_t tick = 0;                 // control tick of this snapshot
    int64_t t_us = 0;                  // esp_timer time at that tick
    Mode mode = Mode::Safe;
    ManualInput manual_input = ManualInput::None;
    bool ramping = false;

    float psi_deg = 0.0f;              // gyro-only heading, wrapped (-180, 180] [D]
    bool psi_ref_valid = false;        // HOLD and SLEW only (R4.3); null on the wire otherwise
    float psi_ref_deg = 0.0f;          // the heading reference, wrapped (-180, 180]
    float rate_dps = 0.0f;             // box rate estimate
    float gyro_bias_dps = 0.0f;
    float tilt_deg = 0.0f;

    float wheel_rpm = 0.0f;            // speed_safe (FD 11.2)
    float wheel_rpm_fast = 0.0f;
    float wheel_ref_rpm = 0.0f;        // 0 until the wheel loop exists (R3.8)
    int throttle = 0;                  // DShot value sent this tick
    int throttle_band_lo = 0;          // manual_throttle_band() at this tick;
    int throttle_band_hi = 0;          // 0 until filled (encoded as null)
    float tau_cmd_mNm = 0.0f;          // 0 until the rate loop exists (R3.9)

    float vbat_v = 0.0f;               // 1 s average
    uint32_t loop_us = 0;              // longest tick in the last 20 ms

    uint32_t flags = 0;                // supervisor Flag bits
    InterlockSet latched = 0;          // latched faults
    uint32_t controlling_client = 0;
    bool bench_mode = false;           // tlm then carries the bench fields (FD section 19.2)

    // Sensor presence from the boot probe.
    bool imu_present = false;
    bool mag_present = false;
    bool baro_present = false;

    // Raw values for bench telemetry (FD section 19.2).
    int16_t gyro_raw[3] = {0, 0, 0};
    int16_t accel_raw[3] = {0, 0, 0};
    float accel_rms_g = 0.0f;          // T-7 (R4.5): imbalance vibration, bench mode only
    int16_t mag_raw[3] = {0, 0, 0};
    bool mag_fresh = false;            // a new magnetometer sample arrived in the last 20 ms
    bool mag_active = false;           // the heading is fused with a calibrated compass (R5.1)
    float mag_deg = 0.0f;              // internal frame (counter-clockwise positive); the encoder converts
    bool sun_valid = false;            // the sun angle is usable (R5.4)
    float sun_deg = 0.0f;              // internal frame (counter-clockwise positive); the encoder converts
    int sun_a_mv = 0;
    int sun_b_mv = 0;

    // BMP280, read at 1 Hz off the control path. valid is false when absent
    // or when the last read failed.
    bool baro_valid = false;
    float baro_t_c = 0.0f;
    float baro_p_pa = 0.0f;

    // Counters.
    uint32_t dropped_events = 0;       // acks and events the outbound queue refused
    uint32_t imu_invalid_streak = 0;
    uint32_t missed_ticks_total = 0;
    uint32_t hall_edges = 0;           // edges accepted by WheelSpeed
    uint32_t hall_glitches = 0;
};

// Per-second timing of the control loop (BC-3 jitter, F-4 budget), measured
// at tick start with esp_timer. Deviation of each tick period from 2000 us is
// binned at 1 us up to kJitterBins - 1; larger deviations land in the last bin.
struct TickStats {
    static constexpr std::size_t kJitterBins = 101;  // 0..99 us, then ">= 100 us"

    uint32_t second = 0;               // seconds since the control task started
    uint32_t ticks = 0;                // ticks started in this second (500 expected)
    uint32_t missed = 0;               // notifications that found the task still busy
    uint32_t missed_total = 0;
    uint32_t loop_us_max = 0;          // longest tick body in this second
    uint32_t period_max_dev_us = 0;    // largest |period - 2000| in this second
    uint32_t dshot_sent = 0;           // frames queued since boot (zero sender included)
    uint32_t dshot_dropped = 0;
    uint32_t imu_invalid = 0;          // invalid IMU reads in this second
    uint16_t jitter_hist[kJitterBins] = {};

    // Deviation below which `fraction` (e.g. 0.99) of this second's periods
    // fall, in us; kJitterBins - 1 means "at least that".
    uint32_t percentile_dev_us(float fraction) const
    {
        uint32_t total = 0;
        for (uint16_t count : jitter_hist) {
            total += count;
        }
        if (total == 0) {
            return 0;
        }
        const uint32_t needed = static_cast<uint32_t>(fraction * static_cast<float>(total) + 0.999f);
        uint32_t seen = 0;
        for (std::size_t bin = 0; bin < kJitterBins; ++bin) {
            seen += jitter_hist[bin];
            if (seen >= needed) {
                return static_cast<uint32_t>(bin);
            }
        }
        return static_cast<uint32_t>(kJitterBins - 1);
    }

    void add_period(int64_t period_us, int64_t nominal_us)
    {
        int64_t dev = period_us - nominal_us;
        dev = dev < 0 ? -dev : dev;
        const uint32_t dev_u = static_cast<uint32_t>(dev);
        if (dev_u > period_max_dev_us) {
            period_max_dev_us = dev_u;
        }
        const std::size_t bin = dev_u < kJitterBins - 1 ? dev_u : kJitterBins - 1;
        if (jitter_hist[bin] < UINT16_MAX) {
            ++jitter_hist[bin];
        }
    }
};

}  // namespace direcsun
