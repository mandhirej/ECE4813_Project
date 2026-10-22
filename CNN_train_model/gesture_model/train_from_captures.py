"""Retrain the gesture rows with REAL board-camera images from capture mode.

Put the hg_<label>_<session>_<n>.pgm files (written by Arm DS, menu key C) into
gesture_captures/ and run:  python gesture_work/train_from_captures.py

Uses the captures (lightly augmented) together with the photo dataset in
gesture_dataset_64x64/train. The LAST capture session of every label is held out and
never trained on: its accuracy is the real-camera number to trust. Exports to
gesture_export/ like train_gesture_head.py; copy the two files into Quartus as usual.
"""
import glob
import os
import re
import sys

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import train_gesture_head as T  # noqa: E402
from prep_gesture_dataset import preprocess_zone  # noqa: E402

CAP = os.path.join(PROJECT, "gesture_captures")
LABELS = {"fist": 0, "palm": 1, "peace": 2, "nothing": 3}
rng = np.random.default_rng(5)


def augment_zone(z):
    h, w = z.shape
    M = cv2.getRotationMatrix2D((w / 2, h / 2), rng.uniform(-12, 12), rng.uniform(0.92, 1.08))
    M[:, 2] += rng.uniform(-4, 4, 2)
    g = cv2.warpAffine(z.astype(np.float32), M, (w, h), borderMode=cv2.BORDER_REFLECT)
    if rng.random() < 0.5:
        g = g[:, ::-1]
    g = g * rng.uniform(0.8, 1.2) + rng.uniform(-15, 15) + rng.normal(0, rng.uniform(0, 4), g.shape)
    return preprocess_zone(np.clip(g, 0, 255).astype(np.uint8))


def main():
    files = sorted(glob.glob(os.path.join(CAP, "hg_*.pgm")))
    if not files:
        sys.exit(f"no captures in {CAP}")
    by = {}
    for f in files:
        m = re.match(r"hg_(fist|palm|peace|nothing)_([0-9a-f]+)_\d+\.pgm", os.path.basename(f))
        if m:
            by.setdefault((m.group(1), m.group(2)), []).append(f)
    held = {lab: sorted(s for (l, s) in by if l == lab)[-1] for lab in LABELS if any(l == lab for (l, _) in by)}
    tr_img, tr_y, te_img, te_y = [], [], [], []
    for (lab, sess), fl in by.items():
        for f in fl:
            z = cv2.imread(f, cv2.IMREAD_GRAYSCALE)
            if sess == held.get(lab) and len([s for (l, s) in by if l == lab]) > 1:
                te_img.append(preprocess_zone(z)); te_y.append(LABELS[lab])
            else:
                tr_img.append(preprocess_zone(z)); tr_y.append(LABELS[lab])
                for _ in range(8):
                    tr_img.append(augment_zone(z)); tr_y.append(LABELS[lab])
    ph_img, ph_y = T.load_split("train")
    p, w1, b1, w2, b2 = T.load_hw_weights()
    x = T.int_features(np.concatenate([np.stack(tr_img), ph_img]), p, w1, b1, w2, b2)
    y = np.concatenate([np.array(tr_y), ph_y])
    print(f"training on {len(tr_y)} capture images (+aug) and {len(ph_y)} photo images")
    wq, bq, bias_scale, ws = T.train_head(p, x, y)
    if te_img:
        tx = T.int_features(np.stack(te_img), p, w1, b1, w2, b2)
        pred = T.int_predict(T.int_scores(tx, wq, bq))
        ty = np.array(te_y)
        print(f"HELD-OUT real-camera sessions: accuracy {(pred == ty).mean():.3f} ({(pred == ty).sum()}/{len(ty)})")
        print(T.fmt_conf(T.confusion(ty, pred)))
    else:
        print("(only one session per label: capture each gesture twice to get a held-out test)")
    np.savez(os.path.join(PROJECT, "gesture_export", "gesture_head_int.npz"), wq=wq, bq=bq)
    T.export_head(wq, bq, ws)
    print("exported to gesture_export/ - copy weights_linear_gesture.mif and linear_bias_gesture.vh "
          "(as linear_bias.vh) into the Quartus project, then run gesture_work/sim/run_sim.sh")


if __name__ == "__main__":
    main()
