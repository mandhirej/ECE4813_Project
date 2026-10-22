# gesture_model: hand-gesture classes on the card CNN

The two conv layers and the 54 card rows are **frozen** (card behaviour unchanged);
only 4 extra FC rows are trained: 54 fist, 55 palm, 56 peace, 57 nothing.

| File | Purpose |
|---|---|
| `prep_gesture_dataset.py` | crops the hand from the photos into the HG-zone shape (90x174, wrist at the bottom), HPS-identical preprocessing (grey, area resize to 64x64, contrast stretch), augmentation (tilt +-35 deg, size, shift, mirror, dark board), split by capture session |
| `train_gesture_head.py` | computes the frozen integer card features, trains the 4 rows, quantises them and exports `weights_linear_gesture.mif` (card rows copied verbatim) and `linear_bias_gesture.vh`; cross-session accuracy report |
| `train_from_captures.py` | retrains with REAL board-camera images saved by capture mode (`C` in the Arm DS menu), with a held-out capture session |
| `orientation_experiment.py` | compares training variants (arm from bottom/top, tilt range) |
| `check_hps_preprocess.py` | proves the C preprocessing in `gesture_core.h` equals the Python one |
| `hdmi_mockup.py` | pixel mock-up of the HDMI layout from the Verilog font and coordinates |
| `export/` | current exported weights, bias include and `report.txt` (accuracy, confusion matrices) |

Retrain: `python prep_gesture_dataset.py` then `python train_gesture_head.py`; copy
`export/weights_linear_gesture.mif` and `export/linear_bias_gesture.vh` (as
`linear_bias.vh`) into the Quartus project and run `../simulation/run_sim.sh`.
