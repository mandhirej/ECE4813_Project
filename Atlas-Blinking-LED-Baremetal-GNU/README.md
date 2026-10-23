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
| `game_config.h` | **the deck**: 4 ranks per suit column (D: A-4, C: 8-J, H: 8-J, S: A-4) in `game_deck_ranks`, 2 own jokers, 2 opponent jokers, and the switches `fixed_deck_enable` / `seven_to_eight_enable` (see below) |
| `game_core.h` | game rules: insertion order, legal cards, joker choice |
| `vision_core.h` | snapshot capture (480x270 and the high-resolution DRAW window), cache maintenance, CNN upload (card or gesture mode) |
| `board_core.h` | board detection: card quads per cell, 64x64 crops, high-resolution averaged DRAW crop, corner-pip colour |
| `gesture_core.h` | hand gestures: HG zone crop and preprocessing, 3 s hold vote, input mode (H / SW0 / PALM), capture mode `C`, `GESTURE_FROM_TOP` |
| `hdmi_text_v16_4.h` | HDMI text rows over the mailbox; FIST/PEACE/PALM labels in gesture mode |
| `fixed_board_grid.h` | board geometry in camera pixels (must match `fixed_board_grid.vh` in the Quartus project) |
| `reference_crop.h` | a board-verified card crop used for checks |
| `hwlib/` | Intel SoC FPGA hardware library |
| `u-boot-spl.axf`, `u-boot-spl.dtb`, `debug-hosted.ds` | preloader and debugger script used by the launch configuration |

## Fixed deck switches (`game_config.h`)

| Switch | 1 | 0 |
|---|---|---|
| `fixed_deck_enable` | fixed deck from `game_deck_ranks`: a drawn card with a deck rank but the wrong suit is corrected to the remaining deck card of the same rank and **colour** (2C -> 2S, 9D -> 9H); a rank not in the deck is rejected (HDMI: `WRONG RANK CARD`, take it out of DRAW); game ends at 16 deck cards | no fixed deck: every card is taken exactly as the CNN reads it (no correction); a card already on the board is rejected as a duplicate; game ends when the board is full |
| `seven_to_eight_enable` | a CNN "7" counts as 8 (only with the fixed deck on, and only if the deck has an 8 and no 7) | a 7 is a wrong-rank card and is rejected |

Default in the repo: `fixed_deck_enable = 0` (no fixed deck), `seven_to_eight_enable = 1`.
Both are global variables, so they can also be changed in the Arm DS Variables view
before starting a game (not during one).

## Refused DRAW card

If a recognised DRAW card is refused (already on the board, column full, rank not in the deck),
the console and HDMI show the reason and DRAW is scanned again automatically.

## Console commands (main menu)

`G` new game, `A` camera/board status (after leaving a game: **continue it**), `H` hand gestures (or flip SW0 up),
`C` capture gesture images to the PC, `Q` stop.
