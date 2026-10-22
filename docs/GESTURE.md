# Hand-gesture input (innovation bonus)

Three static hand gestures (fist, open palm, peace), plus a "nothing" class, are recognised
by the **same FPGA CNN accelerator** that recognises the cards. With gesture input switched
on, the whole game can be played without touching the keyboard.

| Gesture (hold 3 s) | Acts as | Who-starts screen | Main menu |
|---|---|---|---|
| Fist | ENTER (confirm / draw / check) | our team starts | new game |
| Peace | J (use own joker) | opponent starts | camera scan |
| Open palm | Q (cancel / back to menu) | cancel game | back to keyboard input |

ENTER, the most used command, is on FIST because fist is the most distinct gesture:
in the game simulation fist read 100% correct on every photo, while palm and peace
are sometimes confused with each other.

**Board layout (fifth column):** DR, the draw slot, is at the **top**, and the three
cells below it are the **HG** hand-gesture zone (camera x 370..459, y 76..249). Hold
the hand in HG coming in **from the bottom edge, fingers pointing up**. That way the
arm never lies across DR or the card grid. Putting a card into DR means reaching
across HG for about a second. That is far shorter than the 3 s hold, and in the
simulation 32 such reaches triggered nothing.

## How it works

1. HPS takes a snapshot, cuts out the 90 x 174 HG zone, converts to gray
   (`(77R+150G+29B+128)>>8`), area-resizes to 64 x 64 and contrast-stretches it
   (2nd/98th percentile, minimum span 48). `gesture_core.h`
2. HPS uploads the 64 x 64 image through the existing CNN bridge and asks for **gesture
   mode**: `bbox_valid=0`, `bbox_min_x=0x7FF`. No Platform Designer change was needed. `vision_core.h`
3. FPGA runs the same conv layer 1 and layer 2. The FC layer then scores only classes 54..57
   (fist, palm, peace, nothing) instead of 0..53. That takes 425,116 cycles (8.5 ms)
   instead of 732,366 (14.6 ms). `CNN.v`
4. HPS hold logic: a vote over the last 3 s. The leading gesture (most readings in
   the window) is counted from the moment it takes the lead. The count restarts
   whenever another gesture takes the lead or the hand leaves. It fires after 3 s if
   it also has more than 60% of the window's readings. After a command the HPS waits
   until HG has been empty for 0.5 s before it acts, and the zone must show
   "nothing" before the next command is accepted.
5. HDMI: with gesture input on, every instruction row names the gesture instead of the
   key (`FIST: DRAW A CARD`, `PEACE: PLACE YOUR JOKER`, `PALM: MAIN MENU`). Debug row
   5 is the event log, row 6 keeps the last accepted gesture (`LAST HAND: FIST =
   ENTER`) until the next one, and row 7 shows the live hold (`HAND: FIST 1.5/3.0 S`).
   The legend has an amber HAND entry for the HG box.
6. Turn timer: our 120 s start when our turn starts (before the joker scan) and run
   without pausing until the turn is verified. With gesture input, when the
   opponent's 120 s run out our turn starts by itself. With keyboard input ENTER is
   still needed, because Arm DS console input blocks the CPU.

The card part of the network is **unchanged**. Both conv layers and the 54 card rows of
the FC ROM are byte-identical to before. Only 4 gesture rows are appended (ROM depth
27648 -> 29696). In card mode the classifier scores 0..53 exactly as before.

## Files changed (originals kept as `*.before_gesture`)

| File | Change |
|---|---|
| `DE10_Nano_D8M_DDR3_CNN/.../CNN.v` | `linear_classifier_fixed`: `class_lo`/`class_hi` inputs replace the fixed 0..53. `cnn_accelerator_top`: latches gesture mode at start |
| `.../cnn_debug_top.v` | ties `class_lo=0`, `class_hi=53` |
| `.../linear_weight_rom.v`, `_bb.v` | depth 29696, init file `weights_linear_gesture.mif` |
| `.../linear_bias.vh` | 4 gesture bias cases appended (card cases unchanged) |
| `.../weights_linear_gesture.mif` | **new**: original 54 card rows + 4 gesture rows |
| `Atlas-Blinking-LED-Baremetal-GNU/vision_core.h` | `cnn_run_mode(..., gesture, ...)`; `cnn_run` unchanged for cards |
| `Atlas-Blinking-LED-Baremetal-GNU/gesture_core.h` | **new**: zone crop, preprocessing, 3 s hold, key mapping |
| `Atlas-Blinking-LED-Baremetal-GNU/atlas_main.c` | `H` in the main menu turns gesture input on; prompts accept gestures |
| `Atlas-Blinking-LED-Baremetal-GNU/hdmi_text_v16_4.h` | with gesture input on, instruction rows say FIST / PEACE / PALM instead of ENTER / J / Q |

