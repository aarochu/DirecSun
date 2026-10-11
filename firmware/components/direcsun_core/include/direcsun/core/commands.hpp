#pragma once

// Plain data exchanged between comms (producer of commands, consumer of acks
// and events) and the control task's supervisor (FD sections 7.4, 13, 15.3).
// No dynamic allocation: every container has a fixed capacity, so the types
// can cross a FreeRTOS queue by copy and live on the control task's stack.

#include <cstddef>
#include <cstdint>

namespace direcsun {

// Box modes. Wire names come from mode_wire_name() (FD section 2.4). BOOT is
// not represented: the supervisor starts in SAFE once the boot path is done.
enum class Mode : uint8_t {
    Safe,
    Spinup,
    Ready,
    Manual,
    // Reachable in later sets; until then `mode` commands for them are refused
    // with "not implemented".
    Detumble,
    Hold,
    Slew,
    SunPoint,
};

const char *mode_wire_name(Mode mode);

// MANUAL inputs (FD section 13.5).
enum class ManualInput : uint8_t { None, Throttle, Wheel, Rate };

// "throttle", "wheel", "rate", or nullptr for None.
const char *manual_input_wire_name(ManualInput input);

// Bit set of MANUAL inputs whose loops the firmware has built. Each rung
// (R3.7 to R3.9) adds its bit; an input without its bit is refused.
using ManualInputSet = uint8_t;
constexpr ManualInputSet manual_input_bit(ManualInput input)
{
    return input == ManualInput::None ? 0 : static_cast<ManualInputSet>(1u << static_cast<unsigned>(input));
}

// One `set_params` value: a number, a boolean, or a short numeric array (the
// feed-forward table is 8 rpm/throttle pairs, flattened to 16 numbers).
struct ParamValue {
    enum class Type : uint8_t { Number, Bool, Array };
    Type type = Type::Number;
    double number = 0.0;
    bool flag = false;
    static constexpr std::size_t kMaxArray = 16;
    float array[kMaxArray] = {};
    uint8_t array_len = 0;
};

struct ParamKeyValue {
    static constexpr std::size_t kMaxKey = 24;
    char key[kMaxKey] = {};
    ParamValue value;
};

// The keys of one `set_params` command. It is too large to copy through the
// command queue, so a SetParams command points at it; the producer keeps it
// alive until the supervisor has acked that command's seq [D].
struct ParamUpdate {
    static constexpr std::size_t kMaxKeys = 24;
    ParamKeyValue items[kMaxKeys];
    std::size_t count = 0;
};

enum class CommandKind : uint8_t {
    Arm,
    Disarm,
    Estop,
    Mode,
    Manual,
    Respin,
    ZeroHeading,
    SetParams,
    WheelStep,
    // A `ping` (FD section 14.4). Comms answers it with a pong itself and
    // forwards it here so the supervisor, which alone knows the controlling
    // client, keeps the heartbeat. Pings are never acked [D].
    Ping,
};

// A parsed, schema-valid command, tagged with its sender and seq. Semantic
// checks (mode guards, ranges, controlling client) happen in the supervisor.
struct Command {
    CommandKind kind = CommandKind::Ping;
    uint32_t client = 0;  // comms' id for the WebSocket client; 0 is never a client
    uint32_t seq = 0;     // echoed in the ack (FD finding V-14)

    // kind == Mode
    Mode target_mode = Mode::Ready;
    ManualInput manual_input = ManualInput::None;  // for target_mode == Manual
    float psi_ref_deg = 0.0f;                      // for target_mode == Slew

    // kind == Manual: exactly one setpoint, named by setpoint_input
    ManualInput setpoint_input = ManualInput::None;
    float setpoint = 0.0f;  // throttle (DShot units), wheel rpm, or rate deg/s

    // kind == Respin
    bool has_wheel_rpm = false;
    float wheel_rpm = 0.0f;

    // kind == WheelStep
    float delta_rpm = 0.0f;

    // kind == SetParams
    const ParamUpdate *params = nullptr;
};

// Reply to one command. `reason` is empty when ok.
struct Ack {
    uint32_t client = 0;
    uint32_t seq = 0;
    bool ok = false;
    static constexpr std::size_t kMaxReason = 64;
    char reason[kMaxReason] = {};
};

// Box events (FD section 15.3): a mode change with its cause, or a notice.
struct Event {
    enum class Kind : uint8_t { Mode, Notice };
    Kind kind = Kind::Mode;
    Mode mode = Mode::Safe;      // Kind::Mode: the new (or confirmed) mode
    const char *cause = "";      // Kind::Mode: interlock name, command name, or transition name
    const char *notice = "";     // Kind::Notice: "PARAMS_RESET" or "HALL_BYPASS"
    const char *detail = "";     // Kind::Notice: optional, e.g. "on" or "off" for HALL_BYPASS
};

// One item on the control-to-comms queue (FD section 7.4): an ack addressed to
// one client, or an event for every client that has spoken (R3.5).
struct Outbound {
    enum class Kind : uint8_t { Ack, Event };
    Kind kind = Kind::Ack;
    Ack ack;      // kind == Ack: reply to ack.client
    Event event;  // kind == Event: its strings are static
};

// Fixed-capacity output list. Pushing into a full box drops the item and
// counts it, so the producer never blocks (FD section 7.4).
template <typename T, std::size_t N>
struct Outbox {
    T items[N];
    std::size_t count = 0;
    uint32_t dropped = 0;

    bool push(const T &item)
    {
        if (count >= N) {
            ++dropped;
            return false;
        }
        items[count++] = item;
        return true;
    }
    void clear()
    {
        count = 0;
        dropped = 0;
    }
};

}  // namespace direcsun
