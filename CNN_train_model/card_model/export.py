import copy
import json
import math
import os
from pathlib import Path

import cv2
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
import torch.nn.functional as F
from sklearn.model_selection import train_test_split
from torch.utils.data import DataLoader
from torchvision import datasets, transforms
from tqdm import tqdm

# ============================================================
# PROJECT CONFIGURATION
# ============================================================
SEED = 42
NUM_CLASSES = 54
IMG_SIZE = 64

# ============================================================
# CALIBRATED 8-BIT ACTIVATION RANGES
# ============================================================
# Validation sweep result:
#   L1 real activation range 0..4  -> 8-bit q=0..255
#   L2 real activation range 0..8  -> 8-bit q=0..255
#
# This combination recovered ~98.84% validation accuracy while
# preserving better resolution than the larger 8/16 or 16/32
# ranges, which gave the same measured accuracy.
#
# Real-value interpretation:
#   input_real ~= q_input * 1 / 255
#   L1_real    ~= q_L1    * 4 / 255
#   L2_real    ~= q_L2    * 8 / 255
#
# Keep these powers of two so RTL requantization is only a shift.
INPUT_ACT_LOG2 = 0   # range = 1
L1_ACT_LOG2 = 2      # range = 4
L2_ACT_LOG2 = 3      # range = 8

INPUT_ACT_RANGE = 1 << INPUT_ACT_LOG2
L1_ACT_RANGE = 1 << L1_ACT_LOG2
L2_ACT_RANGE = 1 << L2_ACT_LOG2

BLACKFOAM_DIR = Path("scanning_method/augmented_dataset_blackfoam")
DATA_DIR = Path("scanning_method/final_dataset_64x64")
TRAIN_DIR = DATA_DIR / "train"
VAL_DIR = DATA_DIR / "val"
MODEL_PATH = Path("models/fpga_card_classifier_model3.pth")
EXPORT_DIR = Path("hex_export")
EXPORT_DIR.mkdir(parents=True, exist_ok=True)

# Camera / training preprocessing constants.
# The FPGA uses the same simple grayscale definition and threshold.
BRIGHT_THRESHOLD = 40

# ============================================================
# 1. EXACT PYTHON PREPROCESSING USED BY THE FPGA PATH
# ============================================================
def hardware_style_gray_bgr(img_bgr: np.ndarray) -> np.ndarray:
    """Match the RTL: gray = B/4 + G/2 + R/4, integer arithmetic."""
    b = img_bgr[:, :, 0].astype(np.uint16)
    g = img_bgr[:, :, 1].astype(np.uint16)
    r = img_bgr[:, :, 2].astype(np.uint16)
    gray = (r >> 2) + (g >> 1) + (b >> 2)
    return np.clip(gray, 0, 255).astype(np.uint8)


def hardware_style_crop(img_path: Path) -> np.ndarray | None:
    """Detect bright card on black foam using the same fixed threshold as RTL."""
    img = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
    if img is None:
        return None

    gray = hardware_style_gray_bgr(img)
    mask = gray > BRIGHT_THRESHOLD
    ys, xs = np.where(mask)
    if len(xs) == 0:
        return None

    x0, x1 = int(xs.min()), int(xs.max())
    y0, y1 = int(ys.min()), int(ys.max())
    crop = img[y0:y1 + 1, x0:x1 + 1]

    # The current FPGA streaming scaler is intended for downsampling.
    # Reject tiny detections rather than silently producing a malformed 64x64 tensor.
    if crop.shape[1] < IMG_SIZE or crop.shape[0] < IMG_SIZE:
        return None

    return crop


