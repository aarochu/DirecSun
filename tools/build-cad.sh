#!/usr/bin/env bash
# Build the printed parts into cad/out/. This is the only producer of cad/out:
# never edit an STL or preview by hand.
#
# Usage: tools/build-cad.sh             every part script, then the assembly check
#        tools/build-cad.sh PART...     only those parts (names without .py);
#                                       add "assembly" to also run the assembly check
#
# Each part goes through the cad-skill runner with --preview --strict. A part
# fails the build if the runner reports failure, a non-watertight mesh, or no
# STL. The runner only finds outputs in the script's own directory, so the part
# scripts keep writing next to themselves and this script moves the files into
# cad/out/ (STEP too, where it is gitignored). cad/out/manifest.json records the
# source hashes that tools/check-cad-stale.py compares against.
#
# After the assembly check, cad/export_ui_model.py writes the main UI's CAD
# model, frontend/public/assets/direcsun.glb, which the manifest tracks too.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="$ROOT/.venv/bin/python"
RUNNER="$ROOT/.agents/skills/cad-skill/run_cadquery_model.py"
PREVIEW="$ROOT/.agents/skills/cad-skill/preview.py"
CAD="$ROOT/cad"
OUT="$CAD/out"
STALE="$ROOT/tools/check-cad-stale.py"
UI_MODEL="frontend/public/assets/direcsun.glb"   # repo-relative, as recorded in the manifest

die() { echo "build-cad: $*" >&2; exit 1; }

PREV=""
# place FILE...: move one part's build outputs into cad/out and print the names
# to record in the manifest (STEP is moved but not recorded: it is not committed).
# Preview renders are not deterministic (a few pixels differ between runs of the
# same mesh), but STL export is byte-for-byte reproducible. So a preview is
# replaced only when its part's STL actually changed; otherwise the existing
# preview is kept, and a rebuild with no geometry change leaves cad/out untouched.
place() {
  "$PY" - "$OUT" "${PREV:-$OUT}" "$@" <<'PYEOF'
import hashlib, os, shutil, sys
out, prev, files = sys.argv[1], sys.argv[2], sys.argv[3:]

def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()

stls = [f for f in files if f.endswith(".stl")]
# geometry unchanged = the new STL is byte-identical to the previous one; decide before moving anything
unchanged = {os.path.basename(f) for f in stls
             if os.path.isfile(os.path.join(prev, os.path.basename(f)))
             and sha(f) == sha(os.path.join(prev, os.path.basename(f)))}

def stl_for(preview):
    base = os.path.basename(preview)[: -len("_preview.png")] + ".stl"
    if any(os.path.basename(s) == base for s in stls):
        return base
    return os.path.basename(stls[0]) if len(stls) == 1 else None  # assembly_preview.png <- assembly_view.stl

names = []
for f in files:
    name = os.path.basename(f)
    dest, old = os.path.join(out, name), os.path.join(prev, name)
    if name.endswith("_preview.png") and stl_for(f) in unchanged and os.path.isfile(old):
        if old != dest:
            shutil.move(old, dest)
        os.remove(f)
    else:
        shutil.move(f, dest)
    if not name.endswith((".step", ".stp")):
        names.append(name)
print(" ".join(names))
PYEOF
}

[ -x "$PY" ] || die "no .venv found; run tools/bootstrap-venv.sh first"
[ -f "$RUNNER" ] || die "cad-skill runner missing; run git submodule update --init"

