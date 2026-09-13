#!/usr/bin/env python3

import argparse
import csv
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read_trace(path):
    t, w = [], []
    with open(path, newline="") as f:
        reader = csv.reader(f)
        next(reader, None)
        for row in reader:
            if len(row) < 2:
                continue
            t.append(float(row[0]))
            w.append(float(row[1]))
    if not t:
        sys.exit(f"error: no data rows in {path}")
    return t, w


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--indir", default="toy_results",
                    help="directory holding the cwnd CSV files")
    ap.add_argument("--noloss", default="cwnd_e2e_noloss.csv")
    ap.add_argument("--loss", default="cwnd_e2e_loss.csv")
    ap.add_argument("--rtt", type=float, default=364.2,
                    help="RTT in seconds, used for the secondary x-axis")
    ap.add_argument("--out", default="cwnd.png")
    args = ap.parse_args()

    p_noloss = os.path.join(args.indir, args.noloss)
    p_loss = os.path.join(args.indir, args.loss)
    for p in (p_noloss, p_loss):
        if not os.path.exists(p):
            sys.exit(f"error: {p} not found. Run run_toy.sh first.")

    t0, w0 = read_trace(p_noloss)
    t1, w1 = read_trace(p_loss)

    i_peak = max(range(len(w1)), key=lambda i: w1[i])
    i_trough = min(range(i_peak, len(w1)), key=lambda i: w1[i])
    t_peak, t_trough = t1[i_peak], t1[i_trough]

    fig, ax = plt.subplots(figsize=(8, 4.5))

    ax.plot(t0, w0, drawstyle="steps-post", label="no loss", linewidth=1.8)
    ax.plot(t1, w1, drawstyle="steps-post", label="one packet dropped",
            linewidth=1.8)

    ymax = max(max(w0), max(w1))
    ax.axvline(t_peak, linestyle="--", linewidth=1.0, color="0.4")
    ax.annotate(f"slow start ends\nt = {t_peak:.0f} s",
                xy=(t_peak, ymax * 0.88), xytext=(8, 0),
                textcoords="offset points", fontsize=9, color="0.25")
    ax.annotate("congestion avoidance:\nlinear growth, one RTT per step",
                xy=(t_trough, w1[i_trough]),
                xytext=(t_trough + 0.10 * t1[-1], ymax * 0.30),
                arrowprops=dict(arrowstyle="->", color="0.4", linewidth=1.0),
                fontsize=9, color="0.25")

    ax.set_xlabel("simulated time (s)")
    ax.set_ylabel("congestion window (segments)")
    ax.set_title("TCP NewReno over an Earth-Mars path (RTT = "
                 f"{args.rtt:.0f} s)")
    ax.legend()
    ax.grid(alpha=0.3)
    ax.set_xlim(left=0)
    ax.set_ylim(bottom=0)

    secax = ax.secondary_xaxis("top", functions=(lambda s: s / args.rtt,
                                                 lambda r: r * args.rtt))
    secax.set_xlabel("round trips")

    fig.tight_layout()
    fig.savefig(args.out, dpi=150)
    print(f"wrote {args.out}")

    span_t = t1[-1] - t_trough
    span_w = w1[-1] - w1[i_trough]
    if span_t > 0:
        print(f"slow start ends at t = {t_peak:.0f} s "
              f"(cwnd {w1[i_peak]:.1f} -> {w1[i_trough]:.1f} segments)")
        print(f"congestion-avoidance growth: "
              f"{span_w / (span_t / args.rtt):.2f} segments per RTT")


if __name__ == "__main__":
    main()