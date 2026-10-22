"""Render the HDMI screen for every step of a game-simulation trace.

Display state (text rows, cells, guide colours, timer...) comes from trace.jsonl,
which the simulated FPGA mailbox wrote; the camera thumbnail comes from the
virtual camera frame of that step. Drawing rules follow suit_stack_ui_shader_v16 /
fixed_grid_overlay_v16 (new layout), the same as hdmi_mockup.py, which matched the
iverilog-rendered frame.

Writes <run>/frames/step_NNN.png, <run>/game.gif and <run>/game_steps.html.
"""
import json
import os
import re
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
import hdmi_mockup as M  # noqa: E402

# 16x16 suit sprites from the shader (suit_row): 0 club, 1 diamond, 2 heart, 3 spade
SUIT = {}
body = M.src[M.src.index("function [15:0] suit_row"):]
body = body[:body.index("end endfunction")]
for sid, rows in re.findall(r"3'd(\d): case\(r\)\s*(.*?)endcase", body):
    bits = [0] * 16
    for sel, val in re.findall(r"([\d,]+|default):suit_row=(?:16'b)?([01]+|0)", rows.replace(" ", "")):
        if sel == "default":
            continue
        for s_ in sel.split(","):
            bits[int(s_)] = int(val, 2) if len(val) > 1 else 0
    SUIT[int(sid)] = bits


def rank_text(c):
    if c <= 3: return "10"
    if c <= 35: return str(2 + ((c - 4) >> 2))
    if c <= 39: return "A"
    if c <= 45: return "J"
    if c <= 49: return "K"
    return "Q"


def suit_of(c):
    if c <= 43: return c & 3
    if c <= 49: return (c - 46) & 3
    return (c - 50) & 3


def board_cells(ui, cls, st):
    for row in range(4):
        for col in range(4):
            i = row * 4 + col
            x0, y0 = 12 + col * 64, 32 + row * 56
            s, c = st[i], cls[i]
            frame = {0: 0x53677F, 1: M.ORANGE, 2: M.MAGENTA}.get(s, M.GREEN)
            ui.rect(x0 + 1, y0 + 2, x0 + 57, y0 + 54, 0x080F20)
            ui.rect(x0, y0, x0 + 56, y0 + 52, frame)
            if s == 0:
                ui.rect(x0 + 1, y0 + 1, x0 + 55, y0 + 51, 0x24354D)
                for ly in range(1, 51):
                    for lx in range(1, 55):
                        if (lx + ly) & 7 == 0:
                            ui.px[y0 + ly, x0 + lx] = M.hexrgb(0x334762)
            else:
                ui.rect(x0 + 1, y0 + 1, x0 + 55, y0 + 51, 0xFFF0F4)
                if c in (44, 45):
                    ui.text(col * 64 + 25, row * 56 + 51, "JOKER", M.MAGENTA)
                else:
                    su = suit_of(c)
                    red = su in (1, 2)
                    ui.text(col * 64 + 16, row * 56 + 36, rank_text(c), M.RED if red else M.INK, 1)
                    for ly in range(16):
                        for lx in range(16):
                            if SUIT[su][ly] >> (15 - lx) & 1:
                                ui.px[y0 + 22 + ly, x0 + 20 + lx] = M.hexrgb(0xC83E52 if red else M.INK)
            ui.rect(x0, y0 + 42, x0 + 56, y0 + 52, frame)
            ui.text(col * 64 + 15, row * 56 + 76, "DCHS"[col] + str(row + 1), M.INK)


def ui_for(step):
    ui = M.UI()
    ui.rect(0, 0, 480, 270, M.BG)
    ui.rect(8, 28, 272, 258, 0x1C2B43)
    ui.text(12, 9, "SUIT STACK", M.LIGHT, 1)
    for col, name in enumerate("DCHS"):
        ui.text(12 + col * 64 + 24, 23, name, M.CYAN)
    board_cells(ui, step["cls"], step["st"])
    ui.rect(280, 8, 472, 264, M.PANEL)
    ui.rect(280, 8, 282, 141, M.ORANGE)
    ui.rect(287, 141, 469, 262, 0x0A1222)
    ui.rect(286, 41, 469, 141, 0x03070E); ui.frame(286, 41, 469, 141, 0x324A60)
    ui.text(288, 11, "TEAM STATUS", M.ORANGE)
    valid, running, expired, opp, left = step["tt"]
    if valid:
        ui.text(402, 11, f"TIMER {left // 60:02d}:{left % 60:02d}", M.RED if expired else (M.MAGENTA if opp else M.GREEN))
    remaining = 16 - sum(1 for s in step["st"] if s == 3)
    ui.text(288, 23, "JOKERS   LEFT     ROUND", M.CYAN)
    ui.text(288, 32, f"{step['score']}        {remaining:02d}      {step['turn']:03d}", M.LIGHT)
    ui.text(288, 43, "INSTRUCTIONS / TURN", M.CYAN)
    t = step["text"]
    title_ink = M.GREEN if t[0].startswith("Y") else M.MAGENTA if t[0].startswith("O") else M.ORANGE
    ui.text(287, 52, t[0], title_ink, 1)
    for i in range(1, 5):
        ui.text(287, 68 + (i - 1) * 9, t[i], M.LIGHT)
    ui.text(288, 104, "DEBUG / LAST 3 EVENTS", M.CYAN)
    for i in range(5, 8):
        ui.text(287, 113 + (i - 5) * 9, t[i], M.MUTED)
    return ui


