"""Train ONLY the gesture rows of the FC layer on top of the frozen card CNN.

The two conv layers and the 54 card rows are read from the existing hardware weight
files and are never modified, so card-mode behaviour is unchanged by construction.

Features are computed with the same integer arithmetic as export.py's
verify_quantized_accuracy() (input q 0..255, L1/L2 ReLU + right shift + clamp 0..255).
The gesture rows use the existing FC format (int8 weights, 32-bit bias), but with their
own weight shift: in gesture mode only gesture rows are compared with each other, so a
common scale factor on all of them does not change the argmax and buys int8 precision.

Outputs in gesture_export/:
  weights_linear_gesture.mif   original 54 card rows (copied verbatim) + gesture rows
  linear_bias_gesture.vh       original card cases (copied verbatim) + gesture cases
  gesture_classes.json         class index -> gesture name
  report.txt                   accuracy and confusion matrices (integer model)
"""
import glob
import json
import os
import re

import cv2
import numpy as np
import torch
import torch.nn.functional as F

SEED = 42
FIRST_CLASS = 54
GESTURES = ["fist", "palm", "peace", "nothing"]

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
HW = os.path.join(PROJECT, "DE10_Nano_D8M_DDR3_CNN", "DE10_Nano_D8M_DDR3_CNN")
DATA = os.path.join(PROJECT, "gesture_dataset_64x64")
OUT = os.path.join(PROJECT, "gesture_export")


def s32(h):
    v = int(h, 16)
    return v - (1 << 32) if v & 0x80000000 else v


def s8(h):
    v = int(h, 16)
    return v - 256 if v & 0x80 else v


def load_hw_weights():
    cfg = open(os.path.join(HW, "cnn_quant_config.vh")).read()
    p = {k: int(v) for k, v in re.findall(r"localparam integer (\w+) = (\d+);", cfg)}

    l1 = open(os.path.join(HW, "layer1_weights.vh")).read()
    w1 = np.zeros((8, 1, 3, 3), np.int64)
    b1 = np.zeros(8, np.int64)
    for oc, y, x, h in re.findall(r"f(\d)_w(\d)(\d) = 8'h([0-9A-Fa-f]+)", l1):
        w1[int(oc), 0, int(y) - 1, int(x) - 1] = s8(h)
    for oc, h in re.findall(r"f(\d)_bias = 32'h([0-9A-Fa-f]+)", l1):
        b1[int(oc)] = s32(h)

    w2 = np.zeros((16, 8, 3, 3), np.int64)
    for mac in range(8):
        lines = [l.strip() for l in open(os.path.join(HW, f"weights_mac{mac}.hex")) if l.strip()]
        assert len(lines) == 18
        for i, line in enumerate(lines):
            pas, tap = divmod(i, 9)
            y, x = divmod(tap, 3)
            for ch in range(8):          # ch0 is the least-significant byte
                w2[mac + 8 * pas, ch, y, x] = s8(line[14 - 2 * ch: 16 - 2 * ch])
    b2 = np.zeros(16, np.int64)
    for mac, pas, h in re.findall(r"l2_m(\d)_b(\d) = 32'h([0-9A-Fa-f]+)", open(os.path.join(HW, "layer2_biases.vh")).read()):
        b2[int(mac) + 8 * int(pas)] = s32(h)
    return p, w1, b1, w2, b2


def int_features(imgs_u8, p, w1, b1, w2, b2):
    """Exact integer L1/L2 path from export.py -> q2 flattened (N, 4096) as int."""
    x = torch.from_numpy(imgs_u8.astype(np.float64))[:, None]
    acc1 = F.conv2d(x, torch.from_numpy(w1).double(), torch.from_numpy(b1).double(), stride=2, padding=1)
    q1 = torch.floor(acc1.clamp(min=0) / 2 ** p["L1_SHIFT"]).clamp(0, 255)
    acc2 = F.conv2d(q1, torch.from_numpy(w2).double(), torch.from_numpy(b2).double(), stride=2, padding=1)
    q2 = torch.floor(acc2.clamp(min=0) / 2 ** p["L2_SHIFT"]).clamp(0, 255)
    return q2.flatten(1).numpy().astype(np.int64)


