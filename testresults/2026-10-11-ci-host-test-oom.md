# Test: host-test selftest build in CI, killed with exit 143 (#57)

- Date/time and timezone: 2026-10-11, 02:24 to 02:31 (America/New_York).
- Operators and witnesses: Julian's Claude Code agent (PR-review session). No hardware.
- Julian's explicit go for any motor-turning step: Not applicable. Host build only; no box, ESC or motor.
- Component: `tools/host-test.sh --selftest`, which builds the `direcsun_core` host tests once per mutant (19 mutants since 1cdbaf0, about 560 objects, with ASan and UBSan).
- Firmware branch/commit: `feat/set4/R4.3-hold-and-slew` at b3eebe4 (CI failing); the fix is 8622d57 on the same branch.
- Procedure and acceptance-criterion reference: reproduce the CI failure from issue #57, then show that the fix passes `tools/check-all.sh`, the CI gate (README, Checks).
- Rig setup: this laptop (8 CPUs, 31 GB). Each run went in a systemd user scope capped at 4 CPUs (`CPUQuota=400%`, like the ubuntu-24.04 runner) and 8 GB with no swap (`MemoryMax=8G`, `MemorySwapMax=0`). Every run started from a clean build directory. The script is [`run-scoped.sh`](2026-10-11-ci-host-test-oom/run-scoped.sh).
- Instruments, calibration or positive-control check:
  - A sampler read the scope's `cgroup.procs` every 0.5 s and counted `cc1plus` processes, along with `memory.current`, `memory.peak` and the `oom_kill` count.
  - It saw compilers in both runs (538 and 8 at peak), so it can see them.
  - The OOM kill itself came from the user journal: `ci57-barej-8G.scope: A process of this unit has been killed by the OOM killer` and `Failed with result 'oom-kill'`.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result |
| --- | --- | --- | --- | --- |
| 1 | CI, run 38110344385 at b3eebe4 ([log](2026-10-11-ci-host-test-oom/ci-38110344385-checks-b3eebe4-killed.log)) | `host-tests` PASS, then `host-tests-selftest` killed at 77 s: "Process completed with exit code 143", "The runner has received a shutdown signal" | Pass | Fail (the reported bug) |
| 2 | CI, run 38106273970 at 7bd6562, before the 19th mutant ([log](2026-10-11-ci-host-test-oom/ci-38106273970-checks-7bd6562-passed.log)) | `host-tests-selftest` PASS after 2 min 38 s | Pass | Pass (control: the same job passed one mutant earlier) |
| 3 | Locally, unbounded `cmake --build -j` (as in b3eebe4), 4 CPUs and 8 GB ([samples](2026-10-11-ci-host-test-oom/1-unbounded-j-samples.txt), [output](2026-10-11-ci-host-test-oom/1-unbounded-j-run.txt)) | Up to **538 `cc1plus` at once**. Memory rose about 0.6 GB/s and was still rising at 8.29 GB when the OOM killer fired at 16 s. The scope was then stopped, exit **143** | Reproduces the CI failure | Fail, reproduced |
| 4 | Locally, the fix: `-j "$jobs"`, with `$jobs` from `CMAKE_BUILD_PARALLEL_LEVEL` or the CPU count (8 here); same scope ([samples](2026-10-11-ci-host-test-oom/2-capped-j-samples.txt), [output](2026-10-11-ci-host-test-oom/2-capped-j-run.txt)) | At most **8 `cc1plus` at once**, peak 3.06 GB, no OOM kill, 4 min 3 s. 158 tests passed and `selftest_must_fail` failed as designed. All **19 of 19** mutants caught. Largest single process 330 MB | Pass with no OOM | Pass |
| 5 | `tools/check-all.sh` on the fixed tree ([output](2026-10-11-ci-host-test-oom/3-check-all-after-fix.txt)) | 9 of 9 steps PASS | 9 of 9 | Pass |
| 6 | CI on 8622d57, run 38118400394 | `checks`, `firmware` and `frontend` all pass | Green | Pass |

## Outcome

- Overall: **Pass.** The bug is reproduced and the fix verified. A bare `-j` gives make no job limit, so the selftest build started about 540 compilers at once and ran out of memory.
- What is inferred, not measured: the runner has 16 GB. With 538 compilers of up to 330 MB each, demand far exceeds 16 GB, but no run was made with a 16 GB cap.
- Faults: none in the tests themselves. Only the build or the runner died.
- Follow-up owner and action:
  - Done in 8622d57, and #57 is closed.
  - `main` and the Set 3 branches still use a bare `-j`, but they build fewer mutants and pass. They get the fix when Set 4 merges.
  - Bringing 8622d57 up into `feat/set4/R4.1-ui-command-contract` is #80 P0.1.
