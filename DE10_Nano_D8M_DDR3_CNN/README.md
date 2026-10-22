# DE10_Nano_D8M_DDR3_CNN: Quartus project (FPGA)

Top-level entity `DE10_Nano_D8M_DDR3` (Cyclone V 5CSEBA6U23I7, Quartus Prime 25.1 Lite).

## Build

Open `DE10_Nano_D8M_DDR3.qpf`, **Start Compilation**, then program
`output_files/DE10_Nano_D8M_DDR3.sof` (the `.sof` is a build output and is not in Git).
Do **not** regenerate Platform Designer and do not run "IP Upgrade": the generated
`soc_system/synthesis` files are in the repo and include an edited submodule (below).
Check the Fitter report: RAM blocks about 495 / 553, `linear_weight_rom` depth 29696.

## Main files

| File | Purpose |
|---|---|
| `DE10_Nano_D8M_DDR3.v` | top level: D8M camera, video frame buffer, HDMI, HPS mailbox decoder, `memory_display_v9` (camera thumbnail + overlay) and `suit_stack_ui_shader_v16` (480x270 UI, 4x4 per HDMI pixel); SW0 is wired to bit 7 of `cnn_status_pio` |
| `CNN.v` | CNN accelerator: layer 1 (3x3 conv, 8 filters, stride 2), layer 2 (3x3 conv, 16 filters, stride 2), serial FC with argmax over a **class range** (cards 0..53, gestures 54..57) |
| `fixed_grid_overlay.vh`, `fixed_board_grid.vh` | camera-preview overlay: 4x4 grid, DR slot (top of column 5), HG gesture box (three cells below DR), legend |
| `turn_timer_120s.v` | 120 s turn timer |
| `layer1_weights.vh`, `layer2_biases.vh`, `weights_mac0..7.hex`, `cnn_quant_config.vh` | conv weights (int8) and quantisation shifts |
| `weights_linear_gesture.mif`, `linear_bias.vh` | FC weights/biases: 54 card rows (unchanged) + 4 gesture rows; loaded by the `linear_weight_rom` IP |
| `weights_linear.mif` | original card-only FC weights (kept for reference) |
| `soc_system.qsys`, `soc_system/` | Platform Designer system (HPS, bridges, PIOs, snapshot and CNN upload IP) |
| `capture_ip/hps_snapshot.v` | snapshot IP: 480x270 frame to HPS DDR; **V2 window mode** (CSR 10..15) captures one rectangle at full/half resolution for the DRAW card. The same file is in `soc_system/synthesis/submodules/hps_snapshot.v`, which is what Quartus compiles: keep both identical |
| `cnn_upload_ip/hps_cnn_bridge.v` | HPS-to-CNN image upload bridge (gesture mode request on the bbox registers) |
| `cnn_debug_top.v` | stand-alone CNN debug top |
| `V/`, `ip/` | Terasic D8M/HDMI controllers and IP cores |

## HPS interface (lightweight bridge 0xFF200000)

| Address | Block |
|---|---|
| 0xFF200000 / 0x10 | game status / command mailbox (HDMI state, timer) |
| 0xFF200020 | `cnn_status_pio`, bit 7 = SW0 (input-mode switch) |
| 0xFF200100 | snapshot IP |
| 0xFF200200 | CNN upload bridge |
