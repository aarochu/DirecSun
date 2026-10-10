"""Export the assembly's viewing model as the main UI's CAD model (binary glTF).

    python cad/export_ui_model.py OUT.glb

tools/build-cad.sh runs this after the assembly check and writes
frontend/public/assets/direcsun.glb, which the ground station loads
(frontend/src/data/assets.ts, frontend/src/ground/RealCubeSatModel.tsx).

What the UI does with the file, read from its source:
- three.js, Y up. Its camera framing (frameCamera) puts east on +X and north on
  -Z. The schematic draws body +X pointing north at heading 0, and the model
  turns with bodyGroup.rotation.y = -heading, so positive heading is clockwise
  seen from above.
- Only the top-level nodes of the scene are sorted: a node whose name matches
  /^(base|fixed|hub|bolt|stand)/i stays fixed, every other node turns with the
  box. So every part is its own top-level node, and only the base hub and the
  bolt carry fixed names.
- The camera frames the scene's bounding box, so units are free; glTF metres
  are used.

Mapping from the CAD frame (mm; +Z up the spin axis, +X the sun-sensor face,
right-handed) to glTF (m): (x, y, z) -> (-y, z, -x) / 1000. It is a proper
rotation (no mirror): CAD +X points north (-Z) and CAD +Z points up (+Y) at
heading 0. The origin stays on the spin axis, at the underside of the bottom
cap.

Left out on purpose: the lock pin (it is pulled before the box turns, and a
turning model would cut through the fixed hub) and the threaded rods (hidden
inside the corner columns; assembly_view.stl leaves them out too).

The file is written deterministically (fixed node order, fixed JSON layout),
so an unchanged assembly rebuilds to the same bytes and tools/check-cad-stale.py
can track it.
"""
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import assembly  # noqa: E402  (placed_parts, hardware)

# Tessellation: coarser than assembly_view.stl (0.05 mm, 0.2 rad), which is for
# collision previews; this keeps the web model small and still round.
TOLERANCE_MM = 0.1
ANGULAR_TOLERANCE_RAD = 0.3

# Materials, by what the parts are made of. Metallic is 0 everywhere: the UI
# lights the scene with a hemisphere and a directional light and no environment
# map, so metallic surfaces would render nearly black.
MATERIALS = {
    "petg_fixed": ((0.42, 0.44, 0.47), 0.75),   # base hub, bolted to the bench
    "printed":    ((0.80, 0.82, 0.84), 0.70),   # PETG and PLA decks, caps, holder, guard
    "pla_black":  ((0.20, 0.21, 0.23), 0.65),   # avionics deck (black PLA, TD section 4.1)
    "steel":      ((0.60, 0.63, 0.66), 0.45),   # bolt, bearings, screws, contacts, wheel washers
    "motor":      ((0.28, 0.30, 0.33), 0.55),   # 2212 motor stand-in
    "pcb":        ((0.16, 0.24, 0.20), 0.60),   # perfboards, sensor boards, XIAO, ESC
    "cells":      ((0.33, 0.40, 0.50), 0.55),   # AA NiMH cells
}

PRINTED = {"bottom_cap", "motor_deck", "hall_bracket", "flywheel_guard", "power_deck", "battery_deck",
           "battery_holder_body", "battery_cap_a", "battery_cap_b", "top_deck"}
FIXED_NAME = ("base", "fixed", "hub", "bolt", "stand")   # frontend/src/data/assets.ts fixedNodePattern


def material_for(name):
    if name == "base_hub":
        return "petg_fixed"
    if name in PRINTED:
        return "printed"
    if name == "avionics_deck":
        return "pla_black"
    if name == "motor":
        return "motor"
    if name == "aa_cells":
        return "cells"
    if name.startswith(("board_", "xiao", "esc")) or name in ("mpu6050", "qmc5883p", "bmp280"):
        return "pcb"
    return "steel"


def ui_nodes():
    """(node name, workplane) in a fixed order: printed parts, then hardware."""
    parts = assembly.placed_parts()
    hw = assembly.hardware()
    nodes = []
    for name, solid in parts.items():
        if name == "lock_pin":
            continue
        nodes.append((name, solid))
    for name, solid in hw.items():
        if name.startswith("rod_") or name in ("washer_flywheel", "shaft_and_nuts", "motor_2212"):
            continue
        if name.startswith("bolt_"):
            name = "bolt"           # fixed by name, see FIXED_NAME
        nodes.append((name, solid))
    # the parts that spin with the wheel, as one node the UI could turn later
    nodes.append(("wheel", hw["washer_flywheel"].union(hw["shaft_and_nuts"])))
    nodes.append(("motor", hw["motor_2212"]))
    for name, _ in nodes:
        fixed = name.lower().startswith(FIXED_NAME)
        if fixed != (name in ("base_hub", "bolt")):
            raise SystemExit(f"export_ui_model: node {name!r} would be {'fixed' if fixed else 'turning'} in the UI")
    return nodes


