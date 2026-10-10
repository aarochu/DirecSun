#pragma once

// Rung 1 of the teleop ladder (R3.7): the open-loop throttle controller.
//
//   SPINUP  throttle ramps from 0 to params.spinup_throttle at
//           kSpinupThrottleRampPerS DShot units per second (open loop, Hall or not)
//   READY   throttle holds params.spinup_throttle (the rate loop of R3.9
//           replaces this hold)
//   MANUAL  throttle input: the supervisor's clamped, staleness-held setpoint
//
// The controller only proposes a throttle. The supervisor clamps it (0..2047,
// and kBypassThrottleMax while the Hall is bypassed) and the interlocks
// override it, so nothing here can bypass a safety limit.

#include "direcsun/core/commands.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/params.hpp"

namespace direcsun {

// The MANUAL inputs whose loops exist after R3.7: open-loop throttle only.
constexpr ManualInputSet kRungOneBuiltInputs = manual_input_bit(ManualInput::Throttle);

// Open-loop spin-up throttle `ticks_in_mode` ticks after entering SPINUP.
int spinup_ramp_throttle(uint32_t ticks_in_mode, int spinup_throttle);

class OpenLoopThrottleController final : public ThrottleController {
public:
    int throttle(const ControlDemand &demand, const TickInputs &in, const Params &params) override;
    void reset() override {}
};

}  // namespace direcsun
