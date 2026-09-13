# CS30401 Lab 1 — ns-3 Toy Example

Toy example for **CS30401 Network Lab 1: TCP for LEO Satellite Networks** (2026 Fall).

It measures the cost of a single packet loss on an Earth–Mars path, and evaluates a
naive fix: splitting the TCP connection at a store-and-forward relay.

> This is a **minimal** example meant to show how to drive ns-3 and how the three
> steps of the lab fit together. It is far below the depth expected of your
> submission. See the Lab 1 handout for the grading criteria.

## Files

| file | what it is |
|---|---|
| `toy-interplanetary.cc` | the simulation; drop into `scratch/` of your ns-3 tree |
| `run_toy.sh` | reproduces every number and figure in the guide |
| `plot_cwnd.py` | plots the congestion window traces |

## Requirements

- ns-3 (tested with 3.48), built with modules `point-to-point`, `internet`, `applications`
- Python 3 with `matplotlib` (only for `plot_cwnd.py`)

## Usage

```bash
# from the root of your ns-3 tree
cp /path/to/toy-interplanetary.cc scratch/
cp /path/to/run_toy.sh /path/to/plot_cwnd.py .
./ns3 build

# a single run
./ns3 run "toy-interplanetary --mode=e2e --dropPkt=40"

# everything
bash run_toy.sh
```

`run_toy.sh` finishes in well under a minute and writes CSV traces and a plot to
`toy_results/`.

## Expected output

Transfer completion time (seconds):

| dropped packet | e2e | split |
|---|---|---|
| none | 3096.2 | 1821.4 |
| 10 | 12203.7 | 6376.1 |
| 40 | 7469.6 | 4009.4 |
| 100 | 4553.4 | 2550.2 |
| 200 | 3097.0 | 1822.2 |

`plot_cwnd.py` additionally reports:

```
slow start ends at t = 1458 s (cwnd 30.0 -> 14.0 segments)
congestion-avoidance growth: 0.56 segments per RTT
```

## Options

```bash
./ns3 run "toy-interplanetary --PrintHelp"
```

| option | meaning |
|---|---|
| `--mode` | `e2e` or `split` |
| `--owdSec` | one-way Earth→Mars delay in seconds (default 182.1) |
| `--rate` | link rate of both hops (default `1Mbps`) |
| `--fileKB` | file size (default 512) |
| `--dropPkt` | index of the data packet dropped on the last hop; `-1` = none |
| `--cwndFile` | write `time,cwnd_segments` to a CSV |
| `--defaultConnSetup` | leave ns-3's connection-setup defaults alone (transfers 0 bytes) |