parts=()
run_assembly=0
if [ $# -eq 0 ]; then
  for f in "$CAD"/*.py; do
    b="$(basename "$f" .py)"
    case "$b" in stack|assembly|export_ui_model) continue ;; esac
    parts+=("$b")
  done
  run_assembly=1
  # full build: start from an empty cad/out so removed or renamed parts cannot linger.
  # The previous outputs are parked in PREV only so that unchanged previews can be reused.
  mkdir -p "$OUT"
  PREV="$(mktemp -d)"
  trap 'rm -rf "$PREV"' EXIT
  for f in "$OUT"/*.stl "$OUT"/*.png "$OUT"/*.step; do [ -e "$f" ] && mv "$f" "$PREV/"; done
  rm -f "$OUT"/manifest.json
else
  for p in "$@"; do
    p="${p%.py}"
    case "$p" in
      stack) die "stack.py is the shared module; it produces no part" ;;
      export_ui_model) die "export_ui_model runs with the assembly check; pass \"assembly\"" ;;
      assembly) run_assembly=1 ;;
      *) [ -f "$CAD/$p.py" ] || die "no such part script: cad/$p.py"; parts+=("$p") ;;
    esac
  done
fi
mkdir -p "$OUT"
# an empty part list would make every check below pass vacuously
[ "${#parts[@]}" -gt 0 ] || [ "$run_assembly" -eq 1 ] || die "no part scripts to build"
[ $# -gt 0 ] || [ "${#parts[@]}" -gt 0 ] || die "no part scripts found in cad/"

built=0
for p in "${parts[@]}"; do
  json="$CAD/.run_$p.json"
  if ! "$PY" "$RUNNER" "$CAD/$p.py" --preview --strict > "$json"; then
    echo "part $p: runner failed (full JSON in cad/.run_$p.json)" >&2
    "$PY" -c 'import json,sys; r=json.load(open(sys.argv[1])); sys.stderr.write(r.get("stderr","")[-3000:])' "$json" || true
    exit 1
  fi
  # check the JSON, move outputs into cad/out, print one result line, list committed outputs
  outputs="$("$PY" - "$json" "$p" <<'PYEOF'
import json, sys
res = json.load(open(sys.argv[1]))
stls, previews, steps = res["stls"], res["previews"], res["steps"]
ok = res["success"] and res["watertight"] is True and len(stls) > 0 and len(previews) == len(stls)
print(f"part {sys.argv[2]}: success={str(res['success']).lower()} watertight={str(res['watertight']).lower()} "
      f"stls={len(stls)} previews={len(previews)} steps={len(steps)}", file=sys.stderr)
if not ok:
    sys.exit(1)
print(" ".join(stls + previews + steps))
PYEOF
)" || die "part $p failed its checks (see the result line above)"
  # shellcheck disable=SC2086
  outputs="$(place $outputs)"
  # shellcheck disable=SC2086
  "$PY" "$STALE" --write "$p.py" $outputs
  built=$((built + 1))
done

[ "$built" -eq "${#parts[@]}" ] || die "built $built of ${#parts[@]} part scripts"
echo "build-cad: built $built/${#parts[@]} part scripts"

if [ "$run_assembly" -eq 1 ]; then
  log="$CAD/.assembly.log"
  set +e
  (cd "$CAD" && "$PY" assembly.py) > "$log" 2>&1
  rc=$?
  set -e
  cat "$log"
  [ "$rc" -eq 0 ] || die "assembly.py exited $rc (2 means a positive control was missed)"
  grep -q '^No overlaps among [0-9]* solids' "$log" || die "assembly.py did not report a clean collision check"
  if grep -q '^OVERLAP\|ENVELOPE VIOLATION' "$log"; then die "assembly.py reported overlaps or an envelope violation"; fi
  n_ctrl="$(grep -c '^positive control,' "$log" || true)"
  n_det="$(grep -c '^positive control,.*: DETECTED$' "$log" || true)"
  [ "$n_ctrl" -gt 0 ] && [ "$n_ctrl" -eq "$n_det" ] || die "positive controls: $n_det of $n_ctrl detected"
  # the viewing model is a multi-body compound and is not watertight by design, so no --strict here
  "$PY" "$PREVIEW" "$CAD/assembly_view.stl" "$CAD/assembly_preview.png" \
    --title "DirecSun assembly (viewing model)" > "$CAD/.asm_preview.log" 2>&1 \
    || { cat "$CAD/.asm_preview.log"; die "assembly preview failed"; }
  asm_outputs="$(place "$CAD/assembly_view.stl" "$CAD/assembly_preview.png" "$CAD/assembly.step")"
  # shellcheck disable=SC2086
  "$PY" "$STALE" --write assembly.py $asm_outputs
  echo "build-cad: assembly check passed, $n_det/$n_ctrl positive controls detected"
  # the main UI's CAD model, from the same placed parts (only after a clean assembly check)
  ui_log="$CAD/.ui_model.log"
  "$PY" "$CAD/export_ui_model.py" "$ROOT/$UI_MODEL" > "$ui_log" 2>&1 || { cat "$ui_log"; die "UI model export failed"; }
  cat "$ui_log"
  ui_bytes="$(wc -c < "$ROOT/$UI_MODEL")"
  [ "$ui_bytes" -lt 5000000 ] || die "$UI_MODEL is $ui_bytes bytes; the UI budget is under 5 MB"
  "$PY" "$STALE" --write export_ui_model.py "$UI_MODEL"
fi

"$PY" "$STALE"
