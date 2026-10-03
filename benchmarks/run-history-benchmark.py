#!/usr/bin/env python3
"""Run each synthetic history case in a fresh process and retain raw samples."""

import argparse
import json
import math
import os
from pathlib import Path
import platform
import re
import signal
import statistics
import subprocess


def distribution(values):
    values = sorted(values)
    return {"median": statistics.median(values),
            "p95": values[math.ceil(len(values) * 0.95) - 1],
            "min": values[0], "max": values[-1]}


def run_case(binary, count, profile, stage, timeout):
    command = [str(binary), "--entries", str(count), "--profile", profile,
               "--stage", stage]
    system = platform.system()
    if system == "Darwin":
        command = ["/usr/bin/time", "-l"] + command
    elif system == "Linux" and Path("/usr/bin/time").exists():
        command = ["/usr/bin/time", "-f", "PASTES_PEAK_RSS_KIB=%M"] + command
    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    process = subprocess.Popen(command, env=environment, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True,
                               start_new_session=(os.name == "posix"))
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        process.communicate()
        raise RuntimeError(f"Benchmark exceeded {timeout} seconds") from None
    if process.returncode:
        raise RuntimeError(stderr or f"Benchmark exited {process.returncode}")
    result = json.loads(stdout)
    result["peak_rss_kib"] = None
    if system == "Darwin":
        match = re.search(r"^\s*(\d+)\s+maximum resident set size\s*$",
                          stderr, re.MULTILINE)
        if match:
            result["peak_rss_kib"] = int(match.group(1)) / 1024
    elif system == "Linux":
        match = re.search(r"^PASTES_PEAK_RSS_KIB=(\d+)$",
                          stderr, re.MULTILINE)
        if match:
            result["peak_rss_kib"] = int(match.group(1))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--counts", type=int, nargs="+", default=[100, 1000, 10000])
    parser.add_argument("--profiles", nargs="+", choices=["text", "mixed"], default=["text", "mixed"])
    parser.add_argument("--stages", nargs="+", choices=["database", "view"], default=["database", "view"])
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--label", default="local")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.repeats < 1 or args.timeout < 1 or any(count < 1 or count > 10000 for count in args.counts):
        parser.error("Use positive repeats and entry counts between 1 and 10000")
    binary = args.binary.resolve(strict=True)
    if args.output.exists():
        parser.error("Output already exists; choose a new report path")
    report = {"label": args.label, "system": platform.platform(),
              "machine": platform.machine(), "binary": str(binary),
              "peak_rss_scope": "entire child process, including fixture creation",
              "samples": [], "summary": []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for count in args.counts:
        for profile in args.profiles:
            for stage in args.stages:
                samples = []
                for _ in range(args.repeats):
                    sample = run_case(binary, count, profile, stage, args.timeout)
                    samples.append(sample)
                    report["samples"].append(sample)
                    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                                           encoding="utf-8")
                summary = {"entries": count, "profile": profile, "stage": stage}
                for key in ("first_open_load_ms", "reopen_load_ms", "card_bind_ms",
                            "offscreen_render_ms", "peak_rss_kib"):
                    values = [sample[key] for sample in samples if sample.get(key) is not None]
                    if values:
                        summary[key] = distribution(values)
                if stage == "view":
                    summary["search_ms"] = {}
                    for query in sorted({item["query"] for sample in samples
                                         for item in sample["search_samples"]}):
                        summary["search_ms"][query] = distribution(
                            [item["elapsed_ms"] for sample in samples
                             for item in sample["search_samples"] if item["query"] == query])
                report["summary"].append(summary)
                args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                                       encoding="utf-8")
                timing = summary["reopen_load_ms"]["median"]
                cards = summary.get("card_bind_ms", {}).get("median")
                suffix = f", cards {cards:.1f} ms" if cards is not None else ""
                print(f"{count:5d} {profile:5s} {stage:8s}: reload {timing:.1f} ms{suffix}", flush=True)
    print(f"Report: {args.output}", flush=True)


if __name__ == "__main__":
    main()
