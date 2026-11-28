# ============================================================================
#  autolabel.py — псевдоразметка (player + head) для dataset/people/.
#
#  Поддерживает как .onnx (ONNX Runtime, без PyTorch/ultralytics), так и .pt.
#  По умолчанию берёт 2-классовую модель ../out/build/x64-vcpkg/Release/best.onnx
#  (или runs/detect/train/weights/best.pt, если ONNX не найден).
#
#  Возможности:
#   1. Авторазметка кадров без .txt (пишет классы 0=player и 1=head).
#      Кадры с 0 детекциями переносит в dataset/_review/ (если не указан --no-move).
#   2. --fill-heads: проходит по уже размеченным .txt, где есть player (0),
#      но нет head (1), и дописывает найденную внутри верхней части тела голову.
#
#  Примеры:
#   python autolabel.py --conf 0.40 --head-conf 0.30
#   python autolabel.py --fill-heads --head-conf 0.30
# ============================================================================

import argparse
import shutil
from pathlib import Path
from typing import List, Tuple

import cv2
import numpy as np


def nms_class(boxes: List[Tuple[int, float, float, float, float, float]], iou_th: float = 0.45):
    out = []
    for cls in (0, 1):
        cb = sorted([b for b in boxes if b[0] == cls], key=lambda b: -b[1])
        keep = []
        for b in cb:
            x1, y1 = b[2] - b[4] * 0.5, b[3] - b[5] * 0.5
            x2, y2 = b[2] + b[4] * 0.5, b[3] + b[5] * 0.5
            ok = True
            for k in keep:
                kx1, ky1 = k[2] - k[4] * 0.5, k[3] - k[5] * 0.5
                kx2, ky2 = k[2] + k[4] * 0.5, k[3] + k[5] * 0.5
                ix1, iy1 = max(x1, kx1), max(y1, ky1)
                ix2, iy2 = min(x2, kx2), min(y2, ky2)
                inter = max(0.0, ix2 - ix1) * max(0.0, iy2 - iy1)
                uni = b[4] * b[5] + k[4] * k[5] - inter
                if uni > 0.0 and inter / uni > iou_th:
                    ok = False
                    break
            if ok:
                keep.append(b)
        out.extend(keep)
    return out


class OnnxRunner:
    def __init__(self, path: Path, imgsz: int):
        import onnxruntime as ort
        self.sess = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
        self.in_name = self.sess.get_inputs()[0].name
        out_shape = self.sess.get_outputs()[0].shape
        self.nc = max(1, int(out_shape[1]) - 4)
        self.imgsz = imgsz

    def predict(self, img_path: Path, conf_p: float, conf_h: float):
        im = cv2.imread(str(img_path))
        if im is None:
            return []
        if im.shape[1] != self.imgsz or im.shape[0] != self.imgsz:
            im = cv2.resize(im, (self.imgsz, self.imgsz), interpolation=cv2.INTER_LINEAR)
        x = im[:, :, ::-1].transpose(2, 0, 1)[None].astype(np.float32) / 255.0
        y = self.sess.run(None, {self.in_name: x})[0][0]  # (4+nc, anchors)

        raw = []
        inv = 1.0 / float(self.imgsz)
        for cls in range(min(self.nc, 2)):
            th = conf_p if cls == 0 else conf_h
            scores = y[4 + cls]
            idxs = np.where(scores >= th)[0]
            for i in idxs:
                cx = float(y[0, i]) * inv
                cy = float(y[1, i]) * inv
                bw = float(y[2, i]) * inv
                bh = float(y[3, i]) * inv
                raw.append((cls, float(scores[i]), cx, cy, bw, bh))
        return nms_class(raw)


class PtRunner:
    def __init__(self, path: Path, imgsz: int):
        from ultralytics import YOLO
        self.model = YOLO(str(path))
        self.imgsz = imgsz

    def predict(self, img_path: Path, conf_p: float, conf_h: float):
        res = self.model.predict(
            source=str(img_path), conf=min(conf_p, conf_h),
            imgsz=self.imgsz, device="cpu", verbose=False, save=False
        )[0]
        raw = []
        for b in res.boxes:
            cls = int(b.cls[0])
            sc = float(b.conf[0])
            if (cls == 0 and sc < conf_p) or (cls == 1 and sc < conf_h):
                continue
            cx, cy, bw, bh = b.xywhn[0].tolist()
            raw.append((cls, sc, cx, cy, bw, bh))
        return nms_class(raw)


def head_belongs_to_player(hx: float, hy: float, px: float, py: float, pw: float, ph: float) -> bool:
    return (px - pw * 0.60 <= hx <= px + pw * 0.60) and (py - ph * 0.65 <= hy <= py + ph * 0.20)