def load_split(split):
    imgs, labels = [], []
    for ci, g in enumerate(GESTURES):
        for f in sorted(glob.glob(os.path.join(DATA, split, g, "*.png"))):
            imgs.append(cv2.imread(f, cv2.IMREAD_GRAYSCALE))
            labels.append(ci)
    return np.stack(imgs), np.array(labels)


def int_scores(q2, wq, bq):
    return q2 @ wq.T + bq                          # exactly what the serial FC accumulates


def int_predict(scores):
    return scores.argmax(1)                        # strict '>' in RTL -> first max wins, same as argmax


def confusion(y, pred):
    m = np.zeros((len(GESTURES), len(GESTURES)), int)
    for a, b in zip(y, pred):
        m[a, b] += 1
    return m


def fmt_conf(m):
    head = "true\\pred " + " ".join(f"{g:>8}" for g in GESTURES)
    rows = [f"{g:>9} " + " ".join(f"{v:>8}" for v in m[i]) for i, g in enumerate(GESTURES)]
    return "\n".join([head] + rows)


def train_head(p, x_int, y, epochs=60):
    """Float logistic regression on the integer features, then int8 quantisation.

    Fixed epoch count with weight decay: val/test are too small to pick an epoch from.
    """
    torch.manual_seed(SEED)
    feat_scale = (1 << p["L2_ACT_LOG2"]) / 255.0          # q2 -> real L2 activation
    xtr = torch.tensor(x_int * feat_scale, dtype=torch.float32)
    ytr = torch.tensor(y)
    counts = torch.bincount(ytr, minlength=len(GESTURES)).float().clamp(min=1)
    class_w = (counts.sum() / counts) / len(GESTURES)

    lin = torch.nn.Linear(4096, len(GESTURES))
    torch.nn.init.zeros_(lin.weight)
    torch.nn.init.zeros_(lin.bias)
    opt = torch.optim.Adam(lin.parameters(), lr=1e-3, weight_decay=1e-3)
    for _ in range(epochs):
        perm = torch.randperm(len(ytr))
        for i in range(0, len(ytr), 128):
            idx = perm[i:i + 128]
            loss = F.cross_entropy(lin(xtr[idx]), ytr[idx], weight=class_w, label_smoothing=0.05)
            opt.zero_grad()
            loss.backward()
            opt.step()

    w = lin.weight.detach().numpy()
    ws = max(0, min(14, int(np.floor(np.log2(127.0 / float(np.abs(w).max()))))))
    bias_scale = 255.0 * (1 << ws) / (1 << p["L2_ACT_LOG2"])
    wq = np.clip(np.round(w * (1 << ws)), -128, 127).astype(np.int64)
    bq = np.round(lin.bias.detach().numpy() * bias_scale).astype(np.int64)
    return wq, bq, bias_scale, ws


def load_by_session(p, w1, b1, w2, b2):
    """Clean (non-augmented) photos of every split, tagged A (<=18:13) or B (>=18:15)."""
    imgs, ys, sess, aug = [], [], [], []
    for split in ("train", "val", "test"):
        for ci, g in enumerate(GESTURES):
            for f in sorted(glob.glob(os.path.join(DATA, split, g, "*.png"))):
                name = os.path.basename(f)
                if name.startswith("synthetic"):
                    s = "both"
                else:
                    s = "A" if int(name.split("_")[2][:4]) <= 1813 else "B"
                if g == "nothing":
                    s = "both"                      # only one nothing session exists
                imgs.append(cv2.imread(f, cv2.IMREAD_GRAYSCALE))
                ys.append(ci)
                sess.append(s)
                aug.append("_aug" in name or name.startswith("synthetic") or split != "train")
    x = int_features(np.stack(imgs), p, w1, b1, w2, b2)
    return x, np.array(ys), np.array(sess), np.array(aug)


