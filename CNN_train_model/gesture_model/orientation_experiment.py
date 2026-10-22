"""Which training augmentation makes the gesture rows robust to how the hand is held?

Variants (augmentation of the TRAIN photos):
  bottom35 : arm from the bottom, tilt +-35 deg      (current model)
  bottom50 : arm from the bottom, tilt +-50 deg
  both50   : arm from the bottom OR the top, tilt +-50 deg
Train on one capture session, test on the OTHER session's clean photos rotated to
0, +-25, +-45 deg and the same from the top (180 +- ...). Integer model, frozen
card conv layers, exactly like train_gesture_head.py.
"""
import glob
import os
import sys

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import prep_gesture_dataset as P  # noqa: E402
import train_gesture_head as T  # noqa: E402

rng = np.random.default_rng(1)
P.rng = rng


def session(path):
    return "A" if int(os.path.basename(path).split("_")[2][:4]) <= 1813 else "B"


def rotate_ctx(gray_ctx, ang):
    ch, cw = gray_ctx.shape
    M = cv2.getRotationMatrix2D((cw / 2, ch / 2), ang, 1.0)
    return cv2.warpAffine(gray_ctx.astype(np.float32), M, (cw, ch), borderMode=cv2.BORDER_REFLECT)


def aug(gray_ctx, mask_ctx, cls, tilt, both):
    g = gray_ctx.astype(np.float32)
    if both and rng.random() < 0.5:
        g, mask_ctx = g[::-1, ::-1].copy(), mask_ctx[::-1, ::-1].copy()
    old = P.augment.__defaults__
    # reuse P.augment but with a different tilt range
    ch, cw = g.shape
    if cls != "nothing" and rng.random() < 0.7:
        soft = cv2.GaussianBlur(mask_ctx.astype(np.float32) / 255.0, (9, 9), 0)
        g = g * (soft + (1 - soft) * rng.uniform(0.15, 0.8))
    M = cv2.getRotationMatrix2D((cw / 2, ch / 2), rng.uniform(-tilt, tilt), rng.uniform(0.85, 1.2))
    M[0, 2] += rng.uniform(-0.12, 0.12) * P.WIN_W
    M[1, 2] += rng.uniform(-0.12, 0.12) * P.WIN_H
    g = cv2.warpAffine(g, M, (cw, ch), borderMode=cv2.BORDER_REFLECT)
    if rng.random() < 0.5:
        g = g[:, ::-1]
    g = P.center(g)
    g = 255.0 * (np.clip(g, 0, 255) / 255.0) ** rng.uniform(0.7, 1.4)
    g = g * rng.uniform(0.7, 1.2) + rng.uniform(-20, 20)
    if rng.random() < 0.4:
        g = cv2.GaussianBlur(g, (0, 0), rng.uniform(0.5, 2.0))
    g = g + rng.normal(0, rng.uniform(0, 6), g.shape)
    return P.preprocess_zone(np.clip(g, 0, 255).astype(np.uint8))


def main():
    folders = {k: [d for d in glob.glob(os.path.join(P.RAW_ROOT, "*")) if os.path.basename(d).strip() == k][0]
               for k in P.CLASSES}
    data = []   # (cls_index, session, gray_ctx, mask_ctx)
    for ci, (folder, cls) in enumerate(P.CLASSES.items()):
        for f in sorted(glob.glob(os.path.join(folders[folder], "*.jpg"))):
            bc = P.base_crop(f, cls)
            if bc is not None:
                data.append((ci, "both" if cls == "nothing" else session(f), bc[0], bc[1], cls))
    p, w1, b1, w2, b2 = T.load_hw_weights()
    test_angles = [0, -25, 25, -45, 45, 180, 155, 205, 135, 225]
    variants = {"bottom35": (35, False), "bottom50": (50, False), "both50": (50, True)}
    print(f"{'variant':9} {'test':5} " + " ".join(f"{a:>5}" for a in test_angles))
    for name, (tilt, both) in variants.items():
        for tr, te in (("A", "B"), ("B", "A")):
            imgs, ys = [], []
            for ci, s, g, m, cls in data:
                if s == te:
                    continue
                for _ in range(40 if cls != "nothing" else 80):
                    imgs.append(aug(g, m, cls, tilt, both)); ys.append(ci)
            for _ in range(300):
                imgs.append(P.synthetic_board()); ys.append(3)
            x = T.int_features(np.stack(imgs), p, w1, b1, w2, b2)
            wq, bq, _, _ = T.train_head(p, x, np.array(ys))
            accs = []
            for ang in test_angles:
                timgs, tys = [], []
                for ci, s, g, m, cls in data:
                    if s != te:
                        continue
                    timgs.append(P.preprocess_zone(np.clip(P.center(rotate_ctx(g, ang)), 0, 255).astype(np.uint8)))
                    tys.append(ci)
                tx = T.int_features(np.stack(timgs), p, w1, b1, w2, b2)
                pred = (tx @ wq.T + bq).argmax(1)
                accs.append((pred == np.array(tys)).mean())
            print(f"{name:9} {tr}->{te}  " + " ".join(f"{a * 100:5.0f}" for a in accs), flush=True)


if __name__ == "__main__":
    main()
