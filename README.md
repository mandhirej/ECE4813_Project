# Arm Based ECE4813-SoC: Suit Stack card game with hand-gesture control

ECE4813 Arm SoC Design mini-project (Monash), Milestone 3, on the **DE10-Nano**
(Cyclone V: dual-core Arm Cortex-A9 HPS + FPGA), **Terasic D8M camera** and **HDMI**.

A camera watches the card board. The FPGA runs the video path, the HDMI user
interface and a **CNN accelerator** that recognises the 54 playing-card classes and
4 hand-gesture classes. The HPS (bare-metal C) runs the game, crops the cards and
the hand-gesture zone, sends them to the CNN and drives the HDMI text.

## Folders

| Folder | What it is |
|---|---|
| [`DE10_Nano_D8M_DDR3_CNN/`](DE10_Nano_D8M_DDR3_CNN/) | Quartus project (FPGA): video pipeline, HDMI UI, CNN accelerator, Platform Designer system, snapshot/CNN bridge IP |
| [`Atlas-Blinking-LED-Baremetal-GNU/`](Atlas-Blinking-LED-Baremetal-GNU/) | HPS bare-metal C program (Arm DS project): game logic, card vision, gesture input |
| [`CNN_train_model/`](CNN_train_model/) | Python: card CNN training/export, gesture training, and the simulations used to verify the design |
| [`docs/`](docs/) | Documentation of the gesture feature, display layout and change history |

## Build and run

1. **FPGA**: open `DE10_Nano_D8M_DDR3_CNN/DE10_Nano_D8M_DDR3.qpf` in Quartus Prime 25.1 Lite,
   run **Start Compilation**, program `output_files/DE10_Nano_D8M_DDR3.sof`.
   Platform Designer does not need to be regenerated.
2. **HPS**: import `Atlas-Blinking-LED-Baremetal-GNU` into Arm Development Studio,
   **Clean** then **Build**, start the debug configuration (it loads `u-boot-spl.axf`
   and then `atlas_main.axf`).
3. At the Arm DS console: `G` new game, `A` camera/board check, `H` (or flip **SW0** up)
   hand gestures, `C` capture gesture images, `Q` stop.

## Playing with hand gestures

Hold the gesture for 3 s in the amber **HG** box (three cells below the DR slot),
arm coming in from the bottom edge, fingers up:

| Gesture | Acts as |
|---|---|
| Fist | ENTER (confirm, draw, check, start our turn) |
| Peace | J (use own joker) |
| Palm | Q (cancel / main menu / back to keyboard) |

See [`docs/GESTURE.md`](docs/GESTURE.md) for the full description.
