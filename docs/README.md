# Documents

| File | What it is |
| --- | --- |
| [technical-design.md](technical-design.md) | System design (TD): requirements, physics and sizing, mechanical and electrical design, firmware and ground-station architecture, test plan, risks, schedule, assumptions, and the validation log |
| [firmware-design.md](firmware-design.md) | Firmware design (FD): architecture, drivers, estimation, control, modes, safety, the wire protocol, verification, and the rules for agents changing the firmware |
| [hardware/perfboards.html](hardware/perfboards.html) | Perfboard build sheet: the XIAO carrier and power deck hole by hole, the cables between decks, the battery box wiring, and the checks before first power. Open it in a browser. TD Section 5 and `cad/` win if it disagrees with them |
| [sizing.py](sizing.py) | Every number in TD Section 3. Run `.venv/bin/python docs/sizing.py > docs/sizing_output.txt` after changing an input |
| [sizing_output.txt](sizing_output.txt) | The last run of `sizing.py` |
| [reference/Electronics_Component_Inventory_with_Robotics.pdf](reference/Electronics_Component_Inventory_with_Robotics.pdf) | The owner's parts inventory referenced in TD Section 2 |

## Which source wins

1. Measurements recorded in TD Section 12.2.
2. `cad/stack.py` and `docs/sizing.py`. They win over TD prose.
3. TD.
4. FD.
5. `protocol/schema-v1.json` must agree with FD §15. A disagreement is a bug in whichever changed last.

These documents are authoritative in this repository. They were imported on 2026-10-10 from a Claude Doc export (the TD) and a message (the FD); the first import commit is byte-identical to those originals, and every later change is listed in TD Section 12.4 (repository import) and FD §23.1 (V-22 onward).

## Claim labels

Every statement a reader might act on carries a label: \[V\] verified against an opened source, \[C\] calculated, \[A\] assumed, \[M\] must be measured, and in the FD, \[D\] a design decision. Keep the labels honest when you change a number, and log the change in the document's validation log.
