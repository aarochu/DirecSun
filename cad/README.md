# DirecSun printed 2U stack

Desk-scale reaction wheel trainer (formerly PocketADCS): a 2212 920KV drone
motor spins a washer flywheel inside a printed 2U CubeSat frame
(100 x 100 x 227 mm) that turns on two 608 skateboard bearings over a fixed
M8 bolt.

All geometry is parametric CadQuery. Shared dimensions live in `stack.py`;
change them there and rebuild. Each part script writes an STL (for slicing)
and a STEP (for editing); `tools/build-cad.sh` collects them into `cad/out/`.

**To print**, take the STLs from `cad/out/`. They are committed, so no Python
setup is needed. Never edit an STL or STEP by hand: change the script and
rebuild.

```bash
# from the project root
tools/bootstrap-venv.sh            # once: Python 3.12 venv in .venv with pinned CadQuery
tools/build-cad.sh                 # every part, the assembly check, and cad/out/manifest.json
tools/build-cad.sh bottom_cap      # one part (the manifest keeps the other entries)
tools/check-cad-stale.py           # fails if a script changed since cad/out was built
```

`build-cad.sh` runs each part through the cad-skill runner
(`.agents/skills/cad-skill/run_cadquery_model.py --preview --strict`), then
`cad/assembly.py` (collision check with its six positive controls), then
`cad/export_ui_model.py` (the main UI's CAD model, below), and stops at the
first failure.

## Print list

Print `bearing_fit_test` first. Press a 608 into each pocket, pick the size
that holds it firmly, and set `BRG_POCKET_D` in `stack.py` before printing the
bottom cap. Done Oct 9 2026: the 608 holds only in the 22.4 mm (three-notch)
pocket, so `BRG_POCKET_D` = 22.4 and the printer makes holes about 0.4 mm
small. `HOLE_COMP` in `stack.py` adds that 0.4 mm to every hole, slot, and nut
trap that something must fit. Leave Orca's X-Y hole compensation at 0 so the
correction is not applied twice.

`hole_gauge` results (Oct 10 2026): the M8 hole, AA bore, and #8-32 lock-nut
trap fit as intended; the M3 clearance hole is slightly loose (fine for a
clearance hole); the rod hole was loose and the 2.2 mm M2 pilot was too
small, so small holes shrink more than large ones. `hole_ladder` then set
the final values: `ROD_D` drawn 4.8 mm (4.6 slid but scraped over 6 mm, and
the deck columns are 35 to 55 mm long) and `M2_PILOT` drawn 2.4 mm (2.3 did
not take the screw, 2.5 took it with play). The M2 pilots are blind on
purpose, with a 0.8 mm floor so screws cannot come out the other side.

Every nut trap and counterbore that opens on the bed face now has two
0.2 mm sacrificial bridge layers over it (`trap_bridge` in `stack.py`): the
first bridges straight across the pocket and leaves a slot as wide as the
hole, the second bridges across the slot and leaves a square. This stops the
stringing seen on the gauge's trap ceiling. It assumes 0.2 mm layers
(`LAYER_H`). The #8-32 lock nut is wider than the 8.5 mm corner column, so the
corner traps reach 0.8 mm past the box edge on one side; that is cosmetic.

| Part | Qty | Material | Settings | Orientation |
|---|---|---|---|---|
| bearing_fit_test | 1 | same as bottom cap | same as bottom cap | flat |
| hole_gauge | 1 | PETG | 3 walls, 40% infill | flat (printed Oct 10) |
| hole_ladder | 1 | PETG | 3 walls, 40% infill | flat, bosses up |
| washer_stack_test | 1 | PETG | 100% infill, Z hop on | flat; pause on layer 60 of 62 (11.92 mm on Orca's slider), press in the 7 washers (10.6 mm holes first), resume; cut the 0.6 mm lid off afterwards to recover them |
| base_hub | 1 | PETG | 4 walls, 50% infill | flange down |
| bottom_cap | 1 | PETG | 4 walls, 40% infill | plate down, boss up |
| motor_deck | 1 | PETG | 4 walls, 40% infill | plate down |
| hall_bracket | 1 | PETG | 3 walls, 40% infill | foot down |
| flywheel_guard | 1 | PETG | 3 walls, 30% infill | upright |
| lock_pin | 1 | PETG | 4 walls, 40% infill | lying flat on its flat |
| power_deck | 1 | PLA or PETG | 3 walls, 20% infill | plate down |
| battery_deck | 1 | PLA | 3 walls, 20% infill | plate down |
| battery_holder_body | 1 | PETG | 3 walls, 20% infill | standing on end A (triangle mark down), bores vertical |
| battery_cap_a, battery_cap_b | 1 each | PETG | 3 walls, 40% infill | inner face down, skirt up; knife any flash out of the spring slots |
| avionics_deck | 1 | black PLA | 3 walls, 20% infill | plate down |
| top_deck | 1 | PLA | 3 walls, 20% infill | plate down, rails up; installed flipped |

All at 0.2 mm layers with no supports. Every part fits a 220 mm bed. Add a
brim to the decks with tall columns (motor and avionics decks) if your bed
adhesion is marginal.

## Hardware

| Item | Qty | Used for |
|---|---|---|
| 608 bearing: Bones Reds (on hand) | 2 | bottom cap boss |
| M8 x 60 hex bolt (the 70 is a spare; it would need `HUB_H` raised to 41) | 1 | base hub to bearings |
| #8-32 threaded rod (Home Depot), cut to about 245 mm | 4 | through all corner columns (4.6 mm holes) |
| #8-32 nylon-insert lock nuts, stainless | 8 | rod ends: 9.0 mm hex traps, 4.6 mm deep under the bottom cap, 2 mm deep on the top deck (the nut sits proud there) |
| M4 flat washers (screw kit) | 4 | under the four top nuts |
| M3 x 6 button-head screws (screw kit) | 4 | motor to motor deck. Longer screws can reach the windings |
| M3 x 12 button-head screw + nut (screw kit) | 1 | Hall bracket |
| Wood screws, 4 mm | 4 | base hub to a board |
| Energizer Recharge NiMH AA (on hand) | 8 | 9.6 V nominal pack in the printed holder |
| M3 x 8 button-head screws, M3 nuts, 2 M3 flat washers each (screw kit) | 16 sets | battery contacts: head inside against the cell, then washer, wire loop, washer, nut outside |
| M2 x 8 button-head screws (screw kit) | 6 | battery caps to the holder body, 3 per cap |
| M3 x 8 button-head screws + M3 nuts (screw kit) | 2 | cap A feet to the battery deck, nuts in the traps under the deck |
| Stranded wire, 18 to 22 AWG, about 40 cm | 1 | seven series bridges and the two pack leads |
| Zip ties | 2 | ESC |
| 5 x 7 cm perfboard | 2 | power deck, and one cut to 50 x 35 mm as the XIAO carrier |
| M2 x 8 button-head screws (screw kit) | 12 | the three Adafruit boards, threaded straight into 1.8 mm printed pilot holes |
| STEMMA QT cables | 2 + 1 | 200 mm MPU-6050 to top deck and QMC5883P to BMP280; one STEMMA QT to header cable for the XIAO |
| 2.5 in (63.5 mm) steel fender washers | 3 | flywheel, baseline (50 mm washers are the fallback); match the hole to your prop thread |
| Small magnets | 2 | bell, opposite each other |

## Stack (assembly frame, z = 0 at the bottom cap underside)

| Deck | Plate z | Holds |
|---|---|---|
| base_hub | -31 to 0 | M8 bolt head (hex trap), 11 mm pad under the lower bearing's inner ring |
| bottom_cap | 0 | two 608s, 25 mm apart, in a boss rising into the box; 19.6 mm ledge bore touches only the outer ring (SKF D2 19.2, Da max 20) |
| motor_deck | 38 | 2212 motor (16 x 19 mm M3), Hall bracket at 45 degrees |
| power_deck | 95 | perfboard (buck, fuse, connectors) offset to -X; ESC on +X with 2 zip ties |
| battery_deck | 126 | printed 8 x AA holder (62.7 x 32.7 mm, 75 mm long with caps), between side rails, cap A screwed down at -X |
| avionics_deck | 165 | Adafruit MPU-6050 (#3886) on standoffs centred on the spin axis; XIAO ESP32-S3 on a 50 x 35 mm carrier in short card guides; OP550 sun sensor block on +X |
| top_deck | 223 to 227 | installed flipped: Adafruit QMC5883P (#6388) and BMP280 (#2651) hang on standoffs below the plate, inside the 227 mm envelope |

The three Adafruit sensor boards share one footprint (25.40 x 17.78 mm, four
2.5 mm holes 2.54 mm in from the corners, chip at the board centre; taken from
Adafruit's Eagle board files on GitHub). Each sits on four printed standoffs
4 mm tall, held by M2 x 8 screws threaded straight into blind pilot
holes (drawn 2.4 mm, set by `hole_ladder`), so each board comes off from above. The screws are steel and move with
the magnetometer, so calibration (T-9) absorbs them; nylon or brass M2 screws on
the QMC5883P are an optional improvement.
The MPU-6050 is centred on the spin axis with its STEMMA QT connectors facing
+-X. The I2C bus chains XIAO -> MPU-6050 -> QMC5883P -> BMP280 with STEMMA QT
cables. Each board has its own 3.3 V regulator and 10 k pull-ups; power them
from the XIAO's 3V3 pin. Default addresses: MPU-6050 0x68, QMC5883P 0x2C,
BMP280 0x77 (Adafruit library default).

The XIAO carrier (50 x 35 mm, cut from a 5 x 7 cm perfboard) slides into the
short guides from -Y and stops 2 mm short of the MPU-6050 board. Put the XIAO
at the -Y edge with USB-C facing out, and the Hall pull-up, battery divider,
OP550 load resistors, and 1N5817 on the carrier. The power-deck perfboard keeps
its full-length guides. Glue or tie both perfboards in place.

## Assembly order

1. Press both 608s into the bottom cap: the lower one from underneath, the upper one from the top.
2. Press #8-32 lock nuts into the four traps under the bottom cap, nylon side up, and thread in the rods.
3. Screw the motor to the motor deck from below, then fit the Hall bracket and set the nominal 2.5 mm from magnet face to sensor face.
4. Mount and balance the flywheel (see below), then drop the guard ring into the motor deck groove.
5. Build each deck's electronics off the stack, then slide the decks down the rods in order.
6. Finish with the top deck, an M4 washer and a lock nut on each rod. Snug, not tight.
7. Screw the base hub to a board, drop the M8 bolt in from below, clamp the board to the bench, and shim it level.
8. Lower the stack onto the bolt. The lower bearing's inner ring rests on the hub pad.
9. For every spin-up: turn the box until the hole on the bottom cap's +X side lines up with the hole in the hub flange, drop the lock pin in through the open bottom bay, arm, and spin up. Pull the pin out by its tab once the ground station shows Ready. The pin carries about 1 N of shear at full motor torque.

## Checked by `assembly.py`

No overlaps among 47 solids: the printed parts including the battery holder,
the inserted lock pin, the three Adafruit boards with their real hole
patterns, an M2 screw in every hole, the eight AA cells, every battery contact
screw with its washers and nut, and stand-ins for the motor, 63.5 mm wheel,
bearings, bolt, ESC, perfboards, and rods. Six positive controls run every time
(a battery holder 2.5 mm too tall, AA cells 1 mm too long for the contacts, a
78 mm wheel, a board seated 1 mm low, a screw 1.5 mm off the hole pattern, and
an M2 screw 1 mm too long); the script exits with an error if any is missed.

| Clearance | Value |
|---|---|
| Bolt tip below the motor deck | 3.4 mm |
| Bolt engagement in the upper bearing | 7.0 of 7.0 mm |
| Flywheel nut stack below the power deck | 8.2 mm (motor body assumed 31 mm tall) |
| Hall post top below the washer flywheel | 9.0 mm |
| Component height above the power board | 19.4 mm |
| Battery holder top below the avionics deck | 2.3 mm |

## CAD model for the main UI

`cad/export_ui_model.py` exports the assembly's placed parts as binary glTF to
`frontend/public/assets/direcsun.glb`, which the ground station loads
(`frontend/src/ground/RealCubeSatModel.tsx`). `tools/build-cad.sh` writes it
after a clean assembly check and records it in `cad/out/manifest.json`, so
`tools/check-cad-stale.py` flags it when any part script or `stack.py` changes.
Never edit the GLB by hand.

| Item | Value |
| --- | --- |
| Frame | glTF Y up, metres. CAD (x, y, z) mm maps to glTF (-y, z, -x) / 1000, a rotation with no mirror. At heading 0, CAD +X (the sun-sensor face) points north (-Z in the UI) and the stack stands up +Y; the origin is on the spin axis at the bottom cap's underside |
| Nodes | One top-level node per part (the UI sorts only top-level nodes). 41 nodes, about 58,000 triangles, about 1.8 MB |
| Fixed nodes | `base_hub` and `bolt`: the only names matching the UI's fixed pattern `/^(base\|fixed\|hub\|bolt\|stand)/i`. The export refuses to write a file in which any other node would match, or either of these would not |
| Wheel | Node `wheel` (the washer stack with the shaft and nuts), so the UI can spin it later. The motor stand-in is node `motor` |
| Left out | The lock pin (pulled before the box turns; a turning model would cut through the fixed hub) and the threaded rods (inside the corner columns; `assembly_view.stl` leaves them out too) |
| Materials | By material, all non-metallic because the UI has no environment map: printed parts light grey, the black PLA avionics deck dark grey, the base hub mid grey, steel hardware, motor, boards, and cells each their own colour |

The UI only tries to load the model when `cubesatModel.available` is `true` in
`frontend/src/data/assets.ts`; until that flag is set the ground station keeps
its schematic.

## Flywheel

The flywheel is not printed. It is three 2.5 in (63.5 mm) steel fender washers
clamped to the bell by the prop nut, with a jam nut and thread-locker, because
nothing printed should spin with the wheel. Baseline speeds: 2400 rpm bias,
4000 rpm cap (5.7 J stored). The 50 mm fallback set runs at 3000 / 6000 rpm.
All figures come from `docs/sizing.py`; see the technical design document.

Imbalance force and energy for both wheel options are tabulated in
`docs/sizing_output.txt`. At the baseline cap, a 0.2 mm stack offset gives
about 4.1 N of rotating force.

The box weighs about 7 N, so an unbalanced stack at the cap shakes it with
more than half its own weight. That vibration also lands in the gyro.

1. Centre the washers. Use washers whose hole is a snug fit on the prop thread, or wrap the thread with one turn of tape until they slide on without play.
2. Glue the two magnets to the bell exactly opposite each other, both south pole out (the A1104 is a unipolar switch).
3. Balance before closing the stack. Spin up to the bias speed, log the MPU6050 accelerometer, rotate the top washer in 45 degree steps, and keep the quietest position. The Hall pulse gives a once-per-turn phase reference if you want to do single-plane balancing properly.
4. Set the MPU6050 digital low-pass filter to 20 Hz (DLPF_CFG = 4). It only partly attenuates the 40 Hz wheel vibration at bias, so balancing still matters; a software notch at the wheel frequency removes the rest.
5. Keep the 4000 rpm cap. The rim moves at about 13 m/s there and stores about 5.7 J. The guard ring keeps fingers out and catches a washer if the nut backs off.

**Boards in the card guides.** If the wheel stops from the 4000 rpm cap, for example after an e-stop,
the box can reach 4.0 turns per second, which puts about 2.2 g on a board
35 mm from the axis. Friction will not hold a board against that. Fix each board with a dab of hot glue at the open end.

## Measure before printing

These dimensions came from sellers' listings, which disagree, or from typical
sizes. Check yours and edit `stack.py` or the part script.

- **2212 motor:** 16 x 19 mm M3 pattern is standard for 22xx; confirm. Bell diameter assumed 28 mm, body height 31 mm.
- **ESC:** sold 35A BLHeli_S ESCs range from 28 x 13 to 36 x 18 mm; the zip-tie layout covers that range.
- **AA cell length:** measured 50.17 mm tip to tip (Oct 10); `CELL_L` is set, the holder body is 52.57 mm long, and each contact spring is pushed out 0.45 mm.
- **Motor shaft (open problem):** the thread is 6.76 mm long and 5.81 mm across (M6). That is too short for the washer stack plus prop nut and jam nut described under Flywheel, and the washers on hand have 8.93 and 10.59 mm holes (measured Oct 10). The flywheel is being redesigned as a printed PETG wheel with the washers sealed inside; washer_stack_test rehearses that.
- **Hole gauge:** print it and check every hole before the decks (see the print list).
- **#8-32 lock nut:** 11/32 in (8.73 mm) across flats, height assumed 4.4 mm. If yours is taller than 4.6 mm, raise `ROD_NUT_H` in `stack.py`.
- **Perfboard:** 50 x 70 x 1.6 mm assumed.

## Battery holder

Eight AA cells sit in two rows of four bores in `battery_holder_body`. They
alternate direction so the end caps can wire them in series: follow the "+"
and "-" marks on the body's top face for the top row; in every column the
bottom cell points the other way (the bottom face is marked too). End A is
marked with a triangle.

Each cell end presses on the dome of an M3 x 8 button-head screw that sits in
a disc on a printed spring ring in the cap. The ring is 1.6 mm thick; the
1.4 mm test sample on the hole gauge returned but felt weak. Outside the cap, stack a washer, a
wire loop, a washer, and a nut on each screw. Wire it like this, viewed from
outside each cap:

- Cap B: join the top and bottom contact in each of the four columns.
- Cap A: join top row columns 1-2, bottom row columns 2-3, and top row columns 3-4.
  The pack + is column 1 bottom and the pack - is column 4 bottom; both leads
  leave through the side notches and drop through the deck's wire slots.

Columns are numbered from the -Y side of the box (cap A faces -X). Looking at
cap A from outside, column 1 is on your right; looking at cap B from outside,
it is on your left.
Before closing the stack, measure the pack with a meter: about 10 to 11 V
with charged cells. A single reversed cell reads about 2.5 V low, which the
firmware's 9.6 V arm check also refuses. Leave a little slack in each bridge
wire so it does not hold the spring. To charge, take off cap B (three M2
screws) and slide the cells out.

## Print plan and times

Times are OrcaSlicer 2.3 estimates for the Creality K1C with the
`pocketadcs-petg` profile (0.2 mm layers, 4 walls, 40% cubic infill) and
PETG-K1C filament, sliced Oct 10 2026 from the current STLs; the washer test
and flywheel use 100% infill. Two parts on one plate take the sum of their
times (checked: two decks together 3 h 47 min against 3 h 45 min apart), so
plates only save trips to the printer. Lighter deck settings (3 walls, 20%)
saved only 3 to 6%.

Already printed: bearing_fit_test, hole_gauge, hole_ladder.

Ready now (about 7 h 52 min). Plates 1 to 3 depend on no outstanding
measurement or test; plate 4 depends only on the motor height check below.
base_hub and the card guides were updated Oct 10, so load the current STLs:

| Plate | Parts | Kind | Time |
|---|---|---|---|
| 1 | washer_stack_test (needs someone at the pause) | test | 14 min |
| 2 | bottom_cap + base_hub + lock_pin | parts | 2 h 40 min |
| 3 | battery_holder_body + battery_cap_a + battery_cap_b | parts | 2 h 58 min |
| 4 | battery_deck + top_deck | parts | 2 h 01 min |

After one more check (about 1 h 55 min):

| Plate | Parts | Waits on | Time |
|---|---|---|---|
| 5 | avionics_deck | the actual perfboard width and thickness (guides assume 50 x 1.6 mm) | 1 h 55 min |

Battery deck and everything above it stay put as long as the motor measures
37 mm or less from its mounting face to the top of its spokes; a taller motor
would push the power deck up into the battery bay. Measure that before plate 4
if possible.

Held until the motor is measured (about 5 h 44 min). The motor's height can
move the power deck, which changes the motor deck, power deck, and guard:

| Plate | Parts | Kind | Time |
|---|---|---|---|
| 6 | motor_deck + hall_bracket | parts | 2 h 05 min |
| 7 | power_deck + flywheel_guard | parts | 2 h 21 min |
| 8 | flywheel hub thread test (not designed yet; sliced as a 30 x 10 mm stand-in) | test | about 12 min |
| 9 | flywheel (not designed yet; sliced as a solid 68 x 18 mm stand-in, needs someone at the pause) | part | about 1 h 06 min |

Total: about 15 h 31 min of printing, plus a few minutes per plate change.

Two printers (Oct 10). PETG stays on the K1C for everything that spins,
flexes, carries the bearings, or sits by the motor: base_hub, lock_pin,
battery caps, bottom_cap, washer test, hub thread test, flywheel, motor_deck,
hall_bracket, flywheel_guard. A Bambu P1S prints PLA (3 walls, 20% infill,
55 C textured plate) for battery_holder_body, battery_deck, top_deck,
avionics_deck, power_deck. Hole compensation was tuned on the K1C, so print
hole_ladder on the P1S first and check the rod slot. P1S estimates: hole_ladder
17 min; holder body + battery deck + top deck 3 h 36 min; power deck 1 h 19 min.

Bed temperature: slice PETG on the K1C at 80 C. The first overnight plate
was sliced on Orca's Cool Plate setting (60 C bed) and failed with a blob
around the hotend.