def thumb_for(step, run):
    cam = np.fromfile(os.path.join(run, step["cam"]), np.uint8).reshape(270, 480, 3)[::2, ::2].copy()
    ink = np.zeros((160, 240), np.uint8)
    X0, Y0, S_X, S_Y, Wc, Hc, DX, DY = 5, 9, 45, 29, 45, 29, 185, 9   # DRAW at the top of column 5
    gm = step["guide_mode"]
    col_of = {1: 4, 2: 5, 3: 6}

    def outline(x0, y0, w, h, c, t=1):
        for k in range(t):
            ink[y0 + k, x0:x0 + w] = c; ink[y0 + h - 1 - k, x0:x0 + w] = c
            ink[y0:y0 + h, x0 + k] = c; ink[y0:y0 + h, x0 + w - 1 - k] = c

    def label(x0, y0, txt):
        ink[y0 + 2:y0 + 11, x0 + 2:x0 + 15] = 2
        for i, ch in enumerate(txt):
            for gy in range(7):
                for gx in range(5):
                    if M.FONT[ch][gy] >> (4 - gx) & 1:
                        ink[y0 + 3 + gy, x0 + 3 + i * 6 + gx] = 3

    for i in range(16):
        r, c = divmod(i, 4)
        x0, y0 = X0 + c * S_X, Y0 + r * S_Y
        outline(x0, y0, Wc, Hc, col_of.get(step["guide"][i], 1) if gm else 1)
        label(x0, y0, "DCHS"[c] + str(r + 1))
        if step["target_active"] and step["target"] == i:
            outline(x0, y0, Wc, Hc, 8, 3)
        if step["move"] >> i & 1:
            for ly in range(11, 27):
                for lx in range(Wc):
                    if (20 <= lx <= 24 and ly <= 17) or (ly >= 18 and abs(lx - 22) <= 26 - ly):
                        ink[y0 + ly, x0 + lx] = 9
    outline(DX, DY, Wc, Hc, (col_of.get(step["draw"], 7) if gm else 7))
    label(DX, DY, "DR")
    outline(DX, DY + S_Y, Wc, 3 * S_Y, 6, 2)   # HG: the three cells below DRAW
    label(DX, DY + S_Y, "HG")
    img = np.zeros((160, 240, 3), np.uint8)
    img[:135] = cam
    for k, rgb in M.GUIDE.items():
        img[ink == k] = rgb
    return img


def legend(ui):
    items = [("GOOD", 4), ("EMPTY", 1), ("BAD", 5), ("JOKER", 10), ("MOVE", 9), ("PLACE", 8), ("DRAW", 7)]
    return items


def compose(step, run):
    ui = ui_for(step)
    hd = np.kron(ui.px, np.ones((4, 4, 1), np.uint8))
    th = thumb_for(step, run)
    hd[564:564 + 405, 1152:1872] = np.kron(th[:135], np.ones((3, 3, 1), np.uint8))
    # legend rows (overlay rows 136..159 under the thumbnail)
    leg = np.full((24, 240, 3), (12, 20, 28), np.uint8)
    for k, (name, c) in enumerate(legend(None)):
        r, i = divmod(k, 4)
        x, y = i * 60, r * 12
        leg[y + 3:y + 9, x + 3:x + 8] = M.GUIDE[c]
        for j, ch in enumerate(name):
            for gy in range(7):
                for gx in range(5):
                    if M.FONT[ch][gy] >> (4 - gx) & 1:
                        leg[y + 3 + gy, x + 11 + j * 6 + gx] = (255, 255, 255)
    hd[969:969 + 72, 1152:1872] = np.kron(leg, np.ones((3, 3, 1), np.uint8))
    return Image.fromarray(hd)


def main(run):
    steps = [json.loads(l) for l in open(os.path.join(run, "trace.jsonl"))]
    os.makedirs(os.path.join(run, "frames"), exist_ok=True)
    small = []
    for s in steps:
        im = compose(s, run)
        im.resize((960, 540), Image.LANCZOS).save(os.path.join(run, "frames", f"step_{s['n']:03d}.jpg"), quality=85)
        small.append(im.resize((640, 360), Image.LANCZOS))
    small[0].save(os.path.join(run, "game.gif"), save_all=True, append_images=small[1:], duration=900, loop=0)
    print(f"rendered {len(steps)} steps")


if __name__ == "__main__":
    main(sys.argv[1])
