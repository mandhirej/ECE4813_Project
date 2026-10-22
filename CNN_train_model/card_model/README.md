# card_model: 54-class playing-card CNN

| File | Purpose |
|---|---|
| `train3.py` | trains `HardwareOptimizedCNN_v3` (64x64 grey, 2 conv layers + FC, 54 classes) |
| `export.py` | folds BatchNorm, quantises to int8 with power-of-two shifts and writes `cnn_quant_config.vh`, `layer1_weights.vh`, `weights_mac0..7.hex`, `layer2_biases.vh`, `weights_linear.mif`, `linear_bias.vh`; also an integer reference check |
| `validation3.py`, `evaluate_camera_dataset.py` | accuracy on the validation set / on camera captures |
| `augment.py`, `addmore.py`, `cropper.py`, `resize.py`, `move.py`, `checkOrder.py` | dataset preparation (crop the card, resize to 64x64, augment, split) |
| `cnn_debug_golden.py`, `debug.py`, `signal_tap_to_dataset.py` | golden-model and SignalTap debugging of the FPGA CNN |
| `run1_fpga_crop_64x64.png`, `run2_fpga_crop_64x64.png` | example 64x64 crops captured from the FPGA |

Class order is the sorted dataset folder names (`10Clubs`, `10Diamonds`, ... `QSpade`):
classes 0..53. Gesture classes 54..57 are added by `../gesture_model`.
