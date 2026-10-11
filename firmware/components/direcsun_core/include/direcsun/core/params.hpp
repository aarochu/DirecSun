#pragma once

// Tunable parameters (FD section 16): defaults, ranges, all-or-nothing
// updates, and persistence through an abstract key-value store. The NVS
// implementation of the store lives outside the core (R3.4); writes must not
// run on the control task, because flash writes stall both cores' caches.

#include <cstddef>
#include <cstdint>

#include "direcsun/core/commands.hpp"

namespace direcsun {

// Compile-time ceiling on the wheel cap. omega_cap_rpm can be lowered only by
// rebuilding the firmware after a TD revision (TD section 10.3, FD section 21
// rule 2); no wire command or stored value can exceed this.
constexpr float kOmegaCapMaxRpm = 4000.0f;

// Version of the persisted layout. Bump it whenever a persisted field is added,
// removed, or reinterpreted; a stored blob with another version is discarded
// and the defaults load (event PARAMS_RESET).
constexpr uint32_t kParamVersion = 1;

constexpr std::size_t kFfPoints = 8;

struct Params {
    // Attitude and rate loops (FD section 12.2).
    float k_psi = 3.1f;
    float k_omega = 0.0129f;
    float k_i = 0.0163f;

    // Wheel loop. Zero until T-5 measures the plant [D]; R3.8 sets them.
    float wheel_kp = 0.0f;
    float wheel_ki = 0.0f;

    // Feed-forward: steady throttle versus wheel speed. ff_count is 0 (not yet
    // measured) or kFfPoints; rpm strictly increasing, throttle non-decreasing.
    uint8_t ff_count = 0;
    float ff_rpm[kFfPoints] = {};
    float ff_throttle[kFfPoints] = {};

    // Wheel speed limits (TD sections 3.4, 3.5).
    float omega_low_rpm = 800.0f;
    float omega_bias_rpm = 2400.0f;
    float omega_cap_rpm = kOmegaCapMaxRpm;  // not persisted, not settable over the wire

    int8_t s_w = 1;
    int8_t s_gyro = 1;

    uint16_t dshot_rate = 150;

    // MANUAL throttle band (FD section 13.5). Bench-safe defaults until the
    // T-5 throttle-to-speed map exists [D]: the ceiling equals kBypassThrottleMax.
    uint16_t manual_throttle_min = 150;
    uint16_t manual_throttle_max = 400;

    // Open-loop throttle SPINUP ramps to (R3.7). Bench-safe default until T-5.
    uint16_t spinup_throttle = 300;

    // HALL_SILENT arms once the throttle is above this value: the throttle at
    // which the wheel must already turn at or above the low limit (FD section
    // 14.1, "the low-limit FF value"). Set to FF(omega_low) after T-5 [D].
    uint16_t hall_expect_throttle = 200;

    // Volatile: never persisted, reset at every boot.
    bool bench_mode = false;
    bool hall_installed = true;
};

// Outcome of one validation or update. `key` names the offending key; reason
// is the full text sent in the ack.
struct ParamsResult {
    bool ok = true;
    char reason[Ack::kMaxReason] = {};
};

// Range and cross-key checks on a complete parameter set.
ParamsResult validate_params(const Params &params);

// All-or-nothing update. On success `out` is the merged, validated set; on
// failure `out` is untouched and the result names the first bad key.
// `current` supplies the values the update does not mention.
ParamsResult apply_param_update(const Params &current, const ParamUpdate &update, Params &out);

// Abstract persistent store, namespace handled by the implementation.
class KvStore {
public:
    virtual ~KvStore() = default;
    virtual bool read_u32(const char *key, uint32_t &value) = 0;
    // `len` holds the buffer capacity on entry and the stored size on success.
    virtual bool read_blob(const char *key, void *data, std::size_t &len) = 0;
    virtual bool write_u32(const char *key, uint32_t value) = 0;
    virtual bool write_blob(const char *key, const void *data, std::size_t len) = 0;
    virtual bool commit() = 0;
};

struct ParamsLoad {
    bool reset = false;          // defaults were loaded: post PARAMS_RESET
    const char *detail = "";     // "missing", "version", or "invalid" when reset
};

// Loads the persisted set. Any problem (nothing stored, another version, a
// short or corrupt blob, a value out of range) loads the defaults instead.
// The volatile fields always come back at their defaults.
ParamsLoad load_params(KvStore &store, Params &out);

// Persists the non-volatile fields with kParamVersion. Returns false if the
// store refused any write.
bool save_params(KvStore &store, const Params &params);

}  // namespace direcsun