def prepare_blackfoam_dataset() -> None:
    """Append black-foam samples directly into the dataset used by training."""
    if not BLACKFOAM_DIR.exists():
        print(f"[INFO] {BLACKFOAM_DIR} does not exist; skipping append step.")
        return

    classes = sorted(d.name for d in BLACKFOAM_DIR.iterdir() if d.is_dir())
    total = 0

    for cls in classes:
        src_dir = BLACKFOAM_DIR / cls
        files = sorted(p for p in src_dir.iterdir() if p.is_file())
        if len(files) < 2:
            print(f"[{cls}] skipped: need at least 2 images for train/val split")
            continue

        train_files, val_files = train_test_split(
            files, test_size=0.20, random_state=SEED
        )

        for split_name, split_files in (("train", train_files), ("val", val_files)):
            out_dir = DATA_DIR / split_name / cls
            out_dir.mkdir(parents=True, exist_ok=True)

            for src in split_files:
                crop = hardware_style_crop(src)
                if crop is None:
                    print(f"[{cls}] rejected: {src.name}")
                    continue

                # Convert using the EXACT FPGA grayscale equation before resizing.
                gray_crop = hardware_style_gray_bgr(crop)
                resized = cv2.resize(gray_crop, (IMG_SIZE, IMG_SIZE), interpolation=cv2.INTER_AREA)
                # Save as single-channel grayscale so the training tensor is exactly the same
                # 8-bit grayscale representation that the FPGA receives.
                dst = out_dir / f"blackfoam_{src.stem}.png"
                cv2.imwrite(str(dst), resized)
                total += 1

        print(f"[{cls}] appended train={len(train_files)} val={len(val_files)}")

    print(f"Total appended: {total}")


# ============================================================
# 2. TRAINING MODEL
# ============================================================
class HardwareOptimizedCNN_v3(nn.Module):
    """Training model. BatchNorm is kept here and folded at export time."""

    def __init__(self, num_classes: int = NUM_CLASSES):
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 8, kernel_size=3, stride=2, padding=1, bias=True),
            nn.BatchNorm2d(8),
            nn.ReLU(),
            nn.Conv2d(8, 16, kernel_size=3, stride=2, padding=1, bias=True),
            nn.BatchNorm2d(16),
            nn.ReLU(),
        )
        self.classifier = nn.Linear(16 * 16 * 16, num_classes)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.features(x)
        x = torch.flatten(x, 1)
        return self.classifier(x)


def make_loaders():
    transform = transforms.Compose([
        transforms.Resize((IMG_SIZE, IMG_SIZE)),
        transforms.Grayscale(num_output_channels=1),
        transforms.ToTensor(),  # exactly 0..255 -> 0..1
    ])

    train_data = datasets.ImageFolder(str(TRAIN_DIR), transform=transform)
    val_data = datasets.ImageFolder(str(VAL_DIR), transform=transform)

    if train_data.class_to_idx != val_data.class_to_idx:
        raise RuntimeError("Train/val class-to-index mappings differ.")

    if len(train_data.classes) != NUM_CLASSES:
        raise RuntimeError(
            f"Expected {NUM_CLASSES} classes, found {len(train_data.classes)}: {train_data.classes}"
        )

    train_loader = DataLoader(
        train_data, batch_size=32, shuffle=True, num_workers=0
    )
    val_loader = DataLoader(
        val_data, batch_size=32, shuffle=False, num_workers=0
    )
    return train_data, val_data, train_loader, val_loader


