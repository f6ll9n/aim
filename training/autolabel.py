# ============================================================================
#  autolabel.py — псевдоразметка (self-training): seed-модель сама обводит
#  врагов на неразмеченных кадрах, экономя ручную работу.
#
#  Логика:
#   * Берёт кадры из dataset/people/, у которых ЕЩЁ НЕТ .txt разметки.
#   * Гонит через текущую модель (best.pt / best.onnx) с порогом --conf.
#   * Найденные боксы -> YOLO .txt рядом с кадром (класс 0).
#   * Кадры, где модель НИЧЕГО не нашла, -> dataset/_review/ (ты добьёшь руками).
#
#  Запуск:
#   python autolabel.py --conf 0.5
# ============================================================================

import argparse
import shutil
from pathlib import Path

from ultralytics import YOLO


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="runs/detect/train/weights/best.pt")
    ap.add_argument("--conf", type=float, default=0.5)
    ap.add_argument("--imgsz", type=int, default=640)
    args = ap.parse_args()

    ROOT = Path(__file__).resolve().parent
    img_dir   = ROOT / "dataset" / "people"
    review_dir = ROOT / "dataset" / "_review"
    review_dir.mkdir(exist_ok=True)

    imgs = [p for p in sorted(img_dir.iterdir())
            if p.suffix.lower() in (".jpg", ".jpeg", ".png")
            and not (img_dir / (p.stem + ".txt")).exists()]

    if not imgs:
        print("[autolabel] неразмеченных кадров нет — всё уже размечено.")
        return

    print(f"[autolabel] неразмеченных кадров: {len(imgs)}")
    print(f"[autolabel] модель: {args.model}  conf >= {args.conf}")

    model = YOLO(str(ROOT / args.model))
    results = model.predict(source=[str(p) for p in imgs],
                            conf=args.conf, imgsz=args.imgsz,
                            device="cpu", verbose=False,
                            save=False, save_txt=False)

    labeled = 0
    review  = 0
    total_boxes = 0
    for img_path, res in zip(imgs, results):
        n = len(res.boxes)
        if n == 0:
            shutil.move(str(img_path), str(review_dir / img_path.name))
            review += 1
            continue
        with open(img_dir / (img_path.stem + ".txt"), "w") as fp:
            for b in res.boxes:
                x, y, w, h = b.xywhn[0].tolist()   # нормализованные (0..1)
                cls = int(b.cls[0])
                fp.write(f"{cls} {x:.6f} {y:.6f} {w:.6f} {h:.6f}\n")
                total_boxes += 1
        labeled += 1

    print(f"\n[autolabel] готово:")
    print(f"  авто-размечено: {labeled} кадров ({total_boxes} боксов)")
    print(f"  без детекций -> review: {review} -> {review_dir}")


if __name__ == "__main__":
    main()
