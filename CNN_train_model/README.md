# CNN_train_model: training, export and verification (Python)

| Folder | What it is |
|---|---|
| [`card_model/`](card_model/) | the original 54-class playing-card CNN: dataset preparation, training, integer export to the Verilog weight files, debugging |
| [`gesture_model/`](gesture_model/) | the 4 hand-gesture classes (fist, palm, peace, nothing) added on top of the frozen card CNN, and their exported weights |
| [`simulation/`](simulation/) | iverilog testbenches and the PC game simulator used to verify the design before compiling |

Network (same in Python and in `CNN.v`): 64x64 grayscale input, conv 3x3 x8 stride 2,
conv 3x3 x16 stride 2, fully connected 4096 -> classes; int8 weights, 8-bit activations.

Requirements: Python 3.12, PyTorch, torchvision, OpenCV, NumPy, Pillow, scikit-learn;
iverilog for the simulations.

The gesture scripts and simulations expect the working-folder layout they were
developed in (the Quartus project at `../DE10_Nano_D8M_DDR3_CNN/DE10_Nano_D8M_DDR3_CNN`
relative to the script folder's parent, and the raw photos in `gesture_raw/`); adjust
the path constants at the top of each script if you run them from this repository.