def train_model(epochs: int = 75) -> None:
    torch.manual_seed(SEED)
    np.random.seed(SEED)

    train_data, val_data, train_loader, val_loader = make_loaders()
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print("Device:", device)
    print("Classes:", train_data.classes)

    model = HardwareOptimizedCNN_v3(NUM_CLASSES).to(device)
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.Adam(model.parameters(), lr=1e-3)

    best_acc = -1.0
    best_state = copy.deepcopy(model.state_dict())

    for epoch in range(epochs):
        model.train()
        correct = 0
        total = 0
        train_bar = tqdm(train_loader, desc=f"Epoch {epoch + 1}/{epochs} [Train]")

        for images, labels in train_bar:
            images = images.to(device)
            labels = labels.to(device)

            optimizer.zero_grad()
            logits = model(images)
            loss = criterion(logits, labels)
            loss.backward()
            optimizer.step()

            pred = logits.argmax(dim=1)
            correct += int((pred == labels).sum().item())
            total += labels.numel()
            train_bar.set_postfix(loss=f"{loss.item():.4f}")

        train_acc = correct / max(total, 1)

        model.eval()
        correct = 0
        total = 0
        with torch.no_grad():
            for images, labels in val_loader:
                images = images.to(device)
                labels = labels.to(device)
                logits = model(images)
                pred = logits.argmax(dim=1)
                correct += int((pred == labels).sum().item())
                total += labels.numel()

        val_acc = correct / max(total, 1)
        print(f"Train Acc: {train_acc:.4f} | Val Acc: {val_acc:.4f}")

        if val_acc > best_acc:
            best_acc = val_acc
            best_state = copy.deepcopy(model.state_dict())

    model.load_state_dict(best_state)
    MODEL_PATH.parent.mkdir(parents=True, exist_ok=True)
    torch.save(model.state_dict(), MODEL_PATH)

    # Save the exact class mapping used by ImageFolder.
    with open(EXPORT_DIR / "classes.json", "w", encoding="utf-8") as f:
        json.dump(train_data.class_to_idx, f, indent=2, sort_keys=True)

    print(f"Best validation accuracy: {best_acc:.4f}")
    print(f"Model saved to: {MODEL_PATH}")


# ============================================================
# 3. BATCHNORM FOLDING
# ============================================================
def fold_conv_bn(conv: nn.Conv2d, bn: nn.BatchNorm2d):
    """Return folded Conv2d weights/bias exactly matching eval-mode Conv+BN."""
    if conv.bias is None:
        conv_bias = torch.zeros_like(bn.running_mean)
    else:
        conv_bias = conv.bias

    inv_std = torch.rsqrt(bn.running_var + bn.eps)
    scale = bn.weight * inv_std
    folded_weight = conv.weight * scale.reshape(-1, 1, 1, 1)
    folded_bias = bn.bias + (conv_bias - bn.running_mean) * scale
    return folded_weight.detach().cpu(), folded_bias.detach().cpu()


# ============================================================
# 4. INTEGER QUANTIZATION FORMAT
# ============================================================
# Input activation:
#   real_input ~= q_input * 1 / 255
#
# Layer-1 activation:
#   real_L1 ~= q_L1 * L1_ACT_RANGE / 255
#
# Layer-2 activation:
#   real_L2 ~= q_L2 * L2_ACT_RANGE / 255
#
# Weight convention for a layer with weight_shift=N:
#   real_weight ~= q_weight / 2^N
#
# If a layer input uses activation range A_in=2^a_in and its
# output uses A_out=2^a_out, then the integer accumulator is
# requantized using:
#
#   RTL_SHIFT = weight_shift + a_out - a_in
#
# and the bias in accumulator units is:
#
#   q_bias = round(real_bias * 255 * 2^weight_shift / A_in)
#
# This keeps all feature-map memories at 8 bits while avoiding
# the severe clipping caused by forcing every ReLU into 0..1.
# ============================================================
def choose_weight_shift(weight: torch.Tensor) -> int:
    max_abs = float(weight.abs().max().item())
    if max_abs == 0.0:
        return 0

    # Largest N in [0,7] that keeps the quantized weight within signed int8.
    n = int(math.floor(math.log2(127.0 / max_abs)))
    n = max(0, min(7, n))
    q = torch.round(weight * (2 ** n))
    if float(q.abs().max().item()) > 127:
        raise RuntimeError("Internal quantization range calculation failed")
    return n


def quantize_weights(weight: torch.Tensor, shift: int) -> torch.Tensor:
    return torch.round(weight * (2 ** shift)).clamp(-128, 127).to(torch.int64)


