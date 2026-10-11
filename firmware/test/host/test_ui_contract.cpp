// HT-13 (FD section 19.1, R4.1): the main UI's operator client against the
// whole box. The UI's exact frames (frontend/ground-station: deviceLink.ts
// writes {v, type, seq, cmd, ...fields}, and pings with a fractional
// t_client_ms) go through the real parser into the real supervisor, cascade,
// and estimator around the plant of plant_sim.hpp. Comms is routed as
// ws_server.cpp routes it: a ping is forwarded for the heartbeat, and an estop
// raises the e-stop flag and is queued for its ack. Step 5 of control_task.cpp
// (snapshot_due, then post) and the comms task's flush (every posted item,
// then the newest snapshot) are modelled, so the ordering guarantee is checked
// at every comms phase.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "box_rig.hpp"
#include "cJSON.h"
#include "direcsun/core/protocol.hpp"
#include "direcsun/core/snapshot.hpp"
#include "doctest.h"

using namespace direcsun;
using harness::BoxRig;
using harness::cascade_params;

namespace {

constexpr uint32_t kOperator = 1;       // the UI page that commands
constexpr uint32_t kSecondPage = 2;     // another page that only e-stops
constexpr uint32_t kUiPingTicks = 100;  // deviceLink.ts PING_INTERVAL_MS, 200 ms
constexpr uint32_t kUiAckGapTicks = 10; // the UI sends its next command after the ack: one comms period
constexpr uint32_t kUiHoldTicks = 500;  // deviceOperator.ts HOLD_MS, 1 s in READY between the slews

using Frame = std::pair<uint32_t, std::string>;  // client, text

// One UI page: one seq counter shared by its commands and pings
// (deviceLink.ts nextSeq), and the frames exactly as JSON.stringify writes them.
struct UiPage {
    uint32_t client;
    uint32_t seq = 0;

    std::string ping(double t_client_ms)
    {
        char buf[96];
        std::snprintf(buf, sizeof buf, "{\"v\":1,\"type\":\"ping\",\"seq\":%u,\"t_client_ms\":%.15g}", ++seq,
                      t_client_ms);
        return buf;
    }
    std::string command(const char *cmd, const char *fields = "")
    {
        return std::string("{\"v\":1,\"type\":\"cmd\",\"seq\":") + std::to_string(++seq) + ",\"cmd\":\"" + cmd + "\"" +
               fields + "}";
    }
};

protocol::Inbound &scratch()
{
    static protocol::Inbound inbound;  // about 3 kB: keep it off the test stack
    return inbound;
}

const Ack *ack_for(const TickOutputs &out, uint32_t client, uint32_t seq)
{
    for (std::size_t i = 0; i < out.acks.count; ++i) {
        if (out.acks.items[i].client == client && out.acks.items[i].seq == seq) return &out.acks.items[i];
    }
    return nullptr;
}

const Event *mode_event(const TickOutputs &out)
{
    for (std::size_t i = 0; i < out.events.count; ++i) {
        if (out.events.items[i].kind == Event::Kind::Mode) return &out.events.items[i];
    }
    return nullptr;
}

bool has_mode_event(const TickOutputs &out, Mode mode, const char *cause)
{
    const Event *e = mode_event(out);
    return e != nullptr && e->mode == mode && std::strcmp(e->cause, cause) == 0;
}

// For failure messages: the tick's mode event, as "MODE/cause".
std::string describe_mode_event(const TickOutputs &out)
{
    const Event *e = mode_event(out);
    return e == nullptr ? std::string("none") : std::string(mode_wire_name(e->mode)) + "/" + e->cause;
}

// What step 5 of the control task wrote (a snapshot) or posted (an ack or an
// event), with the tick that produced it.
struct Written {
    uint32_t tick;
    Mode mode;
};
struct Posted {
    uint32_t tick;
    Outbound item;
};

// The box as the UI meets it: frames in, then step 5 exactly as control_task.cpp
// orders it. Every command is acked in the tick it is drained.
struct UiBox {
    BoxRig rig;
    std::vector<Written> snapshots;
    std::vector<Posted> posted;
    std::vector<Ack> refused;  // every ack with ok false

