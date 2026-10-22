#!/bin/sh
# Bit-exact check: Python integer model vs iverilog simulation of CNN.v.
#   1. new RTL (gesture mode) + new ROM      vs Python expectations, every vector
#   2. original RTL (CNN.v.before_gesture) + original ROM  vs new RTL, card-mode vectors
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
HW="$HERE/../../DE10_Nano_D8M_DDR3_CNN/DE10_Nano_D8M_DDR3_CNN"
B="$HERE/build"

python "$HERE/make_vectors.py"
NUM=$(wc -l < "$B/expected.txt")

run() {  # $1 = RTL file, $2 = ROM hex, $3 = run name (own folder, so both can run at once)
    D="$B/$3"
    mkdir -p "$D"
    cp "$HW"/weights_mac*.hex "$B/images.hex" "$B/modes.hex" "$D"/
    cp "$B/$2" "$D/rom.hex"
    iverilog -g2012 -o "$D/sim.vvp" -I "$HW" "$HERE/tb_cnn_gesture.v" "$1"
    (cd "$D" && vvp -n sim.vvp +NUM="$NUM" > sim.log && cp rtl_out.txt "$B/$3.txt")
}
rm -f "$B/rtl_new.txt" "$B/rtl_orig.txt"
run "$HW/CNN.v" rom_new.hex rtl_new &
run "$HW/CNN.v.before_gesture" rom_orig.hex rtl_orig &
wait

python - "$B" <<'EOF'
import sys, os
b = sys.argv[1]
exp = [l.split() for l in open(os.path.join(b, "expected.txt"))]
new = [l.split() for l in open(os.path.join(b, "rtl_new.txt"))]
orig = [l.split() for l in open(os.path.join(b, "rtl_orig.txt"))]
assert len(new) == len(exp) == len(orig), "simulation did not finish every vector"
bad = 0
print(f"{'vector':40} mode python  rtl   orig-rtl  cycles")
for e, n, o in zip(exp, new, orig):
    name, mode, pred = e[0], int(e[1]), int(e[2])
    rtl, cyc, orig_rtl = int(n[1]), int(n[2]), int(o[1])
    ok = rtl == pred and (mode == 1 or orig_rtl == rtl)
    bad += not ok
    print(f"{name[:40]:40} {mode:>4} {pred:>6} {rtl:>5} {orig_rtl:>9} {cyc:>8} {'OK' if ok else 'MISMATCH'}")
print(f"\n{len(exp) - bad}/{len(exp)} match. Card-mode vectors also compared with the unmodified RTL.")
sys.exit(1 if bad else 0)
EOF
