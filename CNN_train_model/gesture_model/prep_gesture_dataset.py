"""Crop, augment and split the hand-gesture photos into 64x64 grayscale CNN inputs.

Input : gesture_raw/<root>/{Fist_dataset,Open_palm_dataset,Peace_dataset,Nothing}/*.jpg
Output: gesture_dataset_64x64/{train,val,test}/{fist,palm,peace,nothing}/*.png
        gesture_dataset_64x64/crop_preview.png  (one sheet per split for eyeballing)

Gesture zone on the board: the 3 unused cells above DRAW, camera pixels x 370..459,
y 18..191 (90 x 174). The hand enters from the DRAW side, so in the zone the wrist is at
the bottom and the fingers point up. Training crops use the same 90:174 window, with the
hand scaled by the forearm width so every gesture keeps its real size relative to the zone.

Preprocessing that the HPS must reproduce on the gesture zone (see preprocess_zone):
  1. gray = (77R + 150G + 29B + 128) >> 8     (same as vision_core.h)
  2. resize the 90x174 zone to 64x64 (area average, aspect squashed)
  3. contrast stretch: lo = 2nd percentile, hi = 98th percentile,
     hi forced to at least lo + MIN_SPAN, out = clamp((x - lo) * 255 / (hi - lo))

Split is done on ORIGINAL photos (never on augmented copies), in time order inside each
capture session, so near-duplicate burst frames cannot leak from train into val/test.
"""
import glob
import os
import random
import shutil
import sys

import cv2
import numpy as np

SEED = 42
IMG = 64
ZONE_W, ZONE_H = 90, 174   # gesture zone in camera pixels
WIN_W, WIN_H = 2 * ZONE_W, 2 * ZONE_H   # working resolution of a crop before the 64x64 resize
CTX = 1.4             # context kept around the window so rotations/shifts do not expose borders
ZONE_PER_FOREARM = 1.6     # zone width / forearm width (90 mm zone, ~55-60 mm forearm)
MIN_SPAN = 48         # caps the contrast-stretch gain at 255/48 ~ 5.3x
TRAIN_AUG = 40        # augmented copies per training photo
NOTHING_EXTRA = 2     # nothing photos are few, so they get this many times more copies
SYNTH_NOTHING = 300   # synthetic empty-board crops added to train

CLASSES = {           # folder in the zip -> class name (order = gesture class index)
    "Fist_dataset": "fist",
    "Open_palm_dataset": "palm",
    "Peace_dataset": "peace",
    "Nothing": "nothing",
}

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
RAW_ROOT = glob.glob(os.path.join(PROJECT, "gesture_raw", "*"))[0]
OUT = os.path.join(PROJECT, "gesture_dataset_64x64")

rng = np.random.default_rng(SEED)
random.seed(SEED)


# ---------------------------------------------------------------- preprocessing
def hps_gray(bgr):
    b = bgr[:, :, 0].astype(np.uint32)
    g = bgr[:, :, 1].astype(np.uint32)
    r = bgr[:, :, 2].astype(np.uint32)
    return ((77 * r + 150 * g + 29 * b + 128) >> 8).astype(np.uint8)


def stretch(gray64):
    lo = int(np.percentile(gray64, 2))
    hi = int(np.percentile(gray64, 98))
    hi = max(hi, lo + MIN_SPAN)
    out = (gray64.astype(np.int32) - lo) * 255 // (hi - lo)
    return np.clip(out, 0, 255).astype(np.uint8)


def preprocess_zone(gray_zone):
    small = cv2.resize(gray_zone, (IMG, IMG), interpolation=cv2.INTER_AREA)
    return stretch(small)


# ---------------------------------------------------------------- loading/cropping
def load(path):
    data = np.fromfile(path, dtype=np.uint8)          # handles odd paths on Windows
    img = cv2.imdecode(data, cv2.IMREAD_COLOR)
    # cv2.imdecode ignores EXIF rotation; apply it so hands point the same way as in the photos
    try:
        from PIL import Image, ImageOps
        pil = ImageOps.exif_transpose(Image.open(path))
        img = cv2.cvtColor(np.array(pil.convert("RGB")), cv2.COLOR_RGB2BGR)
    except Exception:
        pass
    h, w = img.shape[:2]
    scale = 800.0 / max(h, w)
    return cv2.resize(img, (int(w * scale), int(h * scale)), interpolation=cv2.INTER_AREA)


def skin_mask(bgr):
    ycc = cv2.cvtColor(bgr, cv2.COLOR_BGR2YCrCb)
    cr, cb = ycc[:, :, 1].astype(int), ycc[:, :, 2].astype(int)
    m = ((cr > 138) & (cr < 180) & (cb > 77) & (cb < 130)).astype(np.uint8) * 255
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((5, 5), np.uint8))
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((15, 15), np.uint8))
    n, lab, stats, _ = cv2.connectedComponentsWithStats(m)
    if n <= 1:
        return None
    big = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    return (lab == big).astype(np.uint8) * 255