def rtl_output_shift(weight_shift: int, input_act_log2: int, output_act_log2: int) -> int:
    """Power-of-two requantization shift used by the convolution RTL."""
    shift = int(weight_shift + output_act_log2 - input_act_log2)
    if shift < 0:
        raise RuntimeError(
            f"Negative RTL shift {shift}: weight_shift={weight_shift}, "
            f"input_act_log2={input_act_log2}, output_act_log2={output_act_log2}"
        )
    return shift


def qbias_conv(
    bias: torch.Tensor,
    weight_shift: int,
    input_act_log2: int = INPUT_ACT_LOG2,
) -> torch.Tensor:
    """Quantize Conv bias into the integer accumulator's scale."""
    input_range = 2 ** input_act_log2
    scale = 255.0 * (2 ** weight_shift) / input_range
    return torch.round(bias * scale).to(torch.int64)


def qbias_fc(
    bias: torch.Tensor,
    weight_shift: int,
    input_act_log2: int = L2_ACT_LOG2,
) -> torch.Tensor:
    """Quantize FC bias to match the Layer-2 activation/weight product scale."""
    input_range = 2 ** input_act_log2
    scale = 255.0 * (2 ** weight_shift) / input_range
    return torch.round(bias * scale).to(torch.int64)

def to_hex(value: int, bits: int) -> str:
    mask = (1 << bits) - 1
    return f"{(int(value) & mask):0{bits // 4}X}"


def write_verilog_weight_block(out, prefix: str, w: torch.Tensor, b: torch.Tensor, bias_bits: int):
    out_channels = w.shape[0]
    in_channels = w.shape[1]
    assert w.shape[2:] == (3, 3)

    for oc in range(out_channels):
        out.write(f"// Filter {oc}\n")
        for y in range(3):
            for x in range(3):
                v = int(w[oc, 0, y, x].item())
                out.write(
                    f"wire signed [7:0] {prefix}{oc}_w{y+1}{x+1} = 8'h{to_hex(v, 8)};\n"
                )
        out.write(
            f"wire signed [{bias_bits-1}:0] {prefix}{oc}_bias = {bias_bits}'h{to_hex(int(b[oc].item()), bias_bits)};\n\n"
        )


