#!/usr/bin/env bash
# usage: run-scoped.sh <label> <memmax> <worktree>
# Runs tools/host-test.sh --selftest from a clean build dir inside a memory- and
# CPU-capped user scope (4 CPUs like the ubuntu-24.04 runner), sampling the
# scope's compiler count and memory every 0.5 s.
set -u
label=$1; memmax=$2; wt=$3
out=$(dirname "$0")/$label
mkdir -p "$out"
rm -rf "$wt/build/host-tests-selftest"
unit="ci57-$label"
systemd-run --user --scope --quiet -p MemoryMax="$memmax" -p MemorySwapMax=0 -p CPUQuota=400% --unit="$unit" \
    bash -c "cd '$wt' && /usr/bin/time -v tools/host-test.sh --selftest" >"$out/run.txt" 2>&1 &
runner=$!
cg=/sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/app.slice/$unit.scope
for _ in $(seq 1 40); do [[ -d $cg ]] && break; sleep 0.05; done
: >"$out/samples.txt"
while kill -0 $runner 2>/dev/null; do
    if [[ -r $cg/cgroup.procs ]]; then
        n=$(for p in $(cat $cg/cgroup.procs 2>/dev/null); do cat /proc/$p/comm 2>/dev/null; done | grep -c '^cc1plus$')
        echo "$(date +%s.%N) cc1plus=$n mem=$(cat $cg/memory.current 2>/dev/null) peak=$(cat $cg/memory.peak 2>/dev/null) oom_kill=$(grep oom_kill $cg/memory.events 2>/dev/null | awk '{print $2}')" >>"$out/samples.txt"
    fi
    sleep 0.5
done
wait $runner
echo "exit $?" >>"$out/run.txt"