def hand_window(mask):
    """Zone-shaped window (photo orientation: arm enters from the top, fingers point down).

    Its width is ZONE_PER_FOREARM x the forearm width, so the hand has the same size
    relative to the window whatever the gesture and the photo distance."""
    ys, xs = np.where(mask > 0)
    y0, y1 = ys.min(), ys.max()
    arm_rows = [np.count_nonzero(mask[y]) for y in range(y0 + 3, y0 + max(8, int(0.12 * (y1 - y0))))]
    forearm = float(np.median(arm_rows))
    ww = ZONE_PER_FOREARM * forearm
    wh = ww * ZONE_H / ZONE_W
    lower = ys > y0 + 0.55 * (y1 - y0)
    cx = 0.5 * (xs[lower].min() + xs[lower].max())
    bottom = y1 + 0.06 * wh            # fingertips just inside the far edge of the zone
    return cx - ww / 2, bottom - wh, ww, wh


def base_crop(path, cls):
    """Grayscale context window (rotated so the wrist is at the bottom) and its skin mask."""
    bgr = load(path)
    h, w = bgr.shape[:2]
    if cls == "nothing":
        wh = 0.9 * h
        ww = wh * ZONE_W / ZONE_H
        x0, y0 = rng.uniform(0, w - ww), 0.05 * h
        mask = np.zeros((h, w), np.uint8)
    else:
        mask = skin_mask(bgr)
        if mask is None:
            print("  no hand found, skipped:", os.path.basename(path))
            return None
        x0, y0, ww, wh = hand_window(mask)
    # sample a CTX-times larger window around the zone window, at WIN resolution
    cw, ch = int(WIN_W * CTX), int(WIN_H * CTX)
    sx, sy = ww / WIN_W, wh / WIN_H
    ox = x0 + ww / 2 - sx * cw / 2
    oy = y0 + wh / 2 - sy * ch / 2
    M = np.float32([[sx, 0, ox], [0, sy, oy]])
    flags = cv2.INTER_AREA | cv2.WARP_INVERSE_MAP
    gray = cv2.warpAffine(hps_gray(bgr), M, (cw, ch), flags=flags, borderMode=cv2.BORDER_REFLECT)
    m = cv2.warpAffine(mask, M, (cw, ch), flags=cv2.INTER_NEAREST | cv2.WARP_INVERSE_MAP)
    # photo has the arm at the top; on the board it comes from DRAW, i.e. the bottom
    return gray[::-1, ::-1].copy(), m[::-1, ::-1].copy()


def center(gray_ctx):
    oy = (gray_ctx.shape[0] - WIN_H) // 2
    ox = (gray_ctx.shape[1] - WIN_W) // 2
    return gray_ctx[oy:oy + WIN_H, ox:ox + WIN_W]


# ---------------------------------------------------------------- augmentation
def augment(gray_ctx, mask_ctx, cls):
    ch, cw = gray_ctx.shape
    g = gray_ctx.astype(np.float32)
    # Arm from the bottom only. Training both directions ("both50" in
    # orientation_experiment.py) cost too much upright accuracy in the game simulation
    # (peace read 59% palm). A hand from the top is turned 180 deg on the HPS instead
    # (GESTURE_FROM_TOP in gesture_core.h).
    # Black-board look: darken everything that is not skin (the photos were on grey card)
    if cls != "nothing" and rng.random() < 0.7:
        soft = cv2.GaussianBlur(mask_ctx.astype(np.float32) / 255.0, (9, 9), 0)
        g = g * (soft + (1 - soft) * rng.uniform(0.15, 0.8))
    # geometry: tilt up to +-35 deg, size, position, mirror (left vs right hand).
    # +-50 deg was tried: tilted hands improved but clear upright PALM/PEACE poses got
    # worse (gesture_variant_test.py), so +-35 deg is kept.
    ang = rng.uniform(-35, 35)
    sc = rng.uniform(0.85, 1.2)
    M = cv2.getRotationMatrix2D((cw / 2, ch / 2), ang, sc)
    M[0, 2] += rng.uniform(-0.12, 0.12) * WIN_W
    M[1, 2] += rng.uniform(-0.06, 0.12) * WIN_H
    g = cv2.warpAffine(g, M, (cw, ch), borderMode=cv2.BORDER_REFLECT)
    if rng.random() < 0.5:
        g = g[:, ::-1]
    g = center(g)
    # photometric: gamma, gain, offset, blur, noise
    g = 255.0 * (np.clip(g, 0, 255) / 255.0) ** rng.uniform(0.7, 1.4)
    g = g * rng.uniform(0.7, 1.2) + rng.uniform(-20, 20)
    if rng.random() < 0.4:
        g = cv2.GaussianBlur(g, (0, 0), rng.uniform(0.5, 2.0))
    g = g + rng.normal(0, rng.uniform(0, 6), g.shape)
    return preprocess_zone(np.clip(g, 0, 255).astype(np.uint8))