    explicit UiBox(const Params &p) : rig(p) {}

    void tick(const std::vector<Frame> &frames = {})
    {
        std::vector<Command> cmds;
        std::vector<std::pair<uint32_t, uint32_t>> expect_ack;
        bool estop = false;
        for (const Frame &f : frames) {
            protocol::parse_inbound(f.second.data(), f.second.size(), f.first, scratch());
            REQUIRE_MESSAGE(scratch().kind != protocol::InboundKind::Rejected, f.second << ": " << scratch().reason);
            const Command &c = scratch().command;
            if (scratch().kind == protocol::InboundKind::Command) {
                expect_ack.emplace_back(c.client, c.seq);
                if (c.kind == CommandKind::Estop) estop = true;
            }
            cmds.push_back(c);
        }
        rig.in.estop_requested = estop;
        rig.tick(cmds);
        rig.in.estop_requested = false;
        for (const auto &[client, seq] : expect_ack) {
            const Ack *a = ack_for(rig.out, client, seq);
            CHECK_MESSAGE(a != nullptr, "no ack in the tick that drained client " << client << " seq " << seq);
            if (a != nullptr && !a->ok) refused.push_back(*a);
        }

        // Step 5: the snapshot first, then the acks and events.
        const std::size_t outbound = rig.out.acks.count + rig.out.events.count;
        if (snapshot_due(rig.now, outbound)) snapshots.push_back({rig.now, rig.out.mode});
        for (std::size_t i = 0; i < rig.out.acks.count; ++i) {
            Outbound o;
            o.kind = Outbound::Kind::Ack;
            o.ack = rig.out.acks.items[i];
            posted.push_back({rig.now, o});
        }
        for (std::size_t i = 0; i < rig.out.events.count; ++i) {
            Outbound o;
            o.kind = Outbound::Kind::Event;
            o.event = rig.out.events.items[i];
            posted.push_back({rig.now, o});
        }
    }
};

// The operator page: once it has sent its first command it pings every 200 ms
// for as long as the socket is open (deviceLink.ts startTalking), and its first
// command goes out right after its first ping.
struct OperatorPage {
    UiBox &box;
    UiPage page{kOperator};
    bool talking = false;
    uint32_t next_ping_tick = 0;

    double now_ms() const { return box.rig.now * 2.0 + 0.137; }  // performance.now(), fractional

    void tick(std::vector<Frame> frames = {})
    {
        if (talking && box.rig.now + 1 >= next_ping_tick) {
            frames.insert(frames.begin(), Frame{kOperator, page.ping(now_ms())});
            next_ping_tick += kUiPingTicks;
        }
        box.tick(frames);
    }

    // Sends one command on the next tick; returns its seq.
    uint32_t command(const char *cmd, const char *fields = "")
    {
        std::vector<Frame> frames;
        if (!talking) {
            talking = true;
            frames.push_back({kOperator, page.ping(now_ms())});
            next_ping_tick = box.rig.now + 1 + kUiPingTicks;
        }
        frames.push_back({kOperator, page.command(cmd, fields)});
        const uint32_t seq = page.seq;
        tick(frames);
        return seq;
    }