def main() -> None:
    ROOT = Path(__file__).resolve().parent
    default_onnx = ROOT.parent / "out" / "build" / "x64-vcpkg" / "Release" / "best.onnx"
    default_pt   = ROOT / "runs" / "detect" / "train" / "weights" / "best.pt"
    default_model = str(default_onnx if default_onnx.exists() else default_pt)

    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=default_model)
    ap.add_argument("--conf", type=float, default=0.40, help="порог для player (класс 0)")
    ap.add_argument("--head-conf", type=float, default=0.30, help="порог для head (класс 1)")
    ap.add_argument("--imgsz", type=int, default=640)
    ap.add_argument("--fill-heads", action="store_true",
                    help="доразметить head (1) на кадрах, где уже есть player (0), но нет head")
    ap.add_argument("--no-move", action="store_true",
                    help="не переносить пустые кадры в dataset/_review/")
    args = ap.parse_args()

    model_path = Path(args.model)
    if not model_path.is_absolute():
        model_path = (ROOT / model_path).resolve()
    if not model_path.exists():
        print(f"[autolabel] модель не найдена: {model_path}")
        return

    runner = OnnxRunner(model_path, args.imgsz) if model_path.suffix.lower() == ".onnx" else PtRunner(model_path, args.imgsz)

    img_dir    = ROOT / "dataset" / "people"
    review_dir = ROOT / "dataset" / "_review"
    review_dir.mkdir(exist_ok=True)
    exts = (".jpg", ".jpeg", ".png")

    all_imgs = [p for p in sorted(img_dir.iterdir()) if p.suffix.lower() in exts]

    if args.fill_heads:
        targets = []
        for p in all_imgs:
            txt = img_dir / (p.stem + ".txt")
            if not txt.exists():
                continue
            lines = [l.strip() for l in txt.read_text(encoding="utf-8", errors="ignore").splitlines() if l.strip()]
            has0 = any(l.startswith("0 ") for l in lines)
            has1 = any(l.startswith("1 ") for l in lines)
            if has0 and not has1:
                targets.append((p, txt, lines))

        print(f"[autolabel --fill-heads] кадров без head: {len(targets)} | модель: {model_path.name}")
        filled_files = 0
        added_heads = 0
        for p, txt, lines in targets:
            preds = runner.predict(p, args.conf, args.head_conf)
            heads = [b for b in preds if b[0] == 1]
            if not heads:
                continue
            pboxes = [tuple(map(float, l.split()[1:5])) for l in lines if l.startswith("0 ")]
            new_lines = []
            for _, _, hcx, hcy, hbw, hbh in heads:
                if any(head_belongs_to_player(hcx, hcy, px, py, pw, ph) for px, py, pw, ph in pboxes):
                    new_lines.append(f"1 {hcx:.6f} {hcy:.6f} {hbw:.6f} {hbh:.6f}")
            if new_lines:
                txt.write_text("\n".join(lines + new_lines) + "\n", encoding="utf-8")
                filled_files += 1
                added_heads += len(new_lines)
        print(f"[autolabel --fill-heads] доразмечено кадров: {filled_files} (+{added_heads} head)")
        return

    imgs = [p for p in all_imgs if not (img_dir / (p.stem + ".txt")).exists()]
    if not imgs:
        print("[autolabel] неразмеченных кадров нет — всё уже размечено.")
        return

    print(f"[autolabel] неразмеченных кадров: {len(imgs)} | модель: {model_path.name} (p>={args.conf}, h>={args.head_conf})")

    labeled = 0
    review  = 0
    n_p = 0
    n_h = 0
    for idx, img_path in enumerate(imgs, 1):
        preds = runner.predict(img_path, args.conf, args.head_conf)
        players = [b for b in preds if b[0] == 0]
        heads   = [b for b in preds if b[0] == 1]

        # Оставляем головы, подтверждённые внутри player, либо уверенные (>= max(0.45, head_conf))
        valid_heads = []
        for h in heads:
            in_player = any(head_belongs_to_player(h[2], h[3], p[2], p[3], p[4], p[5]) for p in players)
            if in_player or h[1] >= max(0.45, args.head_conf):
                valid_heads.append(h)

        final_boxes = players + valid_heads
        if not final_boxes:
            if not args.no_move:
                shutil.move(str(img_path), str(review_dir / img_path.name))
            review += 1
            continue

        with open(img_dir / (img_path.stem + ".txt"), "w", encoding="utf-8") as fp:
            for cls, _, x, y, w, h in final_boxes:
                fp.write(f"{cls} {x:.6f} {y:.6f} {w:.6f} {h:.6f}\n")
        n_p += len(players)
        n_h += len(valid_heads)
        labeled += 1
        if idx % 100 == 0:
            print(f"  ... {idx}/{len(imgs)}: размечено {labeled} (player={n_p}, head={n_h})")

    print(f"\n[autolabel] готово:")
    print(f"  авто-размечено: {labeled} кадров (player={n_p}, head={n_h})")
    print(f"  без детекций -> {'пропущено' if args.no_move else 'review'}: {review}")


if __name__ == "__main__":
    main()
