"""Pixel mock-up of the HDMI screen, current layout vs proposed layout.

Uses the 5x7 font parsed from DE10_Nano_D8M_DDR3.v and the same coordinate rules as
suit_stack_ui_shader_v16 (480x270 UI texels, each shown as 4x4 HDMI pixels) and
fixed_grid_overlay_v16 (240x135 camera thumbnail, each shown as 3x3 HDMI pixels).
Writes gesture_work/hdmi_mockup_current.png, _proposed.png and _compare.png.
"""
import os
import re

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(os.path.dirname(HERE), "DE10_Nano_D8M_DDR3_CNN", "DE10_Nano_D8M_DDR3_CNN", "DE10_Nano_D8M_DDR3.v")

# ---------------------------------------------------------------- font from the RTL
src = open(TOP).read()
body = src[src.index("function [4:0] font5x7_row"):]
body = body[:body.index("endfunction")]
# explicit rows override, default fills the rest
FONT = {}
for ch, rows in re.findall(r'"(.)": case\(row\)\s*(.*?)endcase', body):
    explicit, default = {}, 0
    for sel, val in re.findall(r"([\d,]+|default):font5x7_row=(?:5'b)?([01]+)", rows.replace(" ", "")):
        v = int(val, 2)
        if sel == "default":
            default = v
        else:
            for s in sel.split(","):
                explicit[int(s)] = v
    FONT[ch] = [explicit.get(r, default) for r in range(7)]


def hexrgb(h):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255)


BG, PANEL, LIGHT, MUTED = 0x0A1222, 0x17263B, 0xEEF3F8, 0x91A0B5
CYAN, ORANGE, GREEN, RED, MAGENTA, INK = 0x20F0E0, 0xFFB347, 0x48E078, 0xFF5858, 0xE060D8, 0x101820
GUIDE = {1: (32, 240, 224), 2: (12, 20, 28), 3: (255, 255, 255), 4: (72, 224, 120), 5: (255, 88, 88),
         6: (255, 190, 72), 7: (64, 144, 255), 8: (179, 102, 255), 9: (255, 224, 48), 10: (255, 78, 215)}


class UI:
    """480x270 texel canvas with the shader's text rule (zoom 0: 6 px/char, zoom 1: 12)."""

    def __init__(self):
        self.px = np.zeros((270, 480, 3), np.uint8)

    def rect(self, x0, y0, x1, y1, c):
        self.px[y0:y1, x0:x1] = hexrgb(c)

    def frame(self, x0, y0, x1, y1, c):
        self.rect(x0, y0, x1, y0 + 1, c); self.rect(x0, y1 - 1, x1, y1, c)
        self.rect(x0, y0, x0 + 1, y1, c); self.rect(x1 - 1, y0, x1, y1, c)

    def text(self, tx, ty, s, c, zoom=0):
        z = 1 << zoom
        for i, ch in enumerate(s[: 14 if zoom else 28]):
            rows = FONT.get(ch, [0] * 7)
            for gy in range(7):
                for gx in range(5):
                    if rows[gy] >> (4 - gx) & 1:
                        x, y = tx + (i * 6 + gx) * z, ty + gy * z
                        self.px[y:y + z, x:x + z] = hexrgb(c)


def board(ui):
    """Left 4x4 board, unchanged in both layouts."""
    ui.rect(8, 28, 272, 258, 0x1C2B43)
    ui.text(12, 9, "SUIT STACK", LIGHT, 1)
    for col, name in enumerate("DCHS"):
        ui.text(12 + col * 64 + 24, 23, name, CYAN)
    for row in range(4):
        for col in range(4):
            x0, y0 = 12 + col * 64, 32 + row * 56
            ui.rect(x0 + 1, y0 + 2, x0 + 57, y0 + 54, 0x080F20)
            ui.rect(x0, y0, x0 + 56, y0 + 52, 0x53677F)
            ui.rect(x0 + 1, y0 + 1, x0 + 55, y0 + 51, 0x24354D)
            for ly in range(1, 51):
                for lx in range(1, 55):
                    if (lx + ly) & 7 == 0:
                        ui.px[y0 + ly, x0 + lx] = hexrgb(0x334762)
            ui.rect(x0, y0 + 42, x0 + 56, y0 + 52, 0x53677F)
            ui.text(col * 64 + 15, row * 56 + 76, "DCHS"[col] + str(row + 1), INK)