    // Ticks until `done` holds after a tick, at most `max_ticks`; returns the
    // ticks taken, the one where it held included.
    template <typename Done>
    uint32_t run_until(Done done, uint32_t max_ticks)
    {
        uint32_t n = 0;
        while (n < max_ticks) {
            tick();
            ++n;
            if (done(box.rig.out)) break;
        }
        return n;
    }
};

const char *slew_fields(int wire_deg)
{
    switch (wire_deg) {
    case 90: return ",\"mode\":\"SLEW\",\"psi_ref_deg\":90";
    case -90: return ",\"mode\":\"SLEW\",\"psi_ref_deg\":-90";
    default: return ",\"mode\":\"SLEW\",\"psi_ref_deg\":0";
    }
}

// The judge sequence as deviceOperator.ts runs it, on a characterised box at
// the design gains: arm, READY by spinup_complete, the lock pin released,
// zero_heading, SLEW to `wire_deg`, 1 s of READY, SLEW 0; then a second page
// e-stops. Checks every step and returns the box with its step-5 log.
std::unique_ptr<UiBox> judge_sequence(double friction_nm, int wire_deg)
{
    CAPTURE(friction_nm);
    CAPTURE(wire_deg);
    Params p = cascade_params();
    p.rate_gain_scale = 1.0f;
    auto box = std::make_unique<UiBox>(p);
    box->rig.box.friction_nm = friction_nm;
    OperatorPage ui{*box};
    const TickOutputs &out = box->rig.out;

    // Arm: SPINUP, then READY by spinup_complete.
    ui.command("arm");
    REQUIRE(has_mode_event(out, Mode::Spinup, "arm"));
    ui.run_until([](const TickOutputs &o) { return mode_event(o) != nullptr; }, 10 * kTickHz);
    REQUIRE_MESSAGE(has_mode_event(out, Mode::Ready, "spinup_complete"), describe_mode_event(out));

    // The operator turns the box loose and presses Rotate.
    box->rig.release(0.0);
    for (int i = 0; i < 250; ++i) ui.tick();
    ui.command("zero_heading");
    const double zero_true = box->rig.box.heading_deg;
    for (uint32_t i = 0; i < kUiAckGapTicks; ++i) ui.tick();

    ui.command("mode", slew_fields(wire_deg));
    REQUIRE(has_mode_event(out, Mode::Slew, "mode"));
    const uint32_t out_ticks = ui.run_until([](const TickOutputs &o) { return mode_event(o) != nullptr; }, 4000);
    REQUIRE_MESSAGE(has_mode_event(out, Mode::Ready, "slew_complete"), describe_mode_event(out));
    MESSAGE("slew to " << wire_deg << " complete " << out_ticks << " ticks after entry");
    CHECK(out_ticks < 3.5 * kTickHz);
    // Wire +90 is clockwise: the box turns -90 in the internal frame.
    const double turned = box->rig.box.heading_deg - zero_true;
    MESSAGE("the plant turned " << turned << " deg for wire " << wire_deg);
    CHECK(std::fabs(turned + wire_deg) < 3.0);

    // The UI's hold: every frame READY, and no mode event.
    for (uint32_t i = 0; i < kUiHoldTicks; ++i) {
        ui.tick();
        REQUIRE(out.mode == Mode::Ready);
        REQUIRE(mode_event(out) == nullptr);
    }

    ui.command("mode", slew_fields(0));
    REQUIRE(has_mode_event(out, Mode::Slew, "mode"));
    const uint32_t back_ticks = ui.run_until([](const TickOutputs &o) { return mode_event(o) != nullptr; }, 4000);
    REQUIRE_MESSAGE(has_mode_event(out, Mode::Ready, "slew_complete"), describe_mode_event(out));
    MESSAGE("return complete " << back_ticks << " ticks after entry");
    CHECK(std::fabs(box->rig.box.heading_deg - zero_true) < 3.0);
    for (int i = 0; i < 100; ++i) ui.tick();

    // A second page that has only watched sends its e-stop: acked to it, and
    // the box goes SAFE in the same tick.
    UiPage second{kSecondPage};
    std::vector<Frame> frames{{kSecondPage, second.ping(12.5)}, {kSecondPage, second.command("estop")}};
    ui.tick(frames);
    const Ack *a = ack_for(out, kSecondPage, second.seq);
    REQUIRE(a != nullptr);
    CHECK(a->ok);
    CHECK(has_mode_event(out, Mode::Safe, "ESTOP"));
    for (int i = 0; i < 50; ++i) ui.tick();  // so a comms flush follows the last event

    CHECK_MESSAGE(box->refused.empty(), "a command was refused: " << (box->refused.empty() ? "" : box->refused[0].reason));
    return box;
}

// The comms task (comms.cpp) waking every 10 ticks at `phase`: it sends every
// item posted so far, then the newest snapshot if it has not sent it yet.
// Returns the frames a client receives, in socket order.
struct Received {
    bool tlm;
    uint32_t tick;    // tlm: the snapshot's tick; ack or event: the tick that posted it
    Mode mode;        // tlm and mode events
    bool mode_event;
};

std::vector<Received> comms_stream(const UiBox &box, uint32_t phase)
{
    std::vector<Received> stream;
    std::size_t next_item = 0;
    std::size_t next_snapshot = 0;  // snapshots[0 .. next_snapshot) were written before this wake
    long sent_snapshot = -1;
    const uint32_t last_tick = box.rig.now;
    for (uint32_t t = 1; t <= last_tick; ++t) {
        if (t % kTicksPerSnapshot != phase) continue;
        while (next_item < box.posted.size() && box.posted[next_item].tick <= t) {
            const Posted &p = box.posted[next_item++];
            const bool is_mode = p.item.kind == Outbound::Kind::Event && p.item.event.kind == Event::Kind::Mode;
            stream.push_back({false, p.tick, is_mode ? p.item.event.mode : Mode::Safe, is_mode});
        }
        while (next_snapshot < box.snapshots.size() && box.snapshots[next_snapshot].tick <= t) ++next_snapshot;
        if (next_snapshot > 0 && static_cast<long>(next_snapshot - 1) != sent_snapshot) {
            sent_snapshot = static_cast<long>(next_snapshot - 1);
            const Written &w = box.snapshots[next_snapshot - 1];
            stream.push_back({true, w.tick, w.mode, false});
        }
    }
    return stream;
}

}  // namespace