def cross_session(p, w1, b1, w2, b2):
    """Train on one capture session's photos, test on the other session's photos."""
    x, y, sess, _ = load_by_session(p, w1, b1, w2, b2)
    names = []
    out = []
    for tr, te in (("A", "B"), ("B", "A")):
        is_test = (sess == te)
        is_train = ~is_test
        wq, bq, _, _ = train_head(p, x[is_train], y[is_train])
        pred = int_predict(int_scores(x[is_test], wq, bq))
        acc = (pred == y[is_test]).mean()
        out.append(f"[cross-session train {tr} -> test {te}] accuracy {acc:.3f} ({(pred == y[is_test]).sum()}/{is_test.sum()})")
        out.append(fmt_conf(confusion(y[is_test], pred)))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    p, w1, b1, w2, b2 = load_hw_weights()

    data = {}
    for split in ("train", "val", "test"):
        imgs, y = load_split(split)
        data[split] = (int_features(imgs, p, w1, b1, w2, b2), y)
        print(f"{split}: {len(y)} images")

    wq, bq, bias_scale, ws = train_head(p, *data["train"])
    ep = 60

    report = [f"Frozen card conv layers; trained {len(GESTURES)} FC rows only (classes {FIRST_CLASS}..{FIRST_CLASS + len(GESTURES) - 1}).",
              f"Trained {ep} epochs. All numbers below use the exported INTEGER weights.", ""]
    for split in ("train", "val", "test"):
        x, y = data[split]
        sc = int_scores(x, wq, bq)
        pred = int_predict(sc)
        report.append(f"[{split}] accuracy {(pred == y).mean():.3f} ({(pred == y).sum()}/{len(y)})")
        report.append(fmt_conf(confusion(y, pred)))
        if split == "test":
            srt = np.sort(sc, 1)
            margin = (srt[:, -1] - srt[:, -2]) / bias_scale
            report.append("test top1-top2 margin (logit units): " + ", ".join(f"{m:.1f}" for m in margin))
        report.append("")
    report += cross_session(p, w1, b1, w2, b2) + [""]
    report.append(f"gesture weight shift {ws} (card rows keep FC_WEIGHT_SHIFT {p['FC_WEIGHT_SHIFT']})")
    report.append(f"max |weight| int8 = {int(np.abs(wq).max())}, bias range {bq.min()}..{bq.max()}")
    worst = int(np.abs(wq).sum(1).max()) * 255 + int(np.abs(bq).max())
    report.append(f"worst-case |accumulator| = {worst} (must be < 2^31 = {2**31})")
    text = "\n".join(report)
    print(text)
    open(os.path.join(OUT, "report.txt"), "w").write(text + "\n")
    export_head(wq, bq, ws)


def export_head(wq, bq, ws):
    """Write the MIF (card rows copied verbatim + gesture rows) and the bias include."""
    os.makedirs(OUT, exist_ok=True)
    mif_lines = open(os.path.join(HW, "weights_linear.mif")).read().splitlines()
    body = [l for l in mif_lines if re.match(r"\s*\d+\s*:", l)]
    assert len(body) == FIRST_CLASS * 512
    to_hex8 = lambda v: f"{int(v) & 0xFF:02X}"
    wq3 = wq.reshape(len(GESTURES), 16, 16, 16)
    total = (FIRST_CLASS + len(GESTURES)) * 512
    with open(os.path.join(OUT, "weights_linear_gesture.mif"), "w") as f:
        f.write(f"WIDTH=64;\nDEPTH={total};\nADDRESS_RADIX=UNS;\nDATA_RADIX=HEX;\nCONTENT BEGIN\n")
        f.write("\n".join(body) + "\n")
        addr = FIRST_CLASS * 512
        for c in range(len(GESTURES)):
            for pong in range(512):
                pas, y, x = (pong >> 8) & 1, (pong >> 4) & 0xF, pong & 0xF
                word = "".join(to_hex8(wq3[c, pas * 8 + ch, y, x]) for ch in reversed(range(8)))
                f.write(f"\t{addr} : {word};\n")
                addr += 1
        f.write("END;\n")

    # card-only source: drop gesture cases from an earlier install of this script's output
    vh = "".join(l for l in open(os.path.join(HW, "linear_bias.vh")).readlines() if "// gesture" not in l)
    extra = "".join(f"        6'd{FIRST_CLASS + c}: current_bias = 32'h{int(bq[c]) & 0xFFFFFFFF:08X}; // gesture {g}\n"
                    for c, g in enumerate(GESTURES))
    assert "        default:" in vh
    open(os.path.join(OUT, "linear_bias_gesture.vh"), "w").write(vh.replace("        default:", extra + "        default:", 1))
    json.dump({FIRST_CLASS + i: g for i, g in enumerate(GESTURES)}, open(os.path.join(OUT, "gesture_classes.json"), "w"), indent=2)
    np.savez(os.path.join(OUT, "gesture_head_int.npz"), wq=wq, bq=bq)
    print("exported to", OUT)


if __name__ == "__main__":
    main()
