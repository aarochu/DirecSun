# Does the mock leave SPINUP with --uncharacterised and the Hall fitted?
import importlib.util, json, sys
spec = importlib.util.spec_from_file_location("mock_box", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

class Clock:
    t = 1000.0
    def __call__(self): return self.t

for characterised in (True, False):
    clock = Clock()
    box = m.Box(m.Schema(), {"throttle", "wheel", "rate"}, characterised=characterised, clock=clock)
    conn = object()
    box.receive(conn, json.dumps({"v": 1, "type": "cmd", "seq": 1, "cmd": "arm"}))
    ready_at = None
    for _ in range(int(15 / m.TICK_S)):
        clock.t += m.TICK_S
        box.controller, box.last_heartbeat = conn, clock.t
        box.tick()
        if box.mode == "READY" and ready_at is None:
            ready_at = round(clock.t - 1000.0, 2)
    print(f"characterised={characterised} hall={box.params['hall_installed']} mode_after_15s={box.mode} ready_at={ready_at} wheel={box.wheel:.0f}")