def synthetic_board():
    """Empty dark board: smooth gradient + texture + noise."""
    yy, xx = np.mgrid[0:WIN_H, 0:WIN_W] / np.float32(WIN_H)
    base = rng.uniform(5, 70)
    grad = rng.uniform(-30, 30) * xx + rng.uniform(-30, 30) * yy
    tex = cv2.GaussianBlur(rng.normal(0, 1, (WIN_H, WIN_W)).astype(np.float32), (0, 0), rng.uniform(2, 8))
    g = base + grad + tex * rng.uniform(2, 10) + rng.normal(0, rng.uniform(1, 5), (WIN_H, WIN_W))
    return preprocess_zone(np.clip(g, 0, 255).astype(np.uint8))


# ---------------------------------------------------------------- split
def session_key(path):
    # IMG_20261021_181125.jpg -> minute 1811; >2 min gap starts a new session
    return int(os.path.basename(path).split("_")[2][:4])


def split_files(files, cls):
    files = sorted(files)
    groups, cur, last = [], [], None
    for f in files:
        k = session_key(f)
        if last is not None and k - last > 2:
            groups.append(cur)
            cur = []
        cur.append(f)
        last = k
    groups.append(cur)
    if cls == "nothing":   # one session: split the grey-card and dark-board halves separately
        bright = [f for f in files if hps_gray(load(f)).mean() > 60]
        groups = [bright, [f for f in files if f not in bright]]
    out = {"train": [], "val": [], "test": []}
    for grp in groups:
        n = len(grp)
        n_test = max(1, round(0.15 * n))
        n_val = max(1, round(0.15 * n))
        out["train"] += grp[: n - n_val - n_test]
        out["val"] += grp[n - n_val - n_test: n - n_test]
        out["test"] += grp[n - n_test:]
    return out


def save(img, split, cls, name):
    d = os.path.join(OUT, split, cls)
    os.makedirs(d, exist_ok=True)
    cv2.imwrite(os.path.join(d, name + ".png"), img)


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    counts = {}
    previews = {"train": [], "val": [], "test": []}
    for folder, cls in CLASSES.items():
        src = [d for d in glob.glob(os.path.join(RAW_ROOT, "*")) if os.path.basename(d).strip() == folder]
        files = glob.glob(os.path.join(src[0], "*.jpg"))
        parts = split_files(files, cls)
        print(f"{cls}: " + ", ".join(f"{k}={len(v)} photos" for k, v in parts.items()))
        for split, fl in parts.items():
            for f in fl:
                bc = base_crop(f, cls)
                if bc is None:
                    continue
                gray_ctx, mask_ctx = bc
                stem = os.path.splitext(os.path.basename(f))[0]
                clean = preprocess_zone(center(gray_ctx))
                save(clean, split, cls, stem)
                previews[split].append(clean)
                counts[(split, cls)] = counts.get((split, cls), 0) + 1
                if split == "train":
                    reps = TRAIN_AUG * (NOTHING_EXTRA if cls == "nothing" else 1)
                    for k in range(reps):
                        save(augment(gray_ctx, mask_ctx, cls), split, cls, f"{stem}_aug{k:02d}")
                        counts[(split, cls)] += 1
    for k in range(SYNTH_NOTHING):
        save(synthetic_board(), "train", "nothing", f"synthetic_{k:03d}")
        counts[("train", "nothing")] += 1

    for split, ims in previews.items():
        cols = 16
        rows = (len(ims) + cols - 1) // cols
        sheet = np.full((rows * (IMG + 2), cols * (IMG + 2)), 255, np.uint8)
        for i, im in enumerate(ims):
            r, c = divmod(i, cols)
            sheet[r * (IMG + 2): r * (IMG + 2) + IMG, c * (IMG + 2): c * (IMG + 2) + IMG] = im
        cv2.imwrite(os.path.join(OUT, f"preview_{split}.png"), cv2.resize(sheet, None, fx=2, fy=2, interpolation=cv2.INTER_NEAREST))
    print("\nimages written:")
    for split in ("train", "val", "test"):
        print(" ", split, {c: counts.get((split, c), 0) for c in CLASSES.values()})


if __name__ == "__main__":
    sys.exit(main())
