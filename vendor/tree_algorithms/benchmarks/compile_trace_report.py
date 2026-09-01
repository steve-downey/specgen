#!/usr/bin/env python3
# benchmarks/compile_trace_report.py
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Summarize compile-time benchmark timings from the CMake build telemetry.

The build telemetry (configured by infra/cmake/BuildTelemetryConfig.cmake)
writes Chrome-trace JSON to <build-dir>/.trace after every `cmake --build`,
with one `compile` event per translation unit carrying its wall-clock
duration. This script reads those events back, isolates the compile-time
benchmark TUs (ct_fix_<N> and ct_plain_<N>, produced by the benchmarks
CMakeLists), and prints a fix-vs-plain-vs-size table.

The story it is meant to tell: ct_fix should track ct_plain closely at each
size, and both should grow roughly linearly with N — i.e. the Fix operator
adds no super-linear compile-time cost of its own; what you pay is the
constexpr evaluation any tree of that size would cost.

Usage:
    compile_trace_report.py --build-dir .build/build-system [--config RelWithDebInfo]
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

# Matches the generated compile-time benchmark sources: ct_<kind>_<N>.cpp
_CT_RE = re.compile(r"ct_(fix|plain)_(\d+)\.cpp$")


def _iter_compile_events(trace_dir: str):
    """Yield (kind, size, duration_ms, obj_bytes, start) for every ct_* compile
    event found across all postCMakeBuild trace files."""
    pattern = os.path.join(trace_dir, "postCMakeBuild-*.json")
    for path in glob.glob(pattern):
        try:
            with open(path) as fh:
                events = json.load(fh)
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(events, list):
            continue
        for event in events:
            if event.get("cat") != "compile":
                continue
            args = event.get("args", {})
            source = args.get("source", "")
            match = _CT_RE.search(os.path.basename(source))
            if not match:
                continue
            kind = match.group(1)
            size = int(match.group(2))
            duration = args.get("duration")  # milliseconds
            if duration is None:
                continue
            sizes = args.get("outputSizes") or [0]
            obj_bytes = sizes[0] if sizes else 0
            start = args.get("timeStart", event.get("ts", 0))
            yield kind, size, float(duration), int(obj_bytes), start


def _latest_by_key(events):
    """Keep only the most recent event per (kind, size)."""
    latest: dict[tuple[str, int], tuple] = {}
    for kind, size, duration, obj_bytes, start in events:
        key = (kind, size)
        if key not in latest or start > latest[key][-1]:
            latest[key] = (duration, obj_bytes, start)
    return latest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", required=True, help="Build directory, e.g. .build/build-system")
    parser.add_argument("--config", default=None, help="Build config (informational; timings are config-agnostic)")
    opts = parser.parse_args()

    trace_dir = os.path.join(opts.build_dir, ".trace")
    if not os.path.isdir(trace_dir):
        print(f"error: no trace directory at {trace_dir}", file=sys.stderr)
        print("       build the compile-time benchmarks first (make bench-compile).", file=sys.stderr)
        return 1

    latest = _latest_by_key(_iter_compile_events(trace_dir))
    if not latest:
        print(f"error: no ct_fix_* / ct_plain_* compile events in {trace_dir}", file=sys.stderr)
        print("       run: make bench-compile", file=sys.stderr)
        return 1

    sizes = sorted({size for (_kind, size) in latest})

    cfg = f" (config {opts.config})" if opts.config else ""
    print(f"Compile-time benchmark: Fix tree vs hand-written control{cfg}")
    print(f"source: {trace_dir}")
    print()
    header = f"{'nodes':>8}  {'fix ms':>10}  {'plain ms':>10}  {'fix/plain':>10}  {'fix obj B':>12}  {'plain obj B':>12}"
    print(header)
    print("-" * len(header))

    prev = None
    for size in sizes:
        fix = latest.get(("fix", size))
        plain = latest.get(("plain", size))
        fix_ms = fix[0] if fix else float("nan")
        plain_ms = plain[0] if plain else float("nan")
        ratio = (fix_ms / plain_ms) if (plain and plain[0]) else float("nan")
        fix_obj = fix[1] if fix else 0
        plain_obj = plain[1] if plain else 0
        print(
            f"{size:>8}  {fix_ms:>10.1f}  {plain_ms:>10.1f}  {ratio:>10.2f}  {fix_obj:>12}  {plain_obj:>12}"
        )
        prev = size

    # Scaling: compare the largest to the smallest size for each kind.
    if len(sizes) >= 2:
        lo, hi = sizes[0], sizes[-1]
        print()
        print(f"Scaling from {lo} to {hi} nodes ({hi / lo:.0f}x the tree):")
        for kind in ("fix", "plain"):
            a = latest.get((kind, lo))
            b = latest.get((kind, hi))
            if a and b and a[0]:
                print(f"  {kind:<5}: {b[0] / a[0]:>6.1f}x compile time  (linear would be {hi / lo:.0f}x)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
