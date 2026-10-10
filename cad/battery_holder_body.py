"""Battery holder body: eight AA bores in two rows of four, open at both ends.

The cells are held between two end caps (battery_holder_cap.py) whose spring
contacts press on each cell end. The body length sets the spring preload:
HOLDER_BODY_L = CELL_L + 2 x screw-head height - 2 x CONTACT_PRELOAD, so
measure your longest cell and set CELL_L in stack.py before printing.

Cells alternate direction so the caps can wire them in series. The "+" and
"-" marks engraved on the top face show which way each top-row cell points;
in every column the bottom cell points the other way. Cap A (with the feet
and the pack leads) goes on the end marked A.

Print orientation: standing on end A, bores vertical, no supports.
Material: PETG, 3 walls, 20% infill.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
L = HOLDER_BODY_L
PILOT_DEPTH = 9.0                # M2 x 8 cap screws, through the 2.4 mm cap plate
MARK_DEPTH = 0.5
MARK_LEN, MARK_W = 4.0, 0.8
MARK_INSET = 6.0                 # mark centre from each end
LETTER_INSET = 3.0


def plus(x, z, face_y, depth):
    """Engraved '+' on the face at y = face_y, centred at (x, z)."""
    a = box_at(x - MARK_LEN / 2, x + MARK_LEN / 2, face_y - depth, face_y + depth, z - MARK_W / 2, z + MARK_W / 2)
    return a.union(box_at(x - MARK_W / 2, x + MARK_W / 2, face_y - depth, face_y + depth, z - MARK_LEN / 2, z + MARK_LEN / 2))


def minus(x, z, face_y, depth):
    return box_at(x - MARK_LEN / 2, x + MARK_LEN / 2, face_y - depth, face_y + depth, z - MARK_W / 2, z + MARK_W / 2)


def letter_a(x, z, face_y, depth):
    """A small triangle marking end A (no font needed)."""
    tri = (cq.Workplane("XZ").polyline([(x - 2.5, z - 1.5), (x + 2.5, z - 1.5), (x, z + 2.0)]).close()
           .extrude(-2 * depth).translate((0, face_y - depth, 0)))
    return tri


def build():
    # cross-section in XY (x across the pack, y up), cell axis along z, end A on the bed
    body = box_at(-HOLDER_W / 2, HOLDER_W / 2, 0, HOLDER_H, 0, L).edges("|Z").fillet(1.0)
    for (x, y, _) in cell_centres():
        body = body.cut(cyl(CELL_BORE_D, -1, L + 1, x, y))
    for x in HOLDER_CAP_SCREWS:
        body = body.cut(cyl(M2_PILOT, -0.01, PILOT_DEPTH, x, HOLDER_H / 2))
        body = body.cut(cyl(M2_PILOT, L - PILOT_DEPTH, L + 0.01, x, HOLDER_H / 2))
    # polarity marks: top face for the top row, bottom face for the bottom row
    for (x, y, plus_at_a) in cell_centres():
        top = y > HOLDER_H / 2
        face = HOLDER_H if top else 0.0
        for z, is_a_end in ((MARK_INSET, True), (L - MARK_INSET, False)):
            mark = plus if plus_at_a == is_a_end else minus
            body = body.cut(mark(x, z, face, MARK_DEPTH))
    for face in (0.0, HOLDER_H):
        body = body.cut(letter_a(0.0, LETTER_INSET, face, MARK_DEPTH))
    return body


if __name__ == "__main__":
    export(build(), "battery_holder_body", os.path.dirname(os.path.abspath(__file__)))
