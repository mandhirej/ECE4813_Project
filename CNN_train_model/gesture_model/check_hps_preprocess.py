"""Check that gesture_core.h's integer zone preprocessing equals the Python training
preprocessing (cv2 INTER_AREA resize + percentile stretch) on a 90x174 zone."""
import cv2
import numpy as np

from prep_gesture_dataset import ZONE_H, ZONE_W, preprocess_zone

S = 64


def c_resize(zone):            # gesture_resize_zone(), integer weights as in the C code
    rows = np.zeros((ZONE_H, S), np.int64)
    for ox in range(S):
        a, b = ox * ZONE_W, (ox + 1) * ZONE_W
        for s in range(a // 64, (b + 63) // 64):
            lo, hi = max(s * 64, a), min((s + 1) * 64, b)
            if hi > lo:
                rows[:, ox] += zone[:, s].astype(np.int64) * (hi - lo)
    out = np.zeros((S, S), np.int64)
    for oy in range(S):
        a, b = oy * ZONE_H, (oy + 1) * ZONE_H
        for s in range(a // 64, (b + 63) // 64):
            lo, hi = max(s * 64, a), min((s + 1) * 64, b)
            if hi > lo:
                out[oy] += rows[s] * (hi - lo)
    return ((out + ZONE_W * ZONE_H // 2) // (ZONE_W * ZONE_H)).astype(np.uint8)


def c_stretch(img):            # gesture_stretch()
    v = np.sort(img.flatten()).astype(int)
    lo = (10 * v[81] + 9 * (v[82] - v[81])) // 10
    hi = (10 * v[4013] + (v[4014] - v[4013])) // 10
    hi = max(hi, lo + 48)
    out = (img.astype(int) - lo) * 255
    out = np.where(out >= 0, out // (hi - lo), -((-out) // (hi - lo)))   # C int division truncates
    return np.clip(out, 0, 255).astype(np.uint8)


rng = np.random.default_rng(0)
worst_resize = worst_final = 0
for k in range(300):
    if k % 3 == 0:
        zone = rng.integers(0, 256, (ZONE_H, ZONE_W), dtype=np.uint8)
    else:   # smooth, hand-like content
        zone = cv2.GaussianBlur(rng.integers(0, 256, (ZONE_H, ZONE_W)).astype(np.float32), (0, 0), rng.uniform(1, 12))
        zone = cv2.normalize(zone, None, rng.uniform(0, 60), rng.uniform(80, 255), cv2.NORM_MINMAX).astype(np.uint8)
    ref_small = cv2.resize(zone, (S, S), interpolation=cv2.INTER_AREA)
    worst_resize = max(worst_resize, int(np.abs(ref_small.astype(int) - c_resize(zone).astype(int)).max()))
    worst_final = max(worst_final, int(np.abs(preprocess_zone(zone).astype(int) - c_stretch(c_resize(zone)).astype(int)).max()))
print(f"max |C - Python| after resize: {worst_resize}, after stretch: {worst_final} (grey levels, 300 zones)")
