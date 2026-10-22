#!/usr/bin/env python3
"""
evaluate_camera_dataset.py

Evaluate the trained 54-class card CNN on real FPGA camera crops.

Default expected layout:
    scanning_method/
    ├── camera_dataset/
    │   └── train/
    │       ├── 10Clubs/
    │       │   ├── 10Clubs_fpga_0000.png
    │       │   └── ...
    │       ├── ADiamonds/
    │       └── ...
    └── final_dataset_64x64/
        └── train/
            ├── 10Clubs/
            ├── ADiamonds/
            └── ... all 54 original training classes

The IMPORTANT part is that prediction index -> class name is taken from the
ORIGINAL training dataset, not from camera_dataset. This prevents wrong class
mapping when camera_dataset currently contains only a subset of the 54 cards.

Example:
    python scanning_method/evaluate_camera_dataset.py

Or:
    python scanning_method/evaluate_camera_dataset.py \
        --camera-dir scanning_method/camera_dataset/train \
        --class-dir scanning_method/final_dataset_64x64/train \
        --model models/fpga_card_classifier_model3.pth
"""

import argparse
from pathlib import Path
from collections import defaultdict

import torch
import torch.nn as nn
from PIL import Image
from torchvision import datasets, transforms


# ---------------------------------------------------------------------
# Model architecture
# Must match the model used to create fpga_card_classifier_model3.pth:
#
#   1x64x64
#     -> Conv 1->8, 3x3, stride 2, padding 1
#     -> BN
#     -> ReLU
#     -> Conv 8->16, 3x3, stride 2, padding 1
#     -> BN
#     -> ReLU
#     -> Flatten (16*16*16 = 4096)
#     -> Linear 4096->54
# ---------------------------------------------------------------------
class CardCNN(nn.Module):
    def __init__(self, num_classes=54):
        super().__init__()

        # IMPORTANT:
        # These module names intentionally match the checkpoint keys:
        #   features.0.*
        #   features.1.*
        #   features.3.*
        #   features.4.*
        #   classifier.*
        self.features = nn.Sequential(
            nn.Conv2d(
                in_channels=1,
                out_channels=8,
                kernel_size=3,
                stride=2,
                padding=1,
                bias=True,
            ),                      # features.0

            nn.BatchNorm2d(8),     # features.1
            nn.ReLU(inplace=False),# features.2

            nn.Conv2d(
                in_channels=8,
                out_channels=16,
                kernel_size=3,
                stride=2,
                padding=1,
                bias=True,
            ),                      # features.3

            nn.BatchNorm2d(16),    # features.4
            nn.ReLU(inplace=False) # features.5
        )

        self.classifier = nn.Linear(
            16 * 16 * 16,
            num_classes,
            bias=True,
        )

    def forward(self, x):
        x = self.features(x)
        x = torch.flatten(x, 1)
        x = self.classifier(x)
        return x


def load_checkpoint(model, checkpoint_path, device):
    """
    Handles common checkpoint formats:
      torch.save(model.state_dict(), path)
      torch.save({"model_state_dict": model.state_dict()}, path)
      torch.save({"state_dict": model.state_dict()}, path)
    """
    checkpoint = torch.load(checkpoint_path, map_location=device)

    if isinstance(checkpoint, dict):
        if "model_state_dict" in checkpoint:
            state_dict = checkpoint["model_state_dict"]
        elif "state_dict" in checkpoint:
            state_dict = checkpoint["state_dict"]
        else:
            # Could already be a raw state_dict.
            state_dict = checkpoint
    else:
        raise RuntimeError(
            "Unsupported checkpoint format. Expected a PyTorch state_dict "
            "or a dictionary containing model_state_dict/state_dict."
        )

    # Remove DataParallel "module." prefix if present.
    cleaned = {}
    for key, value in state_dict.items():
        if key.startswith("module."):
            key = key[len("module."):]
        cleaned[key] = value

    try:
        model.load_state_dict(cleaned, strict=True)
    except RuntimeError as e:
        print("\nERROR: checkpoint does not match the CardCNN architecture in this file.")
        print("Original PyTorch error:")
        print(e)
        print(
            "\nIf your training script used different layer attribute names "
            "(for example features.0 instead of conv1), copy the exact model "
            "class from your training script into this evaluator."
        )
        raise

    return model


def find_images(camera_dir):
    valid_ext = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"}

    items = []

    if not camera_dir.exists():
        raise FileNotFoundError(f"Camera dataset folder does not exist: {camera_dir}")

    for class_dir in sorted(camera_dir.iterdir()):
        if not class_dir.is_dir():
            continue

        # Ignore preview/debug folders.
        if class_dir.name.startswith("_"):
            continue

        true_label = class_dir.name

        for image_path in sorted(class_dir.rglob("*")):
            if image_path.is_file() and image_path.suffix.lower() in valid_ext:
                # Also ignore any enlarged preview images if they accidentally exist.
                if "preview" in image_path.stem.lower():
                    continue
                items.append((image_path, true_label))

    return items