DR/HG swap (originals kept as `*.before_swap`): `fixed_board_grid.h` and
`fixed_board_grid.vh` (`GRID_DRAW_Y0` = top row), `board_core.h` (DRAW row now
follows the grid, `BOARD_DRAW_ROW`), `gesture_core.h` (zone below DRAW),
`fixed_grid_overlay.vh` (HG rectangle below DRAW). **Quartus recompile needed.**

Display layout (originals kept as `*.before_layout`):

| File | Change |
|---|---|
| `.../DE10_Nano_D8M_DDR3.v` | compact instruction/debug panel; camera thumbnail moved up (HDMI y 624 -> 564, constants `CAM_*` in `memory_display_v9`); turn timer `TIMER MM:SS` in the top-right corner (it used to sit under the camera thumbnail and was hidden) |
| `.../fixed_grid_overlay.vh` | legend moved under the 4x4 grid (two rows); new amber **HG** rectangle over the three free cells above DRAW (the gesture zone) |

Unchanged: `weights_linear.mif`, `layer1_weights.vh`, `layer2_biases.vh`, `weights_mac*.hex`,
`cnn_quant_config.vh`, `hps_cnn_bridge.v`, `soc_system.qsys`, `DE10_Nano_D8M_DDR3.v`.

## Build steps

**Quartus** (project `DE10_Nano_D8M_DDR3_CNN/DE10_Nano_D8M_DDR3_CNN/DE10_Nano_D8M_DDR3.qpf`)
1. Make sure `weights_linear_gesture.mif` is in the project folder (it already is).
2. Processing > Start Compilation. Platform Designer does **not** need regenerating.
3. In the Fitter report check **Total RAM Blocks**. It was 463 / 553. The FC ROM grows
   from 224 to an estimated 256 M10K, so expect about 495 / 553. If the Fitter runs out of
   memory, use Standard Fit and turn off physical synthesis.
4. Optional: open `linear_weight_rom` in the IP Parameter Editor. It should show 29696 words
   and `weights_linear_gesture.mif`. The file was edited directly, and its metadata matches.
5. Program the new `.sof`.

**Arm DS:** rebuild `Atlas-Blinking-LED-Baremetal-GNU` as usual (same Makefile) and load the AXF.
At the main menu type `H` + ENTER to switch to gesture input.

## Verification done (no hardware run yet)

- **Bit-exact RTL check** (`gesture_work/sim/run_sim.sh`, iverilog): 40 test images in both
  modes. The RTL class equals the Python integer model on every image. All 24 card-mode
  images also give the same class (and the same cycle count) as the **unmodified** `CNN.v`
  with the original ROM. The sim ROM is a behavioural model of the Quartus ROM with the
  same 2-cycle latency.
- **HPS preprocessing** (`gesture_work/check_hps_preprocess.py`): the C integer resize and
  stretch equal the Python training preprocessing exactly on 300 test zones.
- **HPS code** compiles with the board toolchain (`arm-eabi-gcc`, Makefile flags), with no
  new warnings.
- **Accuracy** (`gesture_export/report.txt`, integer model, phone photos):
  - held-out test photos: 25/26
  - train on one capture session, test on the other: 88% and 86%. The main confusion is
    palm vs peace.

  The 3 s / 80% hold means a gesture that is read correctly most of the time still
  produces the right command.

## Game simulation (before compiling)

`gesture_work/sim/game/` runs the real `atlas_main.c` on the PC (built with the
MinGW gcc that ships with Questa). The FPGA mailbox and display state, the turn
timer, the snapshot camera and the CNN are replaced by software models; the CNN is
the integer model with the Quartus weight files, which is bit-exact with `CNN.v`. A
simulated player reads the HDMI instructions, moves cards on a virtual board (real
card photos) and answers with hand gestures (real hand photos).

```
python gesture_work/sim/game/prep_assets.py      # weights + card/hand sprites
python gesture_work/sim/game/build_sim.py        # host build of atlas_main.c
cd gesture_work/sim/game/build && mkdir run && cp assets.bin run && cd run && ../atlas_sim.exe > console.log
python gesture_work/sim/game/render_game.py <run folder>   # HDMI frame per step
python gesture_work/sim/game/make_viewer.py <run folder>   # game_viewer.html
```

Result (run4): a complete game by gestures only. 16/16 cards were recognised and
verified, including one-card and two-card shifts. An opponent joker was detected and
removed, and one own joker was used. It took 329 s of simulated time.

## Capturing real board images (recommended)

