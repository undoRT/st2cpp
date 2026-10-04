#!/usr/bin/env bash
# Build st2cpp and turn this example into an undoPLC runtime you can run locally.
#
#   ./build.sh              transpile, compile, link, then try to start
#   ./build.sh --no-run     stop after linking
#
# What it demonstrates: a single task calling SEVERAL programs, in the declared
# order, each with its own instance. Small on purpose: one Master plus one Worker
# is two isolated CPUs, which is the minimum that can actually be started, so this
# example runs on an ordinary development machine instead of only on a RT box.
#
# Starting the PLC needs root and the isolated CPUs (one per Master plus one per
# task, so 2 here). Root is not optional: undoPLC needs SCHED_FIFO priorities, and
# it must pin the isolated cores to their nominal frequency via cpufreq. The
# runtime checks both at startup and refuses to start if either is missing.
set -euo pipefail
cd "$(dirname "$0")"

REPO="$(cd ../.. && pwd)"
BUILD="${REPO}/build"
RUN=1
[ "${1:-}" = "--no-run" ] && RUN=0

echo "== building st2cpp CLI =="
# Configure output is suppressed: a first run prints an unrelated Boost warning
# about the install, which has nothing to do with this example.
cmake -S "$REPO" -B "$BUILD" >/dev/null 2>&1 || { echo "cmake configure failed"; exit 1; }
cmake --build "$BUILD" --target st2cpp -j "$(nproc)" >/dev/null
ST2CPP="$BUILD/st2cpp"

echo
echo "== tasks.json: one task, several programs =="
python3 - <<'EOF'
import json
cfg = json.load(open("tasks.json"))
print(f"schema {cfg.get('$schemaVersion', '(omitted -> 1.0)')}")
by_plc = {}
for t in cfg["tasks"]:
    by_plc.setdefault(t["plc"], []).append(t)
for plc, tasks in by_plc.items():
    cycle = min(t["cycle_ms"] for t in tasks)
    prio = max(t["priority"] for t in tasks) + 1
    print(f"\nPLC '{plc}': {len(tasks)} task(s) -> Master cycle {cycle} ms, priority {prio}")
    for t in tasks:
        progs = t.get("programs", [])
        print(f"  {t['name']:<10} every {t['cycle_ms']:>4} ms  prio {t['priority']:<3} "
              f"cpu {t.get('cpu_affinity', -1):<3} programs {progs if progs else '(none)'}")
EOF

echo
echo "== transpiling: modular project + undoPLC runtime =="
rm -rf generated
"$ST2CPP" --workspace ./src --project-style --tasks tasks.json --output-dir generated --strict

echo
echo "== what each Worker calls, in the declared order =="
awk '/^class /{cls=$2} /_program_[A-Z]+\.run\(\);/{printf "  %-22s %s\n", cls, $1}' generated/Runtime.cpp

echo
echo "== each task owns its own program instances =="
awk '/^class /{isWorker = ($0 ~ /UndoWorkerTaskBase/); if (isWorker) print "  "$2; next}
     isWorker && /undoCore::[A-Z]+ _program_/{n=$2; sub(/\{\}/,"",n); print "      - "n}' generated/Runtime.cpp

echo
echo "== compiling and linking the PLC against undoPLC =="
UNC="${REPO}/st2cpp_includes/undoPLC"
INC=(-Igenerated -I"${UNC}/include" -I"${UNC}/third_party/undoCore/include")
# undoLog.hpp needs Boost.Asio and Boost.Lockfree; undoPLC vendors both.
[ -d "${UNC}/third_party/boost_1_91_0" ] && INC+=(-I"${UNC}/third_party/boost_1_91_0")

LIB=""
if [ -f /usr/local/lib/libundoPLC.a ]; then
    LIB=/usr/local/lib
else
    rm -rf build_undoplc && mkdir -p build_undoplc
    for src in undoSystem undoLog undoTasks; do
        g++ -std=c++17 -c "${UNC}/src/${src}.cpp" -o "build_undoplc/${src}.o" "${INC[@]}" -pthread
    done
    ar rcs build_undoplc/libundoPLC.a build_undoplc/*.o
    LIB="$PWD/build_undoplc"
fi

# trace.cpp is this example's observer, not generated code.
g++ -std=c++17 -Wall -Wextra -Wpedantic $(find generated -name '*.cpp') trace.cpp \
    "${INC[@]}" -L"$LIB" -lundoPLC -lpthread -lrt -o plc
echo "OK: plc built"

[ "$RUN" -eq 0 ] && exit 0

echo
echo "== starting: runs until Ctrl+C =="
set +e
./plc --log2console
status=$?
set -e
if [ "$status" -eq 0 ]; then
    echo "(stopped cleanly)"
else
    echo
    echo "The PLC exited with $status. Both causes below are checked at startup and"
    echo "are fatal on purpose, so a PLC never runs with an unbounded cycle:"
    echo "  - not root: undoPLC needs SCHED_FIFO priorities and must write cpufreq to"
    echo "    pin the isolated cores to their nominal frequency. Try: sudo ./build.sh"
    echo "  - too few isolated CPUs: boot with isolcpus= (one core per Master plus"
    echo "    one per task; this example needs 2). Any 2 idle cores work."
fi