def main():
    parser = argparse.ArgumentParser(
        description="Evaluate real FPGA camera crops using the trained card CNN."
    )

    parser.add_argument(
        "--camera-dir",
        type=Path,
        default=Path("scanning_method/camera_dataset/train"),
        help="Folder containing camera crops grouped by true class.",
    )

    parser.add_argument(
        "--class-dir",
        type=Path,
        default=Path("scanning_method/final_dataset_64x64/train"),
        help=(
            "ORIGINAL ImageFolder training directory containing all 54 classes. "
            "Used only to recover the exact class-index mapping."
        ),
    )

    parser.add_argument(
        "--model",
        type=Path,
        default=Path("models/fpga_card_classifier_model3.pth"),
        help="Path to trained PyTorch checkpoint.",
    )

    parser.add_argument(
        "--device",
        choices=["auto", "cpu", "cuda"],
        default="auto",
    )

    args = parser.parse_args()

    # -------------------------------------------------------------
    # Device
    # -------------------------------------------------------------
    if args.device == "cuda":
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA was requested but is not available.")
        device = torch.device("cuda")
    elif args.device == "cpu":
        device = torch.device("cpu")
    else:
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    print("=" * 90)
    print("CAMERA DATASET CNN EVALUATION")
    print("=" * 90)
    print(f"Device       : {device}")
    print(f"Camera data  : {args.camera_dir}")
    print(f"Class source : {args.class_dir}")
    print(f"Model        : {args.model}")
    print()

    # -------------------------------------------------------------
    # Recover exact ImageFolder class mapping used during training.
    # -------------------------------------------------------------
    if not args.class_dir.exists():
        raise FileNotFoundError(
            f"Original training class directory not found: {args.class_dir}\n"
            "Set --class-dir to the original training folder that contains "
            "all 54 class subfolders."
        )

    mapping_dataset = datasets.ImageFolder(str(args.class_dir))
    class_names = mapping_dataset.classes
    class_to_idx = mapping_dataset.class_to_idx

    print(f"Classes found : {len(class_names)}")

    if len(class_names) != 54:
        print(
            f"WARNING: expected 54 training classes but found {len(class_names)}."
        )

    print("\nOriginal model class mapping:")
    for idx, name in enumerate(class_names):
        print(f"  {idx:2d} -> {name}")

    # -------------------------------------------------------------
    # Load model
    # -------------------------------------------------------------
    model = CardCNN(num_classes=len(class_names)).to(device)
    model = load_checkpoint(model, args.model, device)
    model.eval()

    # Exact basic inference preprocessing.
    # FPGA crops are already 64x64 grayscale, but these operations make the
    # script safe for any accidentally different-sized image.
    transform = transforms.Compose([
        transforms.Grayscale(num_output_channels=1),
        transforms.Resize((64, 64)),
        transforms.ToTensor(),
    ])

    items = find_images(args.camera_dir)

    if not items:
        raise RuntimeError(
            f"No images were found under class folders in {args.camera_dir}"
        )

    print()
    print("=" * 90)
    print(f"EVALUATING {len(items)} CAMERA IMAGE(S)")
    print("=" * 90)

    total = 0
    correct = 0

    per_class_total = defaultdict(int)
    per_class_correct = defaultdict(int)

    mistakes = []

    with torch.no_grad():
        for number, (image_path, true_label) in enumerate(items, start=1):

            if true_label not in class_to_idx:
                print(
                    f"\nSKIP: {image_path}\n"
                    f"      Folder label '{true_label}' does not exist in original "
                    f"training class mapping."
                )
                continue

            image = Image.open(image_path).convert("L")
            x = transform(image).unsqueeze(0).to(device)

            logits = model(x)

            pred_idx = int(torch.argmax(logits, dim=1).item())
            pred_label = class_names[pred_idx]

            # Confidence is useful for diagnosing uncertain camera images.
            probs = torch.softmax(logits, dim=1)
            confidence = float(probs[0, pred_idx].item()) * 100.0

            is_correct = pred_label == true_label

            total += 1
            per_class_total[true_label] += 1

            if is_correct:
                correct += 1
                per_class_correct[true_label] += 1
                result = "CORRECT"
            else:
                result = "WRONG"
                mistakes.append(
                    (image_path, true_label, pred_label, pred_idx, confidence)
                )

            print(
                f"[{number:4d}/{len(items):4d}] "
                f"{result:7s} | "
                f"true={true_label:12s} | "
                f"pred={pred_label:12s} "
                f"(idx={pred_idx:2d}) | "
                f"conf={confidence:6.2f}% | "
                f"{image_path}"
            )

    if total == 0:
        raise RuntimeError("No valid labeled images were evaluated.")

    accuracy = 100.0 * correct / total

    print()
    print("=" * 90)
    print("FINAL RESULT")
    print("=" * 90)
    print(f"Correct  : {correct}")
    print(f"Wrong    : {total - correct}")
    print(f"Total    : {total}")
    print(f"Accuracy : {accuracy:.2f}%")

    print()
    print("=" * 90)
    print("PER-CLASS ACCURACY")
    print("=" * 90)

    for label in sorted(per_class_total):
        c = per_class_correct[label]
        n = per_class_total[label]
        acc = 100.0 * c / n
        print(f"{label:15s}: {c:4d}/{n:4d} = {acc:6.2f}%")

    if mistakes:
        print()
        print("=" * 90)
        print(f"MISCLASSIFIED IMAGES ({len(mistakes)})")
        print("=" * 90)

        for image_path, true_label, pred_label, pred_idx, confidence in mistakes:
            print(
                f"true={true_label:12s} -> "
                f"pred={pred_label:12s} "
                f"(idx={pred_idx:2d}, conf={confidence:6.2f}%) | "
                f"{image_path}"
            )
    else:
        print()
        print("No misclassifications.")

    print()


if __name__ == "__main__":
    main()
