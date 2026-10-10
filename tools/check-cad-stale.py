#!/usr/bin/env python3
"""Check that cad/out matches the CadQuery sources that produced it.

cad/out/manifest.json records, for every committed output (STL and preview
PNG), the script that produced it, the sha256 of every cad/ module that script
imports (directly or through other cad/ modules, stack.py included), and the
sha256 of the output itself. An output is a file name in cad/out, or a
repo-relative path (containing "/") for an output that lives elsewhere, such as
frontend/public/assets/direcsun.glb from cad/export_ui_model.py. This check
needs only the standard library, so it runs in CI without CadQuery.

    tools/check-cad-stale.py               check; exit 1 naming every problem
    tools/check-cad-stale.py --selftest    prove the check detects a changed
                                           source (for every output, including
                                           those outside cad/out) and a
                                           hand-edited STL
    tools/check-cad-stale.py --write SCRIPT OUTPUT...
                                           record OUTPUTs of cad/SCRIPT in the
                                           manifest (used by tools/build-cad.sh);
                                           an OUTPUT with a "/" is repo-relative

Problems reported: a source changed since the build, the import graph of a
script changed, an output differs from its recorded hash (hand edit), an output
is missing, an output in cad/out has no manifest entry, or a part script has
never been built.
"""
import argparse
import ast
import hashlib
import json
import os
import shutil
import sys
import tempfile

