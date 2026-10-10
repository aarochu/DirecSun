#!/usr/bin/env python3
"""Validate DirecSun wire messages against protocol/schema-v1.json.

    tools/check-schema.py                 check the fixtures in protocol/examples
    tools/check-schema.py FILE.jsonl ...  validate every line of each JSON-lines file

Fixture rules, so that a schema which accepts everything, or a fixture that fails
for the wrong reason, cannot pass:

- every file in examples/valid/ must validate;
- every file in examples/invalid/ must fail;
- an invalid fixture is named <base>--<rule>.json, where examples/valid/<base>.json
  exists, and it must differ from that base in exactly one top-level key, so each
  invalid fixture exercises one rule.

A JSON-lines file must contain at least one message, and every line must validate.
Exit status 0 means everything passed.

Needs the jsonschema package. If this interpreter lacks it, the script re-runs
itself with $DIRECSUN_PYTHON or the repo's .venv (tools/bootstrap-venv.sh).
"""
import json
import os
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SCHEMA = REPO / "protocol" / "schema-v1.json"
EXAMPLES = REPO / "protocol" / "examples"


def _reexec_with_repo_python():
    # A venv's python is usually a symlink to the system binary, so compare paths as
    # given, and use an environment marker so a broken venv cannot loop.
    if not os.environ.get("DIRECSUN_REEXEC"):
        for candidate in (os.environ.get("DIRECSUN_PYTHON"), str(REPO / ".venv" / "bin" / "python")):
            if candidate and os.path.exists(candidate) and os.path.abspath(candidate) != os.path.abspath(sys.executable):
                os.environ["DIRECSUN_REEXEC"] = "1"
                os.execv(candidate, [candidate, __file__, *sys.argv[1:]])
    sys.exit("check-schema: the jsonschema package is missing. Run tools/bootstrap-venv.sh "
             "or set DIRECSUN_PYTHON to a Python that has it.")


try:
    from jsonschema import Draft202012Validator
    from jsonschema.exceptions import best_match
except ImportError:
    _reexec_with_repo_python()


class Checker:
    """Validates against the full schema; explains failures with the branch the message claims to be."""

    CMD_BRANCH = {"arm": "cmd_bare", "disarm": "cmd_bare", "estop": "cmd_bare", "zero_heading": "cmd_bare",
                  "mode": "cmd_mode", "manual": "cmd_manual", "respin": "cmd_respin",
                  "set_params": "cmd_set_params", "wheel_step": "cmd_wheel_step"}

    def __init__(self):
        self.schema = json.loads(SCHEMA.read_text())
        Draft202012Validator.check_schema(self.schema)
        self.root = Draft202012Validator(self.schema)
        self.branches = {name: Draft202012Validator({"$ref": f"#/$defs/{name}", "$defs": self.schema["$defs"]})
                         for name in self.schema["$defs"]}

    def _branch_for(self, message):
        if not isinstance(message, dict):
            return None
        kind = message.get("type")
        if kind == "cmd":
            return self.CMD_BRANCH.get(message.get("cmd"))
        if kind == "event":
            return {"mode": "event_mode", "notice": "event_notice"}.get(message.get("event"))
        return kind if kind in ("tlm", "ack", "ping", "pong") else None

    def error(self, message):
        """None if the message is valid, else the most specific error as 'path: message'."""
        best = best_match(self.root.iter_errors(message))
        if best is None:
            return None
        branch = self._branch_for(message)
        if branch:
            specific = best_match(self.branches[branch].iter_errors(message))
            if specific is not None:
                best = specific
        where = "/".join(str(p) for p in best.absolute_path) or "(root)"
        text = best.message if len(best.message) <= 160 else best.message[:157] + "..."
        return f"{where}: {text}"


def check_fixtures(validator):
    failures = []
    valid_files = sorted((EXAMPLES / "valid").glob("*.json"))
    invalid_files = sorted((EXAMPLES / "invalid").glob("*.json"))
    if not valid_files or not invalid_files:
        print(f"check-schema: no fixtures found under {EXAMPLES}", file=sys.stderr)
        return 1

    passed = 0
    for path in valid_files:
        error = validator.error(json.loads(path.read_text()))
        if error:
            failures.append(f"valid fixture rejected: {path.name}: {error}")
        else:
            passed += 1

    rejected = 0
    for path in invalid_files:
        base_name, sep, _rule = path.stem.partition("--")
        base_path = EXAMPLES / "valid" / f"{base_name}.json"
        if not sep or not base_path.exists():
            failures.append(f"invalid fixture {path.name} must be named <valid base>--<rule>.json")
            continue
        message = json.loads(path.read_text())
        base = json.loads(base_path.read_text())
        changed = sorted(k for k in set(message) | set(base) if message.get(k, KeyError) != base.get(k, KeyError))
        if len(changed) != 1:
            failures.append(f"invalid fixture {path.name} differs from {base_path.name} in {changed}; it must differ in exactly one key")
            continue
        error = validator.error(message)
        if error is None:
            failures.append(f"invalid fixture accepted: {path.name}")
        else:
            rejected += 1
            print(f"  rejected {path.name} ({changed[0]}): {error}")

    print(f"check-schema: {passed}/{len(valid_files)} valid fixtures passed, "
          f"{rejected}/{len(invalid_files)} invalid fixtures rejected")
    for failure in failures:
        print(f"check-schema: FAIL {failure}", file=sys.stderr)
    return 1 if failures else 0


def check_jsonl(validator, path):
    lines = [line for line in pathlib.Path(path).read_text().splitlines() if line.strip()]
    if not lines:
        print(f"check-schema: FAIL {path} contains no messages", file=sys.stderr)
        return 1
    bad = 0
    kinds = {}
    for number, line in enumerate(lines, start=1):
        try:
            message = json.loads(line)
        except json.JSONDecodeError as exc:
            print(f"check-schema: FAIL {path}:{number}: not JSON: {exc}", file=sys.stderr)
            bad += 1
            continue
        error = validator.error(message)
        if error:
            print(f"check-schema: FAIL {path}:{number}: {error}: {line}", file=sys.stderr)
            bad += 1
        else:
            key = message.get("type", "?") + (f":{message['cmd']}" if message.get("type") == "cmd" else "")
            kinds[key] = kinds.get(key, 0) + 1
    summary = ", ".join(f"{k} {v}" for k, v in sorted(kinds.items()))
    print(f"check-schema: {path}: {len(lines) - bad}/{len(lines)} messages valid ({summary})")
    return 1 if bad else 0


def main(argv):
    validator = Checker()
    if not argv:
        return check_fixtures(validator)
    status = 0
    for path in argv:
        status |= check_jsonl(validator, path)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
