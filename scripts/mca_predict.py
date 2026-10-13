#!/usr/bin/env python3
"""Run llvm-mca (neoverse-v2) on snippets matching the kernels in src/kernels.hpp and write a CSV."""
import argparse
import csv
import re
import subprocess
import sys
from pathlib import Path

UNROLL = 32  # same as kUnroll in kernels.hpp
ITERATIONS = 100
ROOT = Path(__file__).resolve().parent.parent
ENTRY = re.compile(r'X\((\w+),\s*(\d),\s*\w+,\s*"([^"]*)",\s*"([^"]*)"\)')


def load_table(header):
    rows = []
    for line in header.read_text().splitlines():
        m = ENTRY.search(line)
        if m:
            name, sve, lat, thr = m.groups()
            # \\i is the gas macro argument, \\() just separates it from what follows
            thr = thr.replace("\\\\i\\\\()", "{i}").replace("\\\\i", "{i}")
            rows.append((name, sve == "1", lat, thr))
    return rows


def cycles_per_instr(mca, text, extra):
    cmd = [mca, "-mtriple=aarch64", "-mcpu=neoverse-v2", f"-iterations={ITERATIONS}"] + extra
    p = subprocess.run(cmd, input=text, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(p.stderr.strip().splitlines()[-1] if p.stderr.strip() else "llvm-mca failed")
    instrs = re.search(r"^Instructions:\s+(\d+)", p.stdout, re.M)
    cycles = re.search(r"^Total Cycles:\s+(\d+)", p.stdout, re.M)
    if not instrs or not cycles:
        raise RuntimeError("could not parse llvm-mca output")
    return int(cycles.group(1)) / int(instrs.group(1))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mca", default="llvm-mca", help="llvm-mca binary (e.g. llvm-mca-18)")
    ap.add_argument("--header", default=str(ROOT / "src" / "kernels.hpp"))
    ap.add_argument("--out", default="build/predicted.csv")
    ap.add_argument("--snippets", default="build/mca", help="directory for generated .s files")
    args = ap.parse_args()

    table = load_table(Path(args.header))
    if not table:
        sys.exit(f"no kernels found in {args.header}")
    snip_dir = Path(args.snippets)
    snip_dir.mkdir(parents=True, exist_ok=True)
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)

    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["instruction", "latency_cycles", "rthroughput_cycles"])
        for name, sve, lat, thr in table:
            lat_text = "\n".join([lat] * UNROLL) + "\n"
            thr_text = "\n".join(thr.format(i=k % 8) for k in range(UNROLL)) + "\n"
            (snip_dir / f"{name}_lat.s").write_text(lat_text)
            (snip_dir / f"{name}_thr.s").write_text(thr_text)
            extra = ["-mattr=+sve2"] if sve else []
            try:
                lat_c = cycles_per_instr(args.mca, lat_text, extra)
                thr_c = cycles_per_instr(args.mca, thr_text, extra)
            except FileNotFoundError:
                sys.exit(f"{args.mca} not found (needs LLVM 17 or newer; try --mca llvm-mca-18)")
            except RuntimeError as e:
                print(f"warning: {name}: {e}", file=sys.stderr)
                continue
            w.writerow([name, f"{lat_c:.3f}", f"{thr_c:.3f}"])
            print(f"{name:<10} latency {lat_c:7.3f}  rthroughput {thr_c:7.3f}")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
