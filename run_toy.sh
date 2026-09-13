#!/usr/bin/env bash
set -e

NS3="./ns3 run --no-build toy-interplanetary --"
OUT=toy_results
mkdir -p $OUT

echo "=== Table 1: transfer completion time [s] ==========================="
printf "%-10s %-12s %-12s\n" "drop" "e2e" "split"
for d in -1 10 40 100 200; do
    E=$($NS3 --mode=e2e   --dropPkt=$d | awk '/completion_time/{print $3}')
    S=$($NS3 --mode=split --dropPkt=$d | awk '/completion_time/{print $3}')
    printf "%-10s %-12s %-12s\n" "$d" "$E" "$S"
done

echo
echo "=== Figure 1: cwnd traces (e2e, with and without the loss) ========="
$NS3 --mode=e2e --dropPkt=-1 --cwndFile=$OUT/cwnd_e2e_noloss.csv > /dev/null
$NS3 --mode=e2e --dropPkt=40 --cwndFile=$OUT/cwnd_e2e_loss.csv   > /dev/null
echo "wrote $OUT/cwnd_e2e_noloss.csv and $OUT/cwnd_e2e_loss.csv"

echo
echo "=== Validation: does the setup fill the link when it should? ========"
for o in 0.02 0.1; do
    printf "  owd=%-5s " $o
    $NS3 --owdSec=$o --dropPkt=-1 --fileKB=4096 | grep completion_time
done

echo
echo "=== Validation: default ConnTimeout is too small for this path ====="
$NS3 --mode=e2e --dropPkt=-1 --defaultConnSetup=1 | grep -E "bytes_received|completion_time"

echo
echo "=== Figure: plot the cwnd traces ============="
if python3 -c "import matplotlib" 2>/dev/null; then
    python3 plot_cwnd.py --out $OUT/cwnd.png
else
    echo "matplotlib not installed; skipping. (pip install --user matplotlib)"
fi