TEXT = ["SYSTEM MENU", "PALM: NEW GAME", "PEACE: SCAN CAMERA", "HOLD GESTURE FOR 3 S", "FIST: KEYBOARD INPUT",
        "WAITING FOR STARTING TEAM", "PALM=ENTER PEACE=J FIST=Q", "HAND: PALM 1.5/3.0 S"]


def current_ui():
    ui = UI()
    ui.rect(0, 0, 480, 270, BG)
    board(ui)
    ui.rect(280, 8, 472, 151, PANEL)
    ui.rect(280, 142, 472, 264, PANEL)
    ui.rect(287, 155, 469, 259, 0x0A1222)
    ui.rect(286, 52, 469, 158, 0x03070E); ui.frame(286, 52, 469, 158, 0x324A60)
    ui.rect(280, 8, 282, 151, ORANGE)
    ui.text(288, 12, "TEAM STATUS / ACTION", ORANGE)
    ui.text(288, 30, "JOKERS   LEFT     ROUND", CYAN)
    ui.text(288, 40, "2        16      001", LIGHT)
    ui.text(288, 52, "INSTRUCTIONS / TURN", CYAN)
    ui.text(287, 62, TEXT[0], ORANGE, 1)
    for i in range(1, 5):
        ui.text(287, 81 + (i - 1) * 9, TEXT[i], LIGHT)
    ui.text(288, 120, "DEBUG / LAST 3 EVENTS", CYAN)
    for i in range(5, 8):
        ui.text(287, 130 + (i - 5) * 9, TEXT[i], MUTED)
    # timer exists at y 174..240 but the camera preview is drawn on top of it
    ui.text(288, 174, "TURN TIMER", CYAN)
    ui.text(288, 189, "01:45", GREEN, 1)
    return ui


CAM_Y_CUR, CAM_Y_NEW = 624, 564     # HDMI y of the camera thumbnail (x is 1152 in both)


def proposed_ui():
    ui = UI()
    ui.rect(0, 0, 480, 270, BG)
    board(ui)
    ui.rect(280, 8, 472, 264, PANEL)
    ui.rect(280, 8, 282, 140, ORANGE)
    ui.text(288, 11, "TEAM STATUS", ORANGE)
    # turn timer, top-right corner; green = our turn, magenta = opponent, red = expired
    ui.text(408, 8, "01:45", GREEN, 1)
    ui.text(288, 23, "JOKERS   LEFT     ROUND", CYAN)
    ui.text(288, 32, "2        16      001", LIGHT)
    ui.rect(286, 41, 469, 141, 0x03070E); ui.frame(286, 41, 469, 141, 0x324A60)
    ui.text(288, 43, "INSTRUCTIONS / TURN", CYAN)
    ui.text(287, 52, TEXT[0], ORANGE, 1)
    for i in range(1, 5):
        ui.text(287, 68 + (i - 1) * 9, TEXT[i], LIGHT)
    ui.text(288, 104, "DEBUG / LAST 3 EVENTS", CYAN)
    for i in range(5, 8):
        ui.text(287, 113 + (i - 5) * 9, TEXT[i], MUTED)
    # legend, two rows under the camera thumbnail (thumbnail ends at HDMI y 969 = UI y 242)
    items = [("GOOD", 4), ("EMPTY", 1), ("BAD", 5), ("JOKER", 10), ("MOVE", 9), ("PLACE", 8), ("DRAW", 7)]
    for k, (name, c) in enumerate(items):
        r, i = divmod(k, 4)
        x, y = 290 + i * 45, 246 + r * 10
        ui.px[y:y + 6, x:x + 5] = GUIDE[c]
        ui.text(x + 8, y, name, LIGHT)
    return ui


