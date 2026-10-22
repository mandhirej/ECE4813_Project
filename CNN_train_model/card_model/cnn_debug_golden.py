#!/usr/bin/env python3
"""
Generate bit-exact integer reference files for the resource-optimized FPGA CNN.
Run from the same project directory that contains fixed_train_export.py.
"""
import argparse
import sys
from pathlib import Path
import torch
import torch.nn.functional as F
from PIL import Image
from torchvision import transforms
PROJECT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(PROJECT_DIR / "scanning_dataset"))
import export as fx

def clamp_relu_shift(acc, shift):
    q = torch.where(acc < 0, torch.zeros_like(acc), torch.floor(acc / (2 ** shift)))
    return q.clamp(0, 255).to(torch.int64)

def pack8(vals):
    word = 0
    for ch, v in enumerate(vals):
        word |= (int(v) & 0xFF) << (8 * ch)
    return word

def make_window_2d(inp, out_y, out_x):
    c, h, w = inp.shape
    win = torch.zeros((c, 3, 3), dtype=torch.int64)
    for ky in range(3):
        for kx in range(3):
            sy = 2 * out_y + ky - 1
            sx = 2 * out_x + kx - 1
            if 0 <= sy < h and 0 <= sx < w:
                win[:, ky, kx] = inp[:, sy, sx]
    return win

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', required=True)
    ap.add_argument('--out-dir', default='debug_golden')
    args = ap.parse_args()
    image_path = Path(args.image)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    model = fx.HardwareOptimizedCNN_v3(fx.NUM_CLASSES)
    model.load_state_dict(torch.load(fx.MODEL_PATH, map_location='cpu'))
    model.eval()

    w1_f, b1_f = fx.fold_conv_bn(model.features[0], model.features[1])
    w2_f, b2_f = fx.fold_conv_bn(model.features[3], model.features[4])
    wf_f = model.classifier.weight.detach().cpu()
    bf_f = model.classifier.bias.detach().cpu()

    ws1 = fx.choose_weight_shift(w1_f)
    ws2 = fx.choose_weight_shift(w2_f)
    wsf = fx.choose_weight_shift(wf_f)

    rs1 = fx.rtl_output_shift(ws1, fx.INPUT_ACT_LOG2, fx.L1_ACT_LOG2)
    rs2 = fx.rtl_output_shift(ws2, fx.L1_ACT_LOG2, fx.L2_ACT_LOG2)

    w1 = fx.quantize_weights(w1_f, ws1).to(torch.int64)
    w2 = fx.quantize_weights(w2_f, ws2).to(torch.int64)
    wf = fx.quantize_weights(wf_f, wsf).to(torch.int64)

    b1 = fx.qbias_conv(b1_f, ws1, fx.INPUT_ACT_LOG2).to(torch.int64)
    b2 = fx.qbias_conv(b2_f, ws2, fx.L1_ACT_LOG2).to(torch.int64)
    bf = fx.qbias_fc(bf_f, wsf, fx.L2_ACT_LOG2).to(torch.int64)

    transform = transforms.Compose([
        transforms.Resize((fx.IMG_SIZE, fx.IMG_SIZE)),
        transforms.Grayscale(num_output_channels=1),
        transforms.ToTensor(),
    ])
    pil = Image.open(image_path).convert('RGB')
    image_tensor = transform(pil)
    x = torch.round(image_tensor.double() * 255.0).to(torch.int64)
    x_b = x.unsqueeze(0)

    acc1 = F.conv2d(x_b.double(), w1.double(), b1.double(), stride=2, padding=1)
    q1 = clamp_relu_shift(acc1, rs1)
    acc2 = F.conv2d(q1.double(), w2.double(), b2.double(), stride=2, padding=1)
    q2 = clamp_relu_shift(acc2, rs2)
    flat = q2.flatten(1).double()
    scores = flat @ wf.double().t() + bf.double()
    scores_i = scores[0].to(torch.int64)
    int_pred = int(scores_i.argmax().item())
    with torch.no_grad():
        float_pred = int(model(image_tensor.unsqueeze(0)).argmax(dim=1).item())

    class_names = sorted(p.name for p in fx.VAL_DIR.iterdir() if p.is_dir()) if fx.VAL_DIR.exists() else []
    pred_name = class_names[int_pred] if int_pred < len(class_names) else 'UNKNOWN'
    true_name = image_path.parent.name
    true_idx = class_names.index(true_name) if true_name in class_names else None

    with open(out_dir/'debug_input_64x64.hex','w') as f:
        for y in range(64):
            for xx in range(64):
                f.write(f'{int(x[0,y,xx]):02X}\n')

    with open(out_dir/'layer1_ping_expected.hex','w') as f:
        for y in range(32):
            for xx in range(32):
                vals=[q1[0,ch,y,xx].item() for ch in range(8)]
                f.write(f'{pack8(vals):016X}\n')

    with open(out_dir/'layer2_pong_expected.hex','w') as f:
        for p in range(2):
            base=p*8
            for y in range(16):
                for xx in range(16):
                    vals=[q2[0,base+ch,y,xx].item() for ch in range(8)]
                    f.write(f'{pack8(vals):016X}\n')

    with open(out_dir/'fc_scores_expected.txt','w') as f:
        f.write('# class_idx signed_integer_score class_name\n')
        for c in range(fx.NUM_CLASSES):
            name=class_names[c] if c < len(class_names) else 'UNKNOWN'
            f.write(f'{c:02d} {int(scores_i[c].item()):12d} {name}\n')

    with open(out_dir/'checkpoints.txt','w') as f:
        f.write('=== INPUT BRAM CHECKPOINTS ===\n')
        for addr in [0,1,63,64,2048,4095]:
            yy,xx=divmod(addr,64)
            f.write(f'input_addr={addr:4d} data=0x{int(x[0,yy,xx]):02X} ({int(x[0,yy,xx])})\n')
        f.write('\n=== PING RAM CHECKPOINTS ===\n')
        for addr in [0,1,31,32,511,992,1023]:
            yy,xx=divmod(addr,32)
            vals=[q1[0,ch,yy,xx].item() for ch in range(8)]
            f.write(f'ping_addr={addr:4d} word=0x{pack8(vals):016X} ch0..7={list(map(int,vals))}\n')
        f.write('\n=== PONG RAM CHECKPOINTS ===\n')
        for addr in [0,1,15,16,255,256,257,511]:
            p=(addr>>8)&1; yy=(addr>>4)&15; xx=addr&15; base=p*8
            vals=[q2[0,base+ch,yy,xx].item() for ch in range(8)]
            f.write(f'pong_addr={addr:3d} pass={p} y={yy:2d} x={xx:2d} word=0x{pack8(vals):016X} ch={list(map(int,vals))}\n')

    l1_win=make_window_2d(x,0,0)
    with open(out_dir/'l1_addr0_trace.txt','w') as f:
        f.write(f'L1_SHIFT={rs1}\nOutput y=0 x=0\nwindow={l1_win[0].tolist()}\n\n')
        for filt in range(8):
            running=0; f.write(f'FILTER {filt}, bias={int(b1[filt])}\n'); tap=0
            for ky in range(3):
                for kx in range(3):
                    pv=int(l1_win[0,ky,kx]); wv=int(w1[filt,0,ky,kx]); prod=pv*wv; running+=prod
                    f.write(f' tap={tap} pixel={pv:3d} weight={wv:4d} product={prod:7d} acc={running:10d}\n'); tap+=1
            final_sum=running+int(b1[filt]); shifted=final_sum>>rs1; outv=max(0,min(255,shifted))
            f.write(f' final_sum={final_sum} shifted={shifted} qout={outv} expected={int(q1[0,filt,0,0])}\n\n')

    l2_win=make_window_2d(q1[0],0,0)
    with open(out_dir/'l2_addr0_filter0_trace.txt','w') as f:
        f.write(f'L2_SHIFT={rs2}\nOutput y=0 x=0 pass=0 filter=0\n')
        running=0
        for ch in range(8):
            f.write(f'\nINPUT_CHANNEL {ch}\n'); tap=0
            for ky in range(3):
                for kx in range(3):
                    pv=int(l2_win[ch,ky,kx]); wv=int(w2[0,ch,ky,kx]); prod=pv*wv; running+=prod
                    f.write(f' tap={tap} pixel={pv:3d} weight={wv:4d} product={prod:7d} acc={running:12d}\n'); tap+=1
        final_sum=running+int(b2[0]); shifted=final_sum>>rs2; outv=max(0,min(255,shifted))
        f.write(f'\nBIAS={int(b2[0])}\nfinal_sum={final_sum}\nshifted={shifted}\nqout={outv}\nexpected={int(q2[0,0,0,0])}\n')

    with open(out_dir/'summary.txt','w') as f:
        f.write(f'image={image_path}\ntrue_folder={true_name}\ntrue_idx={true_idx}\n')
        f.write(f'L1_SHIFT={rs1}\nL2_SHIFT={rs2}\nFC_SHIFT={wsf}\n')
        f.write(f'float_prediction={float_pred}\ninteger_prediction={int_pred}\ninteger_prediction_name={pred_name}\n')
        f.write(f'input_min={int(x.min())} input_max={int(x.max())}\nq1_min={int(q1.min())} q1_max={int(q1.max())}\nq2_min={int(q2.min())} q2_max={int(q2.max())}\n')

    print('Generated:', out_dir.resolve())
    print(f'L1_SHIFT={rs1}, L2_SHIFT={rs2}, FC_SHIFT={wsf}')
    print('Float prediction:', float_pred)
    print('Integer prediction:', int_pred, pred_name)
    if true_idx is not None: print('True class:', true_idx, true_name)
    print('Copy', out_dir/'debug_input_64x64.hex', 'to Quartus project root.')

if __name__ == '__main__':
    main()
