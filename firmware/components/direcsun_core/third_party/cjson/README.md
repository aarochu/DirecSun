# cJSON (vendored)

Ultralightweight JSON parser in ANSI C, vendored so the firmware and the host
tests compile the same parser (changes/3-teleop/R3.5-comms.md). The firmware no
longer depends on the `espressif/cjson` registry component.

| Item | Value |
| --- | --- |
| Upstream | https://github.com/DaveGamble/cJSON |
| Version | 1.7.19 (`cJSON.c`, `cJSON.h`, `LICENSE`), copied unchanged from the `espressif/cjson` 1.7.19~1 registry component this repo used before |
| License | MIT, see `LICENSE` |
| sha256 `cJSON.c` | `298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d` |
| sha256 `cJSON.h` | `25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee` |

Both builds define `CJSON_NESTING_LIMIT=8`: inbound messages are at most 512
bytes and nest at most 4 levels (`set_params` with `mag_cal`), so a deeper
document is malformed, and the low limit bounds the parser's recursion on the
WebSocket task's stack.

Do not edit these files. To upgrade, replace all three from a tagged release and
update this table.