TEST_CASE("HT-13 (a): the UI's exact frames parse to the commands it means")
{
    struct Case {
        const char *text;
        protocol::InboundKind kind;
        CommandKind cmd;
        uint32_t seq;
        Mode target;
        float internal_psi_ref;
    };
    const Case cases[] = {
        {R"({"v":1,"type":"ping","seq":1,"t_client_ms":473.6})", protocol::InboundKind::Ping, CommandKind::Ping, 1,
         Mode::Safe, 0.0f},
        {R"({"v":1,"type":"cmd","seq":2,"cmd":"arm"})", protocol::InboundKind::Command, CommandKind::Arm, 2, Mode::Safe,
         0.0f},
        {R"({"v":1,"type":"cmd","seq":3,"cmd":"zero_heading"})", protocol::InboundKind::Command,
         CommandKind::ZeroHeading, 3, Mode::Safe, 0.0f},
        {R"({"v":1,"type":"cmd","seq":4,"cmd":"mode","mode":"SLEW","psi_ref_deg":90})", protocol::InboundKind::Command,
         CommandKind::Mode, 4, Mode::Slew, -90.0f},
        {R"({"v":1,"type":"cmd","seq":5,"cmd":"mode","mode":"SLEW","psi_ref_deg":-90})", protocol::InboundKind::Command,
         CommandKind::Mode, 5, Mode::Slew, 90.0f},
        {R"({"v":1,"type":"cmd","seq":6,"cmd":"mode","mode":"SLEW","psi_ref_deg":0})", protocol::InboundKind::Command,
         CommandKind::Mode, 6, Mode::Slew, 0.0f},
        {R"({"v":1,"type":"cmd","seq":7,"cmd":"mode","mode":"READY"})", protocol::InboundKind::Command, CommandKind::Mode,
         7, Mode::Ready, 0.0f},
        {R"({"v":1,"type":"cmd","seq":8,"cmd":"estop"})", protocol::InboundKind::Command, CommandKind::Estop, 8,
         Mode::Safe, 0.0f},
    };
    for (const Case &c : cases) {
        CAPTURE(c.text);
        protocol::parse_inbound(c.text, std::strlen(c.text), kOperator, scratch());
        REQUIRE(scratch().kind == c.kind);
        CHECK(scratch().command.kind == c.cmd);
        CHECK(scratch().command.client == kOperator);
        CHECK(scratch().command.seq == c.seq);
        if (c.cmd == CommandKind::Mode) {
            CHECK(scratch().command.target_mode == c.target);
            if (c.target == Mode::Slew) CHECK(scratch().command.psi_ref_deg == doctest::Approx(c.internal_psi_ref));
        }
    }

    // The ping's pong echoes the ping's seq and its fractional t_client_ms.
    const char *ping = cases[0].text;
    protocol::parse_inbound(ping, std::strlen(ping), kOperator, scratch());
    char buf[protocol::kMaxPongBytes];
    REQUIRE(protocol::encode_pong(41, scratch().command.seq, scratch().t_client_ms, 83120, buf, sizeof buf) > 0);
    cJSON *pong = cJSON_Parse(buf);
    REQUIRE(pong != nullptr);
    const cJSON *ping_seq = cJSON_GetObjectItemCaseSensitive(pong, "ping_seq");
    const cJSON *t_client = cJSON_GetObjectItemCaseSensitive(pong, "t_client_ms");
    CHECK((cJSON_IsNumber(ping_seq) && ping_seq->valuedouble == 1.0));
    CHECK((cJSON_IsNumber(t_client) && t_client->valuedouble == 473.6));
    cJSON_Delete(pong);
}

