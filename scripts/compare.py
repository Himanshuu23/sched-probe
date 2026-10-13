#!/usr/bin/env python3
"""Join measured and predicted CSVs, print a comparison table, save a bar chart."""
import argparse
import csv


def read(path):
    with open(path, newline="") as f:
        return {r["instruction"]: (float(r["latency_cycles"]), float(r["rthroughput_cycles"]))
                for r in csv.DictReader(f)}


def pct(meas, pred):
    return (meas - pred) / pred * 100.0 if pred else float("nan")


def save_chart(rows, path, label):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed, skipping chart")
        return
    names = [r["name"] for r in rows]
    x = list(range(len(names)))
    w = 0.4
    fig, axes = plt.subplots(2, 1, figsize=(max(8, 0.45 * len(names)), 8), sharex=True)
    for ax, key, ylabel in ((axes[0], "l", "latency (cycles)"),
                           (axes[1], "t", "reciprocal throughput (cycles)")):
        ax.bar([i - w / 2 for i in x], [r[key + "m"] for r in rows], w, label=label)
        ax.bar([i + w / 2 for i in x], [r[key + "p"] for r in rows], w, label="llvm-mca neoverse-v2")
        ax.set_yscale("log")
        ax.set_ylabel(ylabel)
        ax.legend()
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(names, rotation=75, ha="right")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    print(f"saved {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("measured")
    ap.add_argument("predicted")
    ap.add_argument("--png", default="compare.png")
    ap.add_argument("--label", default="measured", help="name of the first column (e.g. reference)")
    ap.add_argument("--threshold", type=float, default=20.0, help="flag rows differing by more than this percent")
    args = ap.parse_args()

    meas, pred = read(args.measured), read(args.predicted)
    rows = []
    for name in meas:
        if name in pred:
            (lm, tm), (lp, tp) = meas[name], pred[name]
            rows.append({"name": name, "lm": lm, "lp": lp, "ld": pct(lm, lp),
                         "tm": tm, "tp": tp, "td": pct(tm, tp)})

    a = args.label[:9]
    print(f"{'instruction':<12}{'lat ' + a:>14}{'lat pred':>10}{'diff %':>9}"
          f"{'thr ' + a:>14}{'thr pred':>10}{'diff %':>9}")
    flagged = 0
    for r in rows:
        bad = abs(r["ld"]) > args.threshold or abs(r["td"]) > args.threshold
        flagged += bad
        print(f"{r['name']:<12}{r['lm']:>14.3f}{r['lp']:>10.3f}{r['ld']:>+9.1f}"
              f"{r['tm']:>14.3f}{r['tp']:>10.3f}{r['td']:>+9.1f}{'  <-- differs' if bad else ''}")
    print(f"\n{flagged} of {len(rows)} rows differ by more than {args.threshold:.0f}%")

    for name in sorted(set(meas) ^ set(pred)):
        print(f"not in both files: {name}")

    if rows:
        save_chart(rows, args.png, args.label)


if __name__ == "__main__":
    main()