TRACKED_EXTS = (".stl", ".png")
NOT_PARTS = ("stack.py",)  # shared module, produces nothing


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def local_imports(cad_dir, script):
    """Every cad/ module the script imports, transitively, including itself."""
    seen = set()
    todo = [script]
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        seen.add(name)
        with open(os.path.join(cad_dir, name), encoding="utf-8") as f:
            tree = ast.parse(f.read(), filename=name)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                mods = [a.name.split(".")[0] for a in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
                mods = [node.module.split(".")[0]]
            else:
                continue
            for m in mods:
                if os.path.isfile(os.path.join(cad_dir, m + ".py")):
                    todo.append(m + ".py")
    return sorted(seen)


def source_hashes(root, script):
    cad_dir = os.path.join(root, "cad")
    return {f"cad/{m}": sha256(os.path.join(cad_dir, m)) for m in local_imports(cad_dir, script)}


def output_path(root, name):
    """A manifest key is a file name in cad/out, or a repo-relative path if it contains '/'."""
    return os.path.join(root, name) if "/" in name else os.path.join(root, "cad", "out", name)


def load_manifest(root):
    path = os.path.join(root, "cad", "out", "manifest.json")
    if not os.path.isfile(path):
        return {"version": 1, "outputs": {}}
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def write(root, script, outputs):
    manifest = load_manifest(root)
    out_dir = os.path.join(root, "cad", "out")
    sources = source_hashes(root, script)
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    # drop stale entries of this script so renamed or removed outputs do not linger
    manifest["outputs"] = {k: v for k, v in manifest["outputs"].items() if v["script"] != f"cad/{script}"}
    for name in outputs:
        manifest["outputs"][name] = {
            "script": f"cad/{script}",
            "sha256": sha256(output_path(root, name)),
            "sources": sources,
        }
    manifest["outputs"] = dict(sorted(manifest["outputs"].items()))
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")


def check(root):
    """Return a list of human-readable problems; empty means cad/out is current."""
    problems = []
    cad_dir = os.path.join(root, "cad")
    out_dir = os.path.join(cad_dir, "out")
    manifest = load_manifest(root)
    outputs = manifest.get("outputs", {})
    if not outputs:
        return ["cad/out/manifest.json is missing or empty: run tools/build-cad.sh"]

    current_sources = {}
    for name, entry in outputs.items():
        script = entry["script"].split("/", 1)[1]
        path = output_path(root, name)
        if not os.path.isfile(os.path.join(cad_dir, script)):
            problems.append(f"{name}: producing script {entry['script']} no longer exists")
            continue
        if script not in current_sources:
            current_sources[script] = source_hashes(root, script)
        now = current_sources[script]
        if not os.path.isfile(path):
            problems.append(f"{name}: listed in the manifest but missing")
        elif sha256(path) != entry["sha256"]:
            problems.append(f"{name}: content differs from the manifest (edited by hand, or not built by tools/build-cad.sh)")
        changed = sorted(p for p in set(now) | set(entry["sources"]) if now.get(p) != entry["sources"].get(p))
        if changed:
            problems.append(f"{name}: stale, sources changed since the build: {', '.join(changed)}")

    for name in sorted(os.listdir(out_dir)):
        if name.endswith(TRACKED_EXTS) and name not in outputs:
            problems.append(f"{name}: in cad/out but not in the manifest")

    built = {e["script"] for e in outputs.values()}
    for name in sorted(os.listdir(cad_dir)):
        if name.endswith(".py") and name not in NOT_PARTS and f"cad/{name}" not in built:
            problems.append(f"cad/{name}: never built (no outputs in the manifest)")
    return problems


def selftest(root):
    """Positive controls on a copy of the real tree: the check must pass on the
    copy as committed, then fail after a source edit and after an STL edit."""
    outside = sorted(n for n in load_manifest(root).get("outputs", {}) if "/" in n)

    def copy_tree(tmp):
        shutil.copytree(os.path.join(root, "cad"), os.path.join(tmp, "cad"),
                        ignore=shutil.ignore_patterns("__pycache__", "*.step", ".run_*.json", ".*.log"))
        for name in outside:   # outputs that live outside cad/out, e.g. the UI model
            os.makedirs(os.path.dirname(os.path.join(tmp, name)), exist_ok=True)
            shutil.copy2(os.path.join(root, name), os.path.join(tmp, name))

    with tempfile.TemporaryDirectory(prefix="cad-stale-selftest-") as tmp:
        copy_tree(tmp)
        baseline = check(tmp)
        if baseline:
            print("selftest: baseline copy is already stale, so the controls would prove nothing:")
            for p in baseline:
                print(f"  {p}")
            return 1

        with open(os.path.join(tmp, "cad", "stack.py"), "a", encoding="utf-8") as f:
            f.write("\n# selftest: injected change\n")
        after_src = check(tmp)
        src_ok = any("stale" in p and "cad/stack.py" in p for p in after_src)
        print(f"selftest: comment appended to stack.py -> {len(after_src)} problem(s) reported: "
              f"{'DETECTED' if src_ok else 'NOT DETECTED - CHECK IS BROKEN'}")
        # every output outside cad/out must be covered by the same source tracking
        for name in outside:
            hit = any(p.startswith(f"{name}:") and "stale" in p and "cad/stack.py" in p for p in after_src)
            print(f"selftest: stack.py change makes {name} stale -> "
                  f"{'DETECTED' if hit else 'NOT DETECTED - CHECK IS BROKEN'}")
            src_ok = src_ok and hit

        shutil.rmtree(tmp)
        os.makedirs(tmp)
        copy_tree(tmp)
        out_dir = os.path.join(tmp, "cad", "out")
        stl = sorted(n for n in os.listdir(out_dir) if n.endswith(".stl"))[0]
        with open(os.path.join(out_dir, stl), "ab") as f:
            f.write(b" ")
        after_stl = check(tmp)
        stl_ok = any(p.startswith(f"{stl}:") and "content differs" in p for p in after_stl)
        print(f"selftest: one byte appended to cad/out/{stl} -> "
              f"{'DETECTED' if stl_ok else 'NOT DETECTED - CHECK IS BROKEN'}")
        return 0 if (src_ok and stl_ok) else 1


def main():
    default_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=default_root, help="repository root (default: this script's repo)")
    ap.add_argument("--selftest", action="store_true", help="run the positive controls")
    ap.add_argument("--write", nargs="+", metavar=("SCRIPT", "OUTPUT"),
                    help="record OUTPUTs (file names in cad/out) produced by cad/SCRIPT")
    args = ap.parse_args()

    if args.write:
        if len(args.write) < 2:
            ap.error("--write needs a script name and at least one output")
        write(args.root, args.write[0], args.write[1:])
        return 0
    if args.selftest:
        return selftest(args.root)

    problems = check(args.root)
    n = len(load_manifest(args.root).get("outputs", {}))
    if problems:
        print(f"check-cad-stale: {len(problems)} problem(s) in cad/out ({n} outputs in the manifest):")
        for p in problems:
            print(f"  {p}")
        print("Rebuild with tools/build-cad.sh (never edit STL/PNG by hand).")
        return 1
    print(f"check-cad-stale: ok, {n} outputs match their sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
