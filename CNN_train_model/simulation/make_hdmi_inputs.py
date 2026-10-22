"""Inputs for tb_hdmi_frame.v: a 240x135 camera thumbnail (dark board, a palm in the
HG zone) as hex, plus the 8 HPS text rows as a 1792-bit hex word."""
import glob
import os

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))
B = os.path.join(HERE, "build_hdmi")
os.makedirs(B, exist_ok=True)

rng = np.random.default_rng(3)
yy, xx = np.mgrid[0:135, 0:240]
board = 26 + 8 * np.sin(xx / 37.0) + 6 * yy / 135.0 + rng.normal(0, 2, (135, 240))
img = np.repeat(np.clip(board, 0, 255)[..., None], 3, 2)

# a palm photo in the HG zone (thumbnail x 185..229, y 38..124), wrist at the bottom
palm = sorted(glob.glob(os.path.join(PROJECT, "gesture_raw", "*", "Open_palm_dataset", "*.jpg")))[5]
bgr = cv2.imdecode(np.fromfile(palm, np.uint8), cv2.IMREAD_COLOR)
bgr = cv2.rotate(bgr, cv2.ROTATE_90_CLOCKWISE) if bgr.shape[1] > bgr.shape[0] else bgr
bgr = bgr[::-1, ::-1]                       # arm from the bottom, fingers up
h, w = bgr.shape[:2]
crop = bgr[int(0.05 * h):int(0.95 * h), int(0.2 * w):int(0.8 * w)]
hand = cv2.resize(crop, (45, 87), interpolation=cv2.INTER_AREA).astype(float)
ycc = cv2.cvtColor(hand.astype(np.uint8), cv2.COLOR_BGR2YCrCb)
skin = ((ycc[..., 1] > 138) & (ycc[..., 2] < 130)).astype(float)[..., None]
zone = img[38:125, 185:230]
img[38:125, 185:230] = skin * hand[..., ::-1] + (1 - skin) * zone

with open(os.path.join(B, "camera.hex"), "w") as f:
    for p in img.reshape(-1, 3).astype(np.uint8):
        f.write(f"{p[0]:02X}{p[1]:02X}{p[2]:02X}\n")

rows = ["SYSTEM MENU", "FIST: NEW GAME", "PEACE: SCAN CAMERA", "HOLD GESTURE FOR 3 S",
        "PALM: KEYBOARD INPUT", "WAITING FOR STARTING TEAM", "LAST HAND: FIST = ENTER", "HAND: FIST 1.5/3.0 S"]
text = "".join(r.ljust(28)[:28] for r in rows)
open(os.path.join(B, "ui_text.hex"), "w").write(text.encode().hex().upper() + "\n")
cv2.imwrite(os.path.join(B, "camera_input.png"), cv2.resize(img.astype(np.uint8)[..., ::-1], (720, 405), interpolation=cv2.INTER_NEAREST))
print("inputs written")