The gesture rows were trained on phone photos. To train on the real D8M camera, type
**C** at the keyboard main menu, then F (fist), P (palm), V (peace) or N (nothing),
then ENTER. The HPS saves 100 frames of the HG zone as `hg_<label>_<session>_<n>.pgm`
files on the PC (Arm DS semihosting, in the debugger's working directory) and prints
what the CNN read for each frame. Move and tilt the hand a little during capture.
Two people, 2-3 captures each per gesture, plus a few empty-board (N) captures, is
enough to retrain. Semihosting file writes are untested on this board: if it prints
"cannot create files", tell me and we will use another path.

The model is trained for the hand coming from the bottom **or** the top of HG,
tilted up to 50 deg (`orientation_experiment.py`: a bottom-only model read 19-56%
correct for a hand from the top, this one 70-88%).

## Limitations / next steps

- The training photos are phone photos on grey card. The board camera image will differ, so
  **on-board accuracy is not measured yet.** The most valuable next step is to capture
  50-100 zone images per gesture with the board camera (dump `gesture_crop` after
  `gesture_resize_zone()`), add them to `gesture_raw/`, and rerun the two scripts below.
- The conv layers were trained for cards and are frozen (that is what keeps card mode
  unchanged). If gestures are still not reliable enough, the next step is joint retraining
  with the card dataset. That changes the card weights and needs a full card re-test.


## Change log (latest round)

| File | Change | Needs |
|---|---|---|
| `DE10_Nano_D8M_DDR3.v` | `CNN_status[7]` = **SW0** (spare bit of `cnn_status_pio`, read by the HPS at 0xFF200020) | Quartus |
| `DE10_Nano_D8M_DDR3.v` | status row now `MY JKR  OP JKR  LEFT  ROUND`; the score value carries own + 3*opponent jokers | Quartus |
| `fixed_grid_overlay.vh` | legend entry **HAND** (amber) | Quartus |
| `weights_linear_gesture.mif`, `linear_bias.vh` | gesture rows retrained (arm from the bottom, tilt +-35 deg) | Quartus |
| `gesture_core.h` | input mode: **H** key or SW0 flip up = gestures, SW0 flip down or PALM at the menu = keyboard; 3 s count restarts when the leading gesture changes; row 6 keeps `LAST HAND: ...`; capture mode (C); `GESTURE_FROM_TOP` switch (0 = arm from the bottom) | Arm DS |
| `hdmi_text_v16_4.h` | event log uses row 5 only in gesture mode; instruction rows can be redrawn when the input mode changes | Arm DS |
| `atlas_main.c` | our 120 s start at the start of our turn (before the joker scan); opponent's 120 s hand over automatically in gesture mode; **DRAW is scanned automatically** after "draw a card" (reminder until a card is there, auto retry); **deck colour rule** for the drawn card (e.g. 2S read as 2C still gives 2S); opponent jokers counted (2 at start), joker scan skipped when the opponent has none left | Arm DS |
| `game_config.h` | `GAME_OPP_JOKERS 2u` | Arm DS |
| `game_core.h` | `game_opp_jokers_left` | Arm DS |
| `capture_ip/hps_snapshot.v` **and** `soc_system/synthesis/submodules/hps_snapshot.v` (identical) | snapshot IP V2 (version 0x00020000): **window mode** (CSR 10..15: x0, y0, w, h, enable, step) captures one rectangle of the 1920x1080 source at full or half resolution; normal 480x270 snapshots unchanged; ports unchanged, so no Platform Designer regeneration. Checked in iverilog (`gesture_work/sim/tb_snapshot_window.v`): normal, window, full-res window, normal again, all words correct with random DDR stalls | Quartus |
| `vision_core.h` | `snapshot_window()`; detects IP V2 at start-up and prints `high-resolution DRAW ON/OFF` | Arm DS |
| `board_core.h` | `board_hires_refine()` / `board_hires_crop()`: the DRAW card is re-cropped from a 2x-resolution window, each 64x64 pixel the average of 4x4 samples (like the training crops); falls back to the normal crop if the window fails | Arm DS |

Backups of the previous versions: `*.before_hires`, `*.before_window` (snapshot IP), `*.before_draw`, `*.before_switch`, `*.before_oppjoker`, `*.before_swap`, `*.before_layout`, `*.before_gesture`.

## Retraining

```
python gesture_work/prep_gesture_dataset.py     # crop + augment + split
python gesture_work/train_gesture_head.py       # train gesture rows, export to gesture_export/
```
Copy `gesture_export/weights_linear_gesture.mif` and `linear_bias_gesture.vh` (as
`linear_bias.vh`) into the Quartus project, then run `sh gesture_work/sim/run_sim.sh`
before recompiling.

## Reverting

Copy each `*.before_gesture` file back over its original name. Keep the original
`weights_linear.mif`.