# ============================================================
# 5. EXPORT EXACT TRAINED NETWORK
# ============================================================
def export_model() -> None:
    if not MODEL_PATH.exists():
        raise FileNotFoundError(
            f"{MODEL_PATH} not found. Train the model first or provide the exact checkpoint."
        )

    # Load the SAME architecture used for training.
    model = HardwareOptimizedCNN_v3(NUM_CLASSES)
    state = torch.load(MODEL_PATH, map_location="cpu")
    model.load_state_dict(state)
    model.eval()

    conv1 = model.features[0]
    bn1 = model.features[1]
    conv2 = model.features[3]
    bn2 = model.features[4]
    fc = model.classifier

    # Fold BN into the two convolution layers.
    w1_float, b1_float = fold_conv_bn(conv1, bn1)
    w2_float, b2_float = fold_conv_bn(conv2, bn2)

    # Choose one power-of-two WEIGHT scale per layer.
    weight_shift1 = choose_weight_shift(w1_float)
    weight_shift2 = choose_weight_shift(w2_float)
    weight_shift_fc = choose_weight_shift(fc.weight.detach().cpu())

    # The convolution RTL shifts are NOT identical to the weight shifts.
    # They also account for the calibrated activation range at each boundary.
    rtl_shift1 = rtl_output_shift(
        weight_shift1, INPUT_ACT_LOG2, L1_ACT_LOG2
    )
    rtl_shift2 = rtl_output_shift(
        weight_shift2, L1_ACT_LOG2, L2_ACT_LOG2
    )

    w1_q = quantize_weights(w1_float, weight_shift1)
    w2_q = quantize_weights(w2_float, weight_shift2)
    wfc_q = quantize_weights(fc.weight.detach().cpu(), weight_shift_fc)

    # Biases must use the scale of the layer INPUT activation.
    b1_q = qbias_conv(b1_float, weight_shift1, INPUT_ACT_LOG2)
    b2_q = qbias_conv(b2_float, weight_shift2, L1_ACT_LOG2)
    bfc_q = qbias_fc(fc.bias.detach().cpu(), weight_shift_fc, L2_ACT_LOG2)

    # Sanity checks for the planned RTL widths.
    for name, tensor, bits in (
        ("Layer1 bias", b1_q, 32),
        ("Layer2 bias", b2_q, 32),
        ("FC bias", bfc_q, 32),
    ):
        limit = 2 ** (bits - 1) - 1
        if int(tensor.abs().max().item()) > limit:
            raise RuntimeError(f"{name} does not fit signed {bits}-bit: max={int(tensor.abs().max())}")

    # --------------------------------------------------------
    # Quantization configuration consumed by Verilog.
    # --------------------------------------------------------
    with open(EXPORT_DIR / "cnn_quant_config.vh", "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED. Do not edit manually.\n")
        f.write("// Calibrated activation ranges: input=1, L1=4, L2=8.\n")
        f.write(f"localparam integer L1_SHIFT = {rtl_shift1};\n")
        f.write(f"localparam integer L2_SHIFT = {rtl_shift2};\n")
        f.write(f"localparam integer FC_SHIFT = {weight_shift_fc};\n")
        f.write(f"localparam integer L1_WEIGHT_SHIFT = {weight_shift1};\n")
        f.write(f"localparam integer L2_WEIGHT_SHIFT = {weight_shift2};\n")
        f.write(f"localparam integer FC_WEIGHT_SHIFT = {weight_shift_fc};\n")
        f.write(f"localparam integer L1_ACT_LOG2 = {L1_ACT_LOG2};\n")
        f.write(f"localparam integer L2_ACT_LOG2 = {L2_ACT_LOG2};\n")
        f.write("// real input ~= q/255; real L1 ~= q*4/255; real L2 ~= q*8/255\n")

    # --------------------------------------------------------
    # Layer 1 Verilog weights/biases.
    # --------------------------------------------------------
    with open(EXPORT_DIR / "layer1_weights.vh", "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED FROM TRAINED MODEL AFTER BN FOLDING\n")
        write_verilog_weight_block(f, "f", w1_q, b1_q, 32)

    # --------------------------------------------------------
    # Layer 2 ROMs: 2 passes × 9 spatial positions.
    # --------------------------------------------------------
    for mac in range(8):
        with open(EXPORT_DIR / f"weights_mac{mac}.hex", "w", encoding="utf-8") as f:
            for pass_num in range(2):
                out_ch = mac + 8 * pass_num
                for y in range(3):
                    for x in range(3):
                        # {ch7,ch6,...,ch0}; ch0 is the LSB byte in RTL.
                        line = "".join(
                            to_hex(int(w2_q[out_ch, in_ch, y, x].item()), 8)
                            for in_ch in reversed(range(8))
                        )
                        f.write(line + "\n")

    with open(EXPORT_DIR / "layer2_biases.vh", "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED FROM TRAINED MODEL AFTER BN FOLDING\n")
        for mac in range(8):
            f.write(f"wire signed [31:0] l2_m{mac}_b0 = 32'h{to_hex(int(b2_q[mac].item()), 32)};\n")
            f.write(f"wire signed [31:0] l2_m{mac}_b1 = 32'h{to_hex(int(b2_q[mac + 8].item()), 32)};\n")

    # --------------------------------------------------------
    # FC ROM.
    # Address = class * 512 + pass*256 + y*16 + x.
    # Each word contains 8 channels with channel 0 in the LSB byte.
    # --------------------------------------------------------
    wfc_3d = wfc_q.view(NUM_CLASSES, 16, 16, 16)
    with open(EXPORT_DIR / "weights_linear.mif", "w", encoding="utf-8") as f:
        depth = NUM_CLASSES * 512
        f.write("WIDTH=64;\n")
        f.write(f"DEPTH={depth};\n")
        f.write("ADDRESS_RADIX=UNS;\n")
        f.write("DATA_RADIX=HEX;\n")
        f.write("CONTENT BEGIN\n")

        address = 0
        for class_idx in range(NUM_CLASSES):
            for pong_addr in range(512):
                pass_num = (pong_addr >> 8) & 0x1
                y = (pong_addr >> 4) & 0xF
                x = pong_addr & 0xF
                base_ch = pass_num * 8
                hex_str = "".join(
                    to_hex(int(wfc_3d[class_idx, base_ch + ch, y, x].item()), 8)
                    for ch in reversed(range(8))
                )
                f.write(f"\t{address} : {hex_str};\n")
                address += 1
        f.write("END;\n")

    # --------------------------------------------------------
    # FC bias case statement.
    # --------------------------------------------------------
    with open(EXPORT_DIR / "linear_bias.vh", "w", encoding="utf-8") as f:
        f.write("always @(*) begin\n")
        f.write("    case (active_class)\n")
        for c in range(NUM_CLASSES):
            f.write(
                f"        6'd{c}: current_bias = 32'h{to_hex(int(bfc_q[c].item()), 32)};\n"
            )
        f.write("        default: current_bias = 32'd0;\n")
        f.write("    endcase\n")
        f.write("end\n")

    print("Export complete.")
    print(f"Activation ranges: input={INPUT_ACT_RANGE}, L1={L1_ACT_RANGE}, L2={L2_ACT_RANGE}")
    print(
        f"L1 weight shift={weight_shift1}, RTL shift={rtl_shift1}"
    )
    print(
        f"L2 weight shift={weight_shift2}, RTL shift={rtl_shift2}"
    )
    print(
        f"FC weight shift={weight_shift_fc} "
        "(FC argmax does not need a final output shift)"
    )
    print(f"Output dir = {EXPORT_DIR}")