def to_gltf(points_mm):
    """CAD (x, y, z) in mm -> glTF (-y, z, -x) in m."""
    p = np.asarray(points_mm, dtype=np.float64)
    return np.stack([-p[:, 1], p[:, 2], -p[:, 0]], axis=1) / 1000.0


def mesh_of(workplane):
    shape = workplane.val() if len(workplane.vals()) == 1 else workplane.combine().val()
    verts, tris = shape.tessellate(TOLERANCE_MM, ANGULAR_TOLERANCE_RAD)
    if not tris:
        raise SystemExit("export_ui_model: a solid produced no triangles")
    pos = to_gltf([(v.x, v.y, v.z) for v in verts])
    idx = np.asarray(tris, dtype=np.int64)
    # Vertex normals averaged over the triangles that share a vertex. CadQuery
    # does not share vertices between B-rep faces, so cylinders shade smoothly
    # and edges between faces stay sharp.
    a, b, c = pos[idx[:, 0]], pos[idx[:, 1]], pos[idx[:, 2]]
    tri_n = np.cross(b - a, c - a)
    nrm = np.zeros_like(pos)
    for k in range(3):
        np.add.at(nrm, idx[:, k], tri_n)
    length = np.linalg.norm(nrm, axis=1, keepdims=True)
    nrm = np.where(length > 0, nrm / np.where(length > 0, length, 1.0), np.array([0.0, 1.0, 0.0]))
    return pos.astype(np.float32), nrm.astype(np.float32), idx


def write_glb(path, nodes):
    gltf = {"asset": {"version": "2.0", "generator": "DirecSun cad/export_ui_model.py"},
            "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [], "materials": [],
            "accessors": [], "bufferViews": [], "buffers": [{"byteLength": 0}]}
    bin_chunks = []
    offset = 0
    mat_index = {}
    for key, (rgb, rough) in MATERIALS.items():
        mat_index[key] = len(gltf["materials"])
        gltf["materials"].append({"name": key, "pbrMetallicRoughness": {
            "baseColorFactor": [rgb[0], rgb[1], rgb[2], 1.0], "metallicFactor": 0.0, "roughnessFactor": rough}})

    def add_view(data, target):
        nonlocal offset
        raw = data.tobytes()
        pad = (-len(raw)) % 4
        bin_chunks.append(raw + b"\x00" * pad)
        gltf["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(raw), "target": target})
        offset += len(raw) + pad
        return len(gltf["bufferViews"]) - 1

    stats = []
    for name, workplane in nodes:
        pos, nrm, idx = mesh_of(workplane)
        n_vert = len(pos)
        if n_vert < 65536:
            ind, ctype = idx.astype(np.uint16).ravel(), 5123
        else:
            ind, ctype = idx.astype(np.uint32).ravel(), 5125
        a_pos = len(gltf["accessors"])
        gltf["accessors"].append({"bufferView": add_view(pos, 34962), "componentType": 5126, "count": n_vert,
                                  "type": "VEC3", "min": [float(v) for v in pos.min(axis=0)],
                                  "max": [float(v) for v in pos.max(axis=0)]})
        a_nrm = len(gltf["accessors"])
        gltf["accessors"].append({"bufferView": add_view(nrm, 34962), "componentType": 5126,
                                  "count": n_vert, "type": "VEC3"})
        a_ind = len(gltf["accessors"])
        gltf["accessors"].append({"bufferView": add_view(ind, 34963), "componentType": ctype,
                                  "count": int(ind.size), "type": "SCALAR"})
        gltf["meshes"].append({"name": name, "primitives": [{
            "attributes": {"POSITION": a_pos, "NORMAL": a_nrm}, "indices": a_ind,
            "material": mat_index[material_for(name)]}]})
        gltf["nodes"].append({"name": name, "mesh": len(gltf["meshes"]) - 1})
        gltf["scenes"][0]["nodes"].append(len(gltf["nodes"]) - 1)
        stats.append((name, material_for(name), n_vert, len(idx)))

    gltf["buffers"][0]["byteLength"] = offset
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    blob = b"".join(bin_chunks)
    total = 12 + 8 + len(js) + 8 + len(blob)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(js), b"JSON"))
        f.write(js)
        f.write(struct.pack("<I4s", len(blob), b"BIN\x00"))
        f.write(blob)
    return stats, total


def main(argv):
    if len(argv) != 2:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 64
    stats, size = write_glb(argv[1], ui_nodes())
    tris = sum(s[3] for s in stats)
    fixed = [s[0] for s in stats if s[0].lower().startswith(FIXED_NAME)]
    print(f"export_ui_model: {len(stats)} nodes, {tris} triangles, {size} bytes -> {argv[1]}")
    print(f"export_ui_model: fixed nodes {', '.join(fixed)}; the wheel is node 'wheel'")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
