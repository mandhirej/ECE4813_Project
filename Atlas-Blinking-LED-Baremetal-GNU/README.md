# Atlas-Blinking-LED-Baremetal-GNU: HPS bare-metal program

Arm Development Studio project (GNU toolchain, `Makefile`), runs on the Cortex-A9
with semihosting (Arm DS console for input/output).

## Build and run

Import the folder into Arm DS, **Clean** then **Build** (produces `atlas_main.axf`),
then start `Atlas-Blinking-LED-Baremetal-Debug.launch` (loads `u-boot-spl.axf` first).

## Source files

| File | Purpose |
|---|---|
| `atlas_main.c` | game flow (Suit Stack Race): menus, turns, 120 s timer, draw / placement / verification, jokers, input handling (keyboard, hand gestures, H key, SW0) |
| `game_config.h` | **the deck**: 4 ranks per suit column (D: A-4, C: 8-J, H: 8-J, S: A-4), 2 own jokers, 2 opponent jokers |
| `game_core.h` | game rules: insertion order, legal cards, joker choice |
| `vision_core.h` | snapshot capture (480x270 and the high-resolution DRAW window), cache maintenance, CNN upload (card or gesture mode) |
| `board_core.h` | board detection: card quads per cell, 64x64 crops, high-resolution averaged DRAW crop, corner-pip colour |
| `gesture_core.h` | hand gestures: HG zone crop and preprocessing, 3 s hold vote, input mode (H / SW0 / PALM), capture mode `C`, `GESTURE_FROM_TOP` |
| `hdmi_text_v16_4.h` | HDMI text rows over the mailbox; FIST/PEACE/PALM labels in gesture mode |
| `fixed_board_grid.h` | board geometry in camera pixels (must match `fixed_board_grid.vh` in the Quartus project) |
| `reference_crop.h` | a board-verified card crop used for checks |
| `hwlib/` | Intel SoC FPGA hardware library |
| `u-boot-spl.axf`, `u-boot-spl.dtb`, `debug-hosted.ds` | preloader and debugger script used by the launch configuration |

## Console commands (main menu)

`G` new game, `A` camera/board status, `H` hand gestures (or flip SW0 up),
`C` capture gesture images to the PC, `Q` stop.