# ============================================================
# 6. OPTIONAL INTENTIONAL INTEGER REFERENCE CHECK
# ============================================================
def print_quantization_summary() -> None:
    if not MODEL_PATH.exists():
        print(f"{MODEL_PATH} does not exist; skip summary.")
        return

    model = HardwareOptimizedCNN_v3(NUM_CLASSES)
    model.load_state_dict(torch.load(MODEL_PATH, map_location="cpu"))
    model.eval()

    w1, b1 = fold_conv_bn(model.features[0], model.features[1])
    w2, b2 = fold_conv_bn(model.features[3], model.features[4])
    wf = model.classifier.weight.detach().cpu()
    bf = model.classifier.bias.detach().cpu()

    ws1 = choose_weight_shift(w1)
    ws2 = choose_weight_shift(w2)
    wsf = choose_weight_shift(wf)
    rs1 = rtl_output_shift(ws1, INPUT_ACT_LOG2, L1_ACT_LOG2)
    rs2 = rtl_output_shift(ws2, L1_ACT_LOG2, L2_ACT_LOG2)

    print("--- Quantization summary ---")
    print(
        f"Activation ranges: input={INPUT_ACT_RANGE}, "
        f"L1={L1_ACT_RANGE}, L2={L2_ACT_RANGE}"
    )
    for name, w, s in (
        ("L1", w1, ws1),
        ("L2", w2, ws2),
        ("FC", wf, wsf),
    ):
        print(
            f"{name}: max_abs={float(w.abs().max()):.6f}, "
            f"weight_shift={s}, "
            f"qmax={int(torch.round(w * (2 ** s)).abs().max())}"
        )

    print(f"L1 RTL shift: {rs1}")
    print(f"L2 RTL shift: {rs2}")
    print(
        "L1 bias max:",
        int(qbias_conv(b1, ws1, INPUT_ACT_LOG2).abs().max()),
    )
    print(
        "L2 bias max:",
        int(qbias_conv(b2, ws2, L1_ACT_LOG2).abs().max()),
    )
    print(
        "FC bias max:",
        int(qbias_fc(bf, wsf, L2_ACT_LOG2).abs().max()),
    )

