"""Build iverilog test vectors and the Python integer-model expectations.

Writes into gesture_work/sim/build/:
  images.hex        one byte per line, 4096 lines per image
  modes.hex         one line per image: 0 = card mode, 1 = gesture mode
  expected.txt      name, mode, expected class from the Python integer model, true label
  rom_new.hex       FC ROM (cards + gestures) for the simulation ROM model
  rom_orig.hex      original 54-class FC ROM (for the unmodified-RTL regression run)
"""
import glob
import os
import re
import sys

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
from train_gesture_head import GESTURES, FIRST_CLASS, HW, int_features, load_hw_weights, s32  # noqa: E402

BUILD = os.path.join(HERE, "build")
CARD_NAMES = sorted("""10Clubs 10Diamonds 10Hearts 10Spade 2Clubs 2Diamonds 2Hearts 2Spade 3Clubs 3Diamonds
3Hearts 3Spade 4Clubs 4Diamonds 4Hearts 4Spade 5Clubs 5Diamonds 5Hearts 5Spade 6Clubs 6Diamonds 6Hearts 6Spade
7Clubs 7Diamonds 7Hearts 7Spade 8Clubs 8Diamonds 8Hearts 8Spade 9Clubs 9Diamonds 9Hearts 9Spade AClubs ADiamonds
AHearts ASpade JClubs JDiamonds JHearts JSpade JokerB JokerR KClubs KDiamonds KHearts KSpade QClubs QDiamonds
QHearts QSpade""".split())   # torchvision ImageFolder order = sorted folder names
assert len(CARD_NAMES) == 54
ALL_NAMES = CARD_NAMES + GESTURES


def read_mif(path):
    words = {}
    for line in open(path):
        m = re.match(r"\s*(\d+)\s*:\s*([0-9A-Fa-f]+);", line)
        if m:
            words[int(m.group(1))] = m.group(2).upper()
    return [words[i] for i in range(len(words))]


def fc_from_rom(words, bias_vh):
    n = len(words) // 512
    w = np.zeros((n, 16, 16, 16), np.int64)
    for a, word in enumerate(words):
        c, pong = divmod(a, 512)
        pas, y, x = (pong >> 8) & 1, (pong >> 4) & 0xF, pong & 0xF
        for ch in range(8):
            v = int(word[14 - 2 * ch: 16 - 2 * ch], 16)
            w[c, pas * 8 + ch, y, x] = v - 256 if v & 0x80 else v
    b = np.zeros(n, np.int64)
    for c, h in re.findall(r"6'd(\d+): current_bias = 32'h([0-9A-Fa-f]+)", open(bias_vh).read()):
        if int(c) < n:
            b[int(c)] = s32(h)
    return w.reshape(n, 4096), b


def card_crop(path):
    img = cv2.imread(path, cv2.IMREAD_COLOR)
    gray = ((img[:, :, 2].astype(np.uint16) >> 2) + (img[:, :, 1] >> 1) + (img[:, :, 0] >> 2)).astype(np.uint8)
    ys, xs = np.where(gray > 40)
    crop = gray[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    return cv2.resize(crop, (64, 64), interpolation=cv2.INTER_AREA)


def reference_crop():
    txt = open(os.path.join(PROJECT, "Atlas-Blinking-LED-Baremetal-GNU", "reference_crop.h")).read()
    vals = [int(v) for v in re.findall(r"\d+", txt.split("{", 1)[1].split("}", 1)[0])]
    return np.array(vals, np.uint8).reshape(64, 64)


def main():
    os.makedirs(BUILD, exist_ok=True)
    vectors = []   # (name, mode, image, true_label)
    vectors.append(("reference_crop_10C", 0, reference_crop(), "10Clubs"))
    for f in sorted(glob.glob(os.path.join(PROJECT, "python_files_extracted", "python_files", "run*_fpga_crop_64x64.png"))):
        vectors.append((os.path.basename(f), 0, cv2.imread(f, cv2.IMREAD_GRAYSCALE), "?"))
    for f in sorted(glob.glob(os.path.join(HERE, "card_src", "*.jpg"))):
        label = os.path.basename(f).split("_")[1]
        vectors.append((os.path.basename(f), 0, card_crop(f), label))
    for g in GESTURES:
        for f in sorted(glob.glob(os.path.join(PROJECT, "gesture_dataset_64x64", "test", g, "*.png")))[:3]:
            vectors.append((f"{g}/{os.path.basename(f)}", 1, cv2.imread(f, cv2.IMREAD_GRAYSCALE), g))
    rng = np.random.default_rng(7)
    vectors.append(("all_white", 1, np.full((64, 64), 255, np.uint8), "-"))
    vectors.append(("all_black", 0, np.zeros((64, 64), np.uint8), "-"))
    vectors.append(("noise_card_mode", 0, rng.integers(0, 256, (64, 64), dtype=np.uint8), "-"))
    vectors.append(("noise_gesture_mode", 1, rng.integers(0, 256, (64, 64), dtype=np.uint8), "-"))
    # the same image in both modes, back to back, proves the mode switch per inference
    palm = vectors[[v[3] for v in vectors].index("palm")][2]
    vectors.append(("palm_in_card_mode", 0, palm, "-"))

    p, w1, b1, w2, b2 = load_hw_weights()
    new_words = read_mif(os.path.join(HW, "weights_linear_gesture.mif"))
    orig_words = read_mif(os.path.join(HW, "weights_linear.mif"))
    wfc, bfc = fc_from_rom(new_words, os.path.join(HW, "linear_bias.vh"))
    assert new_words[:len(orig_words)] == orig_words, "card rows changed!"

    imgs = np.stack([v[2] for v in vectors])
    q2 = int_features(imgs, p, w1, b1, w2, b2)
    scores = q2 @ wfc.T + bfc
    assert np.abs(scores).max() < 2 ** 31

    with open(os.path.join(BUILD, "images.hex"), "w") as f:
        for im in imgs:
            f.write("\n".join(f"{v:02X}" for v in im.flatten()) + "\n")
    with open(os.path.join(BUILD, "modes.hex"), "w") as f:
        f.write("\n".join(str(v[1]) for v in vectors) + "\n")
    with open(os.path.join(BUILD, "expected.txt"), "w") as f:
        for (name, mode, _, label), sc in zip(vectors, scores):
            lo, hi = (FIRST_CLASS, FIRST_CLASS + len(GESTURES) - 1) if mode else (0, 53)
            pred = lo + int(np.argmax(sc[lo:hi + 1]))      # first max wins, like the RTL's strict '>'
            f.write(f"{name} {mode} {pred} {ALL_NAMES[pred]} {label}\n")
    for fname, words in (("rom_new.hex", new_words), ("rom_orig.hex", orig_words)):
        open(os.path.join(BUILD, fname), "w").write("\n".join(words) + "\n")
    print(f"{len(vectors)} vectors written to {BUILD}")


if __name__ == "__main__":
    main()
