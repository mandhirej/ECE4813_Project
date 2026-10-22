"""Per-photo gesture read rates on the simulated board (same compositing, HPS
preprocessing and integer CNN as the game simulation)."""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
GW = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, GW)
sys.path.insert(0, os.path.dirname(HERE))
from prep_gesture_dataset import preprocess_zone  # noqa: E402
from train_gesture_head import HW, int_features, load_hw_weights  # noqa: E402
from make_vectors import fc_from_rom, read_mif  # noqa: E402

rng = np.random.default_rng(0)
data = open(os.path.join(HERE, "build", "assets.bin"), "rb").read()
nc, nh = struct.unpack_from("<II", data, 0)
off, hands = 8, []
for i in range(nc + nh):
    gid, w, h = struct.unpack_from("<iii", data, off); off += 12
    ch = 3 if i < nc else 4
    im = np.frombuffer(data, np.uint8, w * h * ch, off).reshape(h, w, ch); off += w * h * ch
    if i >= nc:
        hands.append((gid, im))

p, w1, b1, w2, b2 = load_hw_weights()
wfc, bfc = fc_from_rom(read_mif(os.path.join(HW, "weights_linear_gesture.mif")), os.path.join(HW, "linear_bias.vh"))
names = ["fist", "palm", "peace", "nothing"]
print("photo        read as:  fist  palm peace nothing")
for k, (gid, im) in enumerate(hands):
    zones = []
    for _ in range(60):
        y, x = np.mgrid[0:178, 0:94]
        board = 30 + 7 * np.sin(x / 41.0) + 5 * np.cos(y / 23.0) + rng.normal(0, 2.5, (178, 94))
        jx, jy = rng.integers(0, 5, 2)
        a = im[..., 3:4] / 255.0
        g = 0.95 + 0.1 * rng.random()
        rgb = np.repeat(board[..., None], 3, 2)
        rgb[jy:jy + 174, jx:jx + 90] = a * im[..., :3] * g + (1 - a) * rgb[jy:jy + 174, jx:jx + 90]
        z = rgb[2:176, 2:92].clip(0, 255).astype(np.uint32)
        gray = ((77 * z[..., 0] + 150 * z[..., 1] + 29 * z[..., 2] + 128) >> 8).astype(np.uint8)
        zones.append(preprocess_zone(gray))
    q2 = int_features(np.stack(zones), p, w1, b1, w2, b2)
    pred = (q2 @ wfc[54:58].T + bfc[54:58]).argmax(1)
    cnt = np.bincount(pred, minlength=4)
    print(f"{names[gid]:5} #{k % 6}            " + " ".join(f"{c:5d}" for c in cnt) + f"   ({100 * cnt[gid] / 60:.0f}% correct)")