def verify_quantized_accuracy() -> None:
    """Run an integer-equivalent reference using the exact exported arithmetic."""
    if not MODEL_PATH.exists():
        raise FileNotFoundError(f"{MODEL_PATH} not found")

    model = HardwareOptimizedCNN_v3(NUM_CLASSES)
    model.load_state_dict(torch.load(MODEL_PATH, map_location="cpu"))
    model.eval()

    w1_f, b1_f = fold_conv_bn(model.features[0], model.features[1])
    w2_f, b2_f = fold_conv_bn(model.features[3], model.features[4])
    wf_f = model.classifier.weight.detach().cpu()
    bf_f = model.classifier.bias.detach().cpu()

    ws1 = choose_weight_shift(w1_f)
    ws2 = choose_weight_shift(w2_f)
    wsf = choose_weight_shift(wf_f)

    rs1 = rtl_output_shift(ws1, INPUT_ACT_LOG2, L1_ACT_LOG2)
    rs2 = rtl_output_shift(ws2, L1_ACT_LOG2, L2_ACT_LOG2)

    w1 = quantize_weights(w1_f, ws1).double()
    w2 = quantize_weights(w2_f, ws2).double()
    wf = quantize_weights(wf_f, wsf).double()

    b1 = qbias_conv(b1_f, ws1, INPUT_ACT_LOG2).double()
    b2 = qbias_conv(b2_f, ws2, L1_ACT_LOG2).double()
    bf = qbias_fc(bf_f, wsf, L2_ACT_LOG2).double()

    transform = transforms.Compose([
        transforms.Resize((IMG_SIZE, IMG_SIZE)),
        transforms.Grayscale(num_output_channels=1),
        transforms.ToTensor(),
    ])
    val_data = datasets.ImageFolder(str(VAL_DIR), transform=transform)
    val_loader = DataLoader(
        val_data, batch_size=32, shuffle=False, num_workers=0
    )

    correct = 0
    total = 0
    sat1 = 0
    sat2 = 0
    count1 = 0
    count2 = 0

    with torch.no_grad():
        for images, labels in val_loader:
            # Input: q=0..255 represents real 0..1.
            x = torch.round(images.double() * 255.0)

            acc1 = F.conv2d(x, w1, b1, stride=2, padding=1)
            raw_q1 = torch.floor(torch.clamp(acc1, min=0) / (2 ** rs1))
            sat1 += int((raw_q1 > 255).sum().item())
            count1 += raw_q1.numel()
            q1 = raw_q1.clamp(0, 255)

            acc2 = F.conv2d(q1, w2, b2, stride=2, padding=1)
            raw_q2 = torch.floor(torch.clamp(acc2, min=0) / (2 ** rs2))
            sat2 += int((raw_q2 > 255).sum().item())
            count2 += raw_q2.numel()
            q2 = raw_q2.clamp(0, 255)

            # q2 represents real activation with range L2_ACT_RANGE.
            # FC bias was quantized to the same accumulator scale, so no
            # final shift is needed for argmax.
            flat = q2.flatten(1)
            scores = flat @ wf.t() + bf
            pred = scores.argmax(dim=1)

            correct += int((pred == labels).sum().item())
            total += labels.numel()

    print(
        f"Integer-reference validation accuracy: "
        f"{correct / max(total, 1):.4f}"
    )
    print(
        f"L1 saturation: {100.0 * sat1 / max(count1, 1):.5f}%"
    )
    print(
        f"L2 saturation: {100.0 * sat2 / max(count2, 1):.5f}%"
    )



if __name__ == "__main__":
    # Uncomment exactly the stages you want to run.
    # prepare_blackfoam_dataset()
    # train_model(epochs=75)
    export_model()
    print_quantization_summary()
    verify_quantized_accuracy()
