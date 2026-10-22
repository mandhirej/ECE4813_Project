# simulation: verification before compiling

| File | What it checks |
|---|---|
| `run_sim.sh`, `make_vectors.py`, `tb_cnn_gesture.v` | **bit-exact CNN check**: iverilog simulation of `CNN.v` vs the Python integer model on 40 images (cards and gestures, both modes); card mode also compared with the original, unmodified RTL |
| `tb_hdmi_frame.v`, `make_hdmi_inputs.py` | renders a full 1920x1080 HDMI frame from `memory_display_v9` (UI shader + camera thumbnail + overlay) |
| `tb_snapshot_window.v` | snapshot IP V2: normal capture, half- and full-resolution window capture, normal again; every word checked with random DDR stalls |
| [`game/`](game/) | PC game simulator: the real `atlas_main.c` with software models of the FPGA, camera and CNN |

Run `sh run_sim.sh` (needs iverilog and Python).
