# diagnose_quantization.py

from pathlib import Path
import sys

import torch
import torch.nn.functional as F
from torchvision import datasets, transforms
from torch.utils.data import DataLoader

# ------------------------------------------------------------
# Your renamed exporter
# ------------------------------------------------------------
PROJECT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(PROJECT_DIR / "scanning_dataset"))

import export as fx


DEVICE = torch.device(
    "cuda" if torch.cuda.is_available() else "cpu"
)


def percentile(tensor, p):
    x = tensor.detach().flatten().float().cpu()

    if x.numel() == 0:
        return 0.0

    return float(
        torch.quantile(x, torch.tensor(p)).item()
    )


def main():

    print("Loading model...")

    model = fx.HardwareOptimizedCNN_v3(
        fx.NUM_CLASSES
    )

    model.load_state_dict(
        torch.load(
            fx.MODEL_PATH,
            map_location="cpu"
        )
    )

    model.eval()

    # --------------------------------------------------------
    # Fold BN
    # --------------------------------------------------------
    w1_f, b1_f = fx.fold_conv_bn(
        model.features[0],
        model.features[1]
    )

    w2_f, b2_f = fx.fold_conv_bn(
        model.features[3],
        model.features[4]
    )

    wf_f = model.classifier.weight.detach().cpu()
    bf_f = model.classifier.bias.detach().cpu()

    # --------------------------------------------------------
    # Current quantization
    # --------------------------------------------------------
    s1 = fx.choose_weight_shift(w1_f)
    s2 = fx.choose_weight_shift(w2_f)
    sf = fx.choose_weight_shift(wf_f)

    w1_q = fx.quantize_weights(
        w1_f, s1
    ).double()

    w2_q = fx.quantize_weights(
        w2_f, s2
    ).double()

    wf_q = fx.quantize_weights(
        wf_f, sf
    ).double()

    b1_q = fx.qbias_conv(
        b1_f, s1
    ).double()

    b2_q = fx.qbias_conv(
        b2_f, s2
    ).double()

    bf_q = fx.qbias_fc(
        bf_f, sf
    ).double()

    print()
    print("Weight shifts:")
    print("L1 =", s1)
    print("L2 =", s2)
    print("FC =", sf)

    
    # --------------------------------------------------------
    # Dataset
    # --------------------------------------------------------
    transform = transforms.Compose([
        transforms.Resize(
            (fx.IMG_SIZE, fx.IMG_SIZE)
        ),

        transforms.Grayscale(
            num_output_channels=1
        ),

        transforms.ToTensor(),
    ])

    val_data = datasets.ImageFolder(
        str(fx.VAL_DIR),
        transform=transform
    )

    loader = DataLoader(
        val_data,
        batch_size=64,
        shuffle=False,
        num_workers=0
    )

    print()
    print("Number validation images:",
          len(val_data))

    # ============================================================
    # ACTIVATION RANGE SWEEP
    # ============================================================

    L1_RANGE_POWERS = [1, 2, 3, 4]
    # means:
    # 2, 4, 8, 16

    L2_RANGE_POWERS = [2, 3, 4, 5]
    # means:
    # 4, 8, 16, 32


    print()
    print("=" * 75)
    print("ACTIVATION RANGE SWEEP")
    print("=" * 75)


    for a1_log2 in L1_RANGE_POWERS:

        A1 = 2 ** a1_log2

        for a2_log2 in L2_RANGE_POWERS:

            A2 = 2 ** a2_log2

            # ----------------------------------------------------
            # Output right shifts
            # ----------------------------------------------------

            rtl_shift1 = (
                s1
                + a1_log2
            )

            rtl_shift2 = (
                s2
                + a2_log2
                - a1_log2
            )

            # ----------------------------------------------------
            # Bias quantization
            #
            # bias scale =
            # 255 * weight_scale / input_activation_range
            # ----------------------------------------------------

            b1_test = torch.round(
                b1_f.double()
                * 255.0
                * (2 ** s1)
            )

            b2_test = torch.round(
                b2_f.double()
                * 255.0
                * (2 ** s2)
                / A1
            )

            bf_test = torch.round(
                bf_f.double()
                * 255.0
                * (2 ** sf)
                / A2
            )

            correct = 0
            total_test = 0

            sat1 = 0
            sat2 = 0

            count1_test = 0
            count2_test = 0


            with torch.no_grad():

                for images, labels in loader:

                    # --------------------------------------------
                    # Input range = 0...1
                    # --------------------------------------------

                    xq = torch.round(
                        images.double() * 255.0
                    )

                    # --------------------------------------------
                    # Layer 1
                    # --------------------------------------------

                    acc1 = F.conv2d(
                        xq,
                        w1_q,
                        b1_test,
                        stride=2,
                        padding=1
                    )

                    raw_q1 = torch.floor(
                        torch.clamp(acc1, min=0)
                        / (2 ** rtl_shift1)
                    )

                    sat1 += (
                        raw_q1 > 255
                    ).sum().item()

                    count1_test += raw_q1.numel()

                    q1 = raw_q1.clamp(
                        0, 255
                    )

                    # --------------------------------------------
                    # Layer 2
                    # --------------------------------------------

                    acc2 = F.conv2d(
                        q1,
                        w2_q,
                        b2_test,
                        stride=2,
                        padding=1
                    )

                    raw_q2 = torch.floor(
                        torch.clamp(acc2, min=0)
                        / (2 ** rtl_shift2)
                    )

                    sat2 += (
                        raw_q2 > 255
                    ).sum().item()

                    count2_test += raw_q2.numel()

                    q2 = raw_q2.clamp(
                        0, 255
                    )

                    # --------------------------------------------
                    # FC
                    # --------------------------------------------

                    scores = (
                        q2.flatten(1)
                        @ wf_q.t()
                        + bf_test
                    )

                    pred = scores.argmax(1)

                    correct += (
                        pred == labels
                    ).sum().item()

                    total_test += labels.numel()


            accuracy = (
                100.0 * correct / total_test
            )

            sat1_pct = (
                100.0 * sat1 / count1_test
            )

            sat2_pct = (
                100.0 * sat2 / count2_test
            )


            print(
                f"L1 range={A1:2d} "
                f"L2 range={A2:2d} | "
                f"shifts=({rtl_shift1},{rtl_shift2}) | "
                f"acc={accuracy:6.2f}% | "
                f"L1 sat={sat1_pct:8.5f}% | "
                f"L2 sat={sat2_pct:8.5f}%"
            )
            
    # --------------------------------------------------------
    # Accuracy counters
    # --------------------------------------------------------
    total = 0

    fp32_correct = 0
    folded_correct = 0
    integer_correct = 0

    all_a1 = []
    all_a2 = []

    clipped1 = 0
    clipped2 = 0

    count1 = 0
    count2 = 0

    with torch.no_grad():

        for images, labels in loader:

            # ====================================================
            # A. ORIGINAL FP32
            # ====================================================
            fp_scores = model(images)

            fp_pred = fp_scores.argmax(1)

            fp32_correct += (
                fp_pred == labels
            ).sum().item()

            # ====================================================
            # B. FOLDED FP32
            # ====================================================
            a1 = F.relu(
                F.conv2d(
                    images,
                    w1_f,
                    b1_f,
                    stride=2,
                    padding=1
                )
            )

            a2 = F.relu(
                F.conv2d(
                    a1,
                    w2_f,
                    b2_f,
                    stride=2,
                    padding=1
                )
            )

            folded_scores = (
                a2.flatten(1)
                @ wf_f.t()
                + bf_f
            )

            folded_pred = (
                folded_scores.argmax(1)
            )

            folded_correct += (
                folded_pred == labels
            ).sum().item()

            # Save activation statistics
            all_a1.append(a1.flatten().cpu())
            all_a2.append(a2.flatten().cpu())

            clipped1 += (
                a1 > 1.0
            ).sum().item()

            clipped2 += (
                a2 > 1.0
            ).sum().item()

            count1 += a1.numel()
            count2 += a2.numel()

            # ====================================================
            # C. CURRENT FPGA INTEGER MODEL
            # ====================================================

            # q / 255 ~= real input
            xq = torch.round(
                images.double() * 255.0
            )

            acc1 = F.conv2d(
                xq,
                w1_q,
                b1_q,
                stride=2,
                padding=1
            )

            q1 = torch.where(
                acc1 < 0,
                torch.zeros_like(acc1),
                torch.floor(
                    acc1 / (2 ** s1)
                )
            )

            q1 = q1.clamp(
                0, 255
            )

            acc2 = F.conv2d(
                q1,
                w2_q,
                b2_q,
                stride=2,
                padding=1
            )

            q2 = torch.where(
                acc2 < 0,
                torch.zeros_like(acc2),
                torch.floor(
                    acc2 / (2 ** s2)
                )
            )

            q2 = q2.clamp(
                0, 255
            )

            integer_scores = (
                q2.flatten(1)
                @ wf_q.t()
                + bf_q
            )

            integer_pred = (
                integer_scores.argmax(1)
            )

            integer_correct += (
                integer_pred == labels
            ).sum().item()

            total += labels.numel()

    # --------------------------------------------------------
    # Activation statistics
    # --------------------------------------------------------
    a1 = torch.cat(all_a1)
    a2 = torch.cat(all_a2)

    print()
    print("=" * 60)
    print("ACCURACY")
    print("=" * 60)

    print(
        f"Original FP32: "
        f"{fp32_correct / total:.4%}"
    )

    print(
        f"BN-folded FP32: "
        f"{folded_correct / total:.4%}"
    )

    print(
        f"Current FPGA integer: "
        f"{integer_correct / total:.4%}"
    )

    print()
    print("=" * 60)
    print("LAYER 1 FLOAT ACTIVATION")
    print("=" * 60)

    print(
        "max   =",
        float(a1.max())
    )

    print(
        "p99   =",
        percentile(a1, 0.99)
    )

    print(
        "p99.9 =",
        percentile(a1, 0.999)
    )

    print(
        "p99.99=",
        percentile(a1, 0.9999)
    )

    print(
        "% > 1.0 =",
        100.0 * clipped1 / count1
    )

    print()
    print("=" * 60)
    print("LAYER 2 FLOAT ACTIVATION")
    print("=" * 60)

    print(
        "max   =",
        float(a2.max())
    )

    print(
        "p99   =",
        percentile(a2, 0.99)
    )

    print(
        "p99.9 =",
        percentile(a2, 0.999)
    )

    print(
        "p99.99=",
        percentile(a2, 0.9999)
    )

    print(
        "% > 1.0 =",
        100.0 * clipped2 / count2
    )

def relu_requantize(acc, right_shift):
    """
    Match FPGA:
      negative -> 0
      arithmetic/power-of-two scaling
      saturate -> uint8
    """

    acc = torch.where(
        acc < 0,
        torch.zeros_like(acc),
        acc
    )

    if right_shift >= 0:
        q = torch.floor(
            acc / (2 ** right_shift)
        )
    else:
        q = acc * (2 ** (-right_shift))

    return q.clamp(0, 255)

if __name__ == "__main__":
    main()