TEST_CASE("HT-13 (b): the judge sequence as the UI runs it turns the box both ways and back")
{
    for (double friction : {0.0, 1e-3, 3e-3}) {
        for (int wire : {90, -90}) judge_sequence(friction, wire);
    }
}

TEST_CASE("HT-13 (c): the first tlm after every ack and mode event is at least as new at every comms phase")
{
    int mode_events = 0;
    int acks = 0;
    int stale = 0;
    for (double friction : {0.0, 1e-3, 3e-3}) {
        for (int wire : {90, -90}) {
            const std::unique_ptr<UiBox> box = judge_sequence(friction, wire);
            int posted_mode_events = 0;
            for (const Posted &p : box->posted) {
                if (p.item.kind == Outbound::Kind::Event && p.item.event.kind == Event::Kind::Mode) ++posted_mode_events;
            }
            CHECK(posted_mode_events == 7);  // SPINUP, READY, SLEW, READY, SLEW, READY, SAFE
            const int checked_before = mode_events;
            for (uint32_t phase = 0; phase < kTicksPerSnapshot; ++phase) {
                CAPTURE(phase);
                const std::vector<Received> stream = comms_stream(*box, phase);
                for (std::size_t i = 0; i < stream.size(); ++i) {
                    if (stream[i].tlm) continue;
                    std::size_t j = i + 1;
                    while (j < stream.size() && !stream[j].tlm) ++j;
                    if (j == stream.size()) continue;  // the run ended before the next flush
                    if (stream[i].mode_event) {
                        ++mode_events;
                        if (stream[j].mode != stream[i].mode) ++stale;
                        CHECK_MESSAGE(stream[j].mode == stream[i].mode, "mode event at tick " << stream[i].tick
                                                                            << " followed by a tlm from tick "
                                                                            << stream[j].tick);
                    } else {
                        ++acks;
                    }
                    CHECK(stream[j].tick >= stream[i].tick);
                }
            }
            // The instrument checked every mode event of the run at every phase.
            CHECK(mode_events - checked_before == posted_mode_events * static_cast<int>(kTicksPerSnapshot));
        }
    }
    MESSAGE(mode_events << " mode events and " << acks << " other items checked, " << stale << " followed by a stale tlm");
}