def camera_thumb(proposed):
    """240x135 thumbnail: dark board + the grid overlay (cells, DRAW, legend or HG zone)."""
    rng = np.random.default_rng(1)
    yy, xx = np.mgrid[0:135, 0:240]
    base = 28 + 10 * np.sin(xx / 40.0) + 6 * (yy / 135.0) + rng.normal(0, 2, (135, 240))
    img = np.repeat(np.clip(base, 0, 255)[..., None], 3, 2).astype(np.uint8)
    ink = np.zeros((135, 240), np.uint8)
    X0, Y0, SX, SY, W, H, DX, DY = 5, 9, 45, 29, 45, 29, 185, 96

    def cell_label(x0, y0, txt):
        ink[y0 + 2:y0 + 11, x0 + 2:x0 + 2 + 6 * len(txt) + 1] = 2
        for i, ch in enumerate(txt):
            for gy in range(7):
                for gx in range(5):
                    if FONT[ch][gy] >> (4 - gx) & 1:
                        ink[y0 + 3 + gy, x0 + 3 + i * 6 + gx] = 3

    def outline(x0, y0, w, h, c):
        ink[y0, x0:x0 + w] = c; ink[y0 + h - 1, x0:x0 + w] = c
        ink[y0:y0 + h, x0] = c; ink[y0:y0 + h, x0 + w - 1] = c

    for r in range(4):
        for c in range(4):
            outline(X0 + c * SX, Y0 + r * SY, W, H, 1)
            cell_label(X0 + c * SX, Y0 + r * SY, "DCHS"[c] + str(r + 1))
    outline(DX, DY, W, H, 7)
    cell_label(DX, DY, "DR")
    if proposed:
        # HG: the three free cells above DRAW merged into one rectangle (the gesture zone)
        outline(DX, Y0, W, DY - Y0, 6)
        outline(DX + 1, Y0 + 1, W - 2, DY - Y0 - 2, 6)
        cell_label(DX, Y0, "HG")
    else:
        ink[8:94, 185:240] = 2
        items = [("GOOD", 4), ("EMPTY", 1), ("BAD", 5), ("JOKER", 10), ("MOVE", 9), ("PLACE", 8), ("DRAW", 7)]
        for k, (name, c) in enumerate(items):
            y = 8 + 12 * k
            ink[y + 3:y + 9, 188:193] = c
            for i, ch in enumerate(name):
                for gy in range(7):
                    for gx in range(5):
                        if FONT[ch][gy] >> (4 - gx) & 1:
                            ink[y + 3 + gy, 198 + i * 6 + gx] = 3
    for k, rgb in GUIDE.items():
        img[ink == k] = rgb
    return img


def compose(ui, cam_y, proposed):
    hd = np.kron(ui.px, np.ones((4, 4, 1), np.uint8))
    thumb = np.kron(camera_thumb(proposed), np.ones((3, 3, 1), np.uint8))
    hd[cam_y:cam_y + 405, 1152:1872] = thumb
    return Image.fromarray(hd)


def main():
    cur = compose(current_ui(), CAM_Y_CUR, False)
    new = compose(proposed_ui(), CAM_Y_NEW, True)
    cur.save(os.path.join(HERE, "hdmi_mockup_current.png"))
    new.save(os.path.join(HERE, "hdmi_mockup_proposed.png"))
    w = 960
    a, b = cur.resize((w, 540), Image.NEAREST), new.resize((w, 540), Image.NEAREST)
    sheet = Image.new("RGB", (w * 2 + 20, 580), (255, 255, 255))
    sheet.paste(a, (0, 40)); sheet.paste(b, (w + 20, 40))
    d = ImageDraw.Draw(sheet)
    d.text((10, 12), "CURRENT", fill=(0, 0, 0)); d.text((w + 30, 12), "PROPOSED", fill=(0, 0, 0))
    sheet.save(os.path.join(HERE, "hdmi_mockup_compare.png"))
    print("written")


if __name__ == "__main__":
    main()
