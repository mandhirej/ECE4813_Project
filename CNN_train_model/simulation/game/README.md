# game: PC simulator of the whole game

Compiles the **real** `atlas_main.c` (and its headers) for the PC. Only the hardware
is replaced by software models in `sim_hw.h`:

- FPGA mailbox and HDMI state (same protocol as the Verilog decoder), 120 s timer
- snapshot camera: a virtual dark board with real card photos and hand photos
- CNN: the integer model with the Quartus weight files (bit-exact with `CNN.v`)
- a simulated player that reads the HDMI instructions, moves cards and answers with
  keys or hand gestures

| File | Purpose |
|---|---|
| `prep_assets.py` | exports the CNN weights to C and builds card/hand sprites |
| `build_sim.py` | makes a host copy of `atlas_main.c` with the hardware redirected and compiles it (MinGW gcc) |
| `sim_hw.h` | hardware models and the simulated player |
| `render_game.py`, `make_viewer.py` | HDMI frame per step and an HTML step viewer |
| `gesture_variant_test.py` | gesture read rates per hand photo |

Typical run: `python prep_assets.py`, `python build_sim.py`, then run
`build/atlas_sim.exe` in a folder that contains `assets.bin`
(`SIM_KEYBOARD=1` for a keyboard-only player).