TEST_CASE("HT-13 (d): mode READY is a harmless ok in READY and ends HOLD SLEW and MANUAL")
{
    const char *ready = ",\"mode\":\"READY\"";
    SUBCASE("refused in SAFE and in SPINUP")
    {
        UiBox box(cascade_params());
        OperatorPage ui{box};
        ui.command("mode", ready);
        REQUIRE(box.refused.size() == 1);
        CHECK(std::string(box.refused[0].reason) == "not armed");
        ui.command("arm");
        REQUIRE(box.rig.out.mode == Mode::Spinup);
        ui.command("mode", ready);
        REQUIRE(box.refused.size() == 2);
        CHECK(std::string(box.refused[1].reason) == "wheel not at bias");
        CHECK(box.rig.out.mode == Mode::Spinup);
    }
    SUBCASE("in READY: ok, and nothing else differs from a box that was not sent it")
    {
        // Two identical boxes; one is sent mode READY, the other only a ping.
        UiBox sent(cascade_params());
        UiBox control(cascade_params());
        OperatorPage a{sent};
        OperatorPage b{control};
        a.command("arm");
        b.command("arm");
        a.run_until([](const TickOutputs &o) { return o.mode == Mode::Ready; }, 10 * kTickHz);
        b.run_until([](const TickOutputs &o) { return o.mode == Mode::Ready; }, 10 * kTickHz);
        sent.rig.release(0.0);
        control.rig.release(0.0);
        a.command("zero_heading");
        b.command("zero_heading");
        REQUIRE(sent.rig.now == control.rig.now);
        a.command("mode", ready);
        b.tick({{kOperator, b.page.ping(1.0)}});
        CHECK(sent.refused.empty());
        CHECK(sent.rig.out.mode == Mode::Ready);
        CHECK(mode_event(sent.rig.out) == nullptr);
        for (int i = 0; i < 1000; ++i) {
            a.tick();
            b.tick();
            REQUIRE(sent.rig.out.mode == control.rig.out.mode);
            REQUIRE(sent.rig.out.throttle == control.rig.out.throttle);
            REQUIRE(sent.rig.out.flags == control.rig.out.flags);
            REQUIRE(sent.rig.out.psi_ref_valid == control.rig.out.psi_ref_valid);
            REQUIRE(sent.rig.ctl.wheel_ref_rpm() == control.rig.ctl.wheel_ref_rpm());
            REQUIRE(sent.rig.out.heading_deg == control.rig.out.heading_deg);
            REQUIRE(sent.rig.out.events.count == control.rig.out.events.count);
        }
    }
    SUBCASE("in HOLD, SLEW, and MANUAL: READY with cause mode")
    {
        const char *entries[] = {",\"mode\":\"HOLD\"", ",\"mode\":\"SLEW\",\"psi_ref_deg\":90",
                                 ",\"mode\":\"MANUAL\",\"input\":\"throttle\""};
        const Mode modes[] = {Mode::Hold, Mode::Slew, Mode::Manual};
        for (int k = 0; k < 3; ++k) {
            CAPTURE(k);
            UiBox box(cascade_params());
            OperatorPage ui{box};
            ui.command("arm");
            ui.run_until([](const TickOutputs &o) { return o.mode == Mode::Ready; }, 10 * kTickHz);
            ui.command("mode", entries[k]);
            REQUIRE(box.rig.out.mode == modes[k]);
            for (int i = 0; i < 50; ++i) ui.tick();
            ui.command("mode", ready);
            CHECK(box.refused.empty());
            CHECK(has_mode_event(box.rig.out, Mode::Ready, "mode"));
            CHECK(box.rig.out.mode == Mode::Ready);
            CHECK_FALSE(box.rig.out.psi_ref_valid);
        }
    }
}

TEST_CASE("HT-13 (e): mode READY right after SLEW stops the box near where it started")
{
    for (int wire : {90, -90}) {
        CAPTURE(wire);
        Params p = cascade_params();
        p.rate_gain_scale = 1.0f;
        UiBox box(p);
        box.rig.box.friction_nm = 1e-3;
        OperatorPage ui{box};
        ui.command("arm");
        ui.run_until([](const TickOutputs &o) { return o.mode == Mode::Ready; }, 10 * kTickHz);
        box.rig.release(0.0);
        for (int i = 0; i < 250; ++i) ui.tick();
        ui.command("zero_heading");
        const double start = box.rig.box.heading_deg;
        ui.command("mode", slew_fields(wire));
        CHECK(has_mode_event(box.rig.out, Mode::Slew, "mode"));
        ui.command("mode", ",\"mode\":\"READY\"");
        CHECK(has_mode_event(box.rig.out, Mode::Ready, "mode"));
        CHECK(box.refused.empty());
        int later_mode_events = 0;
        for (int i = 0; i < 3 * static_cast<int>(kTickHz); ++i) {
            ui.tick();
            if (mode_event(box.rig.out) != nullptr) ++later_mode_events;
        }
        CHECK(later_mode_events == 0);
        CHECK(box.rig.out.mode == Mode::Ready);
        CHECK(std::fabs(box.rig.box.heading_deg - start) < 10.0);
        CHECK(std::fabs(box.rig.box.rate_dps()) < 1.0);
    }
}
