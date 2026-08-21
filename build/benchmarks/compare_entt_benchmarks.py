#!/usr/bin/env python3
"""
Differential Benchmark Analyzer for 0 A.D. EnTT Modernization
Compares Google Benchmark JSON output between Legacy OOP and EnTT ECS implementations.
"""

import sys
import json
import os
from collections import defaultdict

def load_benchmark_json(file_path):
    with open(file_path, 'r', encoding='utf-8') as f:
        return json.load(f)

def parse_benchmarks(data):
    benchmarks = {}
    for bm in data.get("benchmarks", []):
        name = bm.get("name", "")
        cpu_time = bm.get("cpu_time", 0.0)
        real_time = bm.get("real_time", 0.0)
        items_per_sec = bm.get("items_per_second", 0.0)
        time_unit = bm.get("time_unit", "ns")
        benchmarks[name] = {
            "cpu_time": cpu_time,
            "real_time": real_time,
            "items_per_sec": items_per_sec,
            "time_unit": time_unit
        }
    return benchmarks

def compare_paired_benchmarks(benchmarks):
    pairs = [
        ("BM_ComponentManager_BroadcastMessage_Dense", "BM_ComponentManager_BroadcastMessage_EnTT"),
        ("BM_ComponentManager_MultiReceiverBroadcast", "BM_ComponentManager_MultiReceiverBroadcast_EnTT"),
        ("BM_ComponentManager_BatchEntityDestruction", "BM_ComponentManager_BatchEntityDestruction_EnTT"),
        ("BM_ComponentManager_ComponentCacheLookup", "BM_ComponentManager_ComponentCacheLookup_EnTT"),
        ("BM_RangeManager_DistanceOrdering", "BM_RangeManager_DistanceOrdering_EnTT"),
        ("BM_UnitMotion_StepMove", "BM_UnitMotion_StepMove_EnTT"),
        ("BM_UnitMotion_PostMove", "BM_UnitMotion_PostMove_EnTT"),
    ]

    print("=" * 96)
    print(f"{'Benchmark Target':<50} | {'Legacy':<10} | {'EnTT':<10} | {'Speedup':<8} | {'Delta'}")
    print("=" * 96)

    # Find all parameter sweeps
    all_names = list(benchmarks.keys())
    for legacy_base, entt_base in pairs:
        legacy_matches = sorted([k for k in all_names if k.startswith(legacy_base)])
        for leg_name in legacy_matches:
            suffix = leg_name[len(legacy_base):]
            entt_name = entt_base + suffix
            if entt_name in benchmarks:
                leg_data = benchmarks[leg_name]
                entt_data = benchmarks[entt_name]

                leg_time = leg_data["cpu_time"]
                entt_time = entt_data["cpu_time"]
                unit = leg_data["time_unit"]

                if entt_time > 0:
                    speedup = leg_time / entt_time
                else:
                    speedup = 1.0

                pct_delta = ((entt_time - leg_time) / leg_time) * 100.0 if leg_time > 0 else 0.0

                label = f"{legacy_base}{suffix}"
                print(f"{label:<50} | {leg_time:>8.1f} {unit} | {entt_time:>8.1f} {unit} | {speedup:>6.2f}x | {pct_delta:>+6.1f}%")

    print("=" * 96)

def main():
    if len(sys.argv) < 2:
        print("Usage: compare_entt_benchmarks.py <benchmark_output.json>")
        sys.exit(1)

    json_path = sys.argv[1]
    if not os.path.exists(json_path):
        print(f"Error: file not found: {json_path}")
        sys.exit(1)

    data = load_benchmark_json(json_path)
    bms = parse_benchmarks(data)
    compare_paired_benchmarks(bms)

if __name__ == "__main__":
    main()
