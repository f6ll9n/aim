# ============================================================================
#  train.py — разбивка датасета + обучение YOLO + экспорт в ONNX.
#
#  Требует установки:  pip install ultralytics
#  (на CPU локально или, быстрее, на Google Colab — см. README.md)
#
#  Пример (локально на CPU):
#    python train.py --model yolov8n.pt --epochs 100 --imgsz 640 --batch 8
#
#  Выход:
#    runs/detect/train/weights/best.pt   — лучшие веса
#    runs/detect/train/weights/best.onnx — для твоего C++ (ONNX Runtime)
# ============================================================================

import argparse
import random
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATASET = ROOT / "dataset"


def split_dataset(val_ratio: float, seed: int) -> None:
    """Из people/ (позитивы) и background/ (негативы) собирает train/val."""
    exts = (".jpg", ".jpeg", ".png")

    # Очищаем старые сплиты — иначе копируем поверх и накапливаем хлам
    # от прошлых прогонов (дубликаты и устаревшие лейблы).
    for sub in ("images/train", "images/val", "labels/train", "labels/val"):
        d = DATASET / sub
        if d.exists():
            shutil.rmtree(d)

    people_dir    = DATASET / "people"
    background_dir = DATASET / "background"

    all_people = sorted(p for p in people_dir.iterdir()
                        if p.suffix.lower() in exts)

    # позитивы — только размеченные (у которых есть .txt рядом)
    positives = [p for p in all_people
                 if (people_dir / (p.stem + ".txt")).exists()]
    unlabeled = [p for p in all_people if p not in positives]
    if unlabeled:
        print(f"[warn] {len(unlabeled)} кадров в people/ без разметки — пропущены")

    negatives = sorted(p for p in background_dir.iterdir()
                       if p.suffix.lower() in exts)

    random.seed(seed)
    random.shuffle(positives)
    random.shuffle(negatives)

    def cut(files):
        n = int(len(files) * (1.0 - val_ratio))
        return files[:n], files[n:]

    pos_tr, pos_val = cut(positives)
    neg_tr, neg_val = cut(negatives)

    # --- раскладка ---
    for split, imgs in (("train", pos_tr), ("val", pos_val)):
        for d in ("images", "labels"):
            (DATASET / d / split).mkdir(parents=True, exist_ok=True)
        for img in imgs:
            shutil.copy2(img, DATASET / "images" / split / img.name)
            shutil.copy2(people_dir / (img.stem + ".txt"),
                         DATASET / "labels" / split / (img.stem + ".txt"))

    # негативы: изображение + пустой .txt (обозначает «объектов нет»)
    for split, imgs in (("train", neg_tr), ("val", neg_val)):
        for d in ("images", "labels"):
            (DATASET / d / split).mkdir(parents=True, exist_ok=True)
        for img in imgs:
            shutil.copy2(img, DATASET / "images" / split / img.name)
            (DATASET / "labels" / split / (img.stem + ".txt")).write_text("")

    print(f"[split] позитивы: train={len(pos_tr)} val={len(pos_val)} | "
          f"негативы: train={len(neg_tr)} val={len(neg_val)}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model",  default="yolov8n.pt")
    ap.add_argument("--epochs", type=int, default=100)
    ap.add_argument("--imgsz",  type=int, default=640)
    ap.add_argument("--batch",  type=int, default=8)
    ap.add_argument("--device", default="cpu")   # "0" на Colab GPU
    ap.add_argument("--patience", type=int, default=20)  # early stopping: стоп через N эпох без роста mAP
    ap.add_argument("--val-ratio", type=float, default=0.2)
    ap.add_argument("--seed",   type=int, default=0)
    args = ap.parse_args()

    split_dataset(args.val_ratio, args.seed)

    from ultralytics import YOLO

    model = YOLO(args.model)
    model.train(
        data=str(DATASET / "data.yaml"),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        device=args.device,
        patience=args.patience,
        project=str(ROOT / "runs" / "detect"),
        name="train",
    )

    # Экспорт для C++ (ONNX Runtime) — вход 1x3x640x640 float NCHW.
    # Берём веса из ФАКТИЧЕСКОЙ папки прогона (ultralytics авто-инкрементит
    # имя, поэтому захардкоженный путь приводил к экспорту старых весов).
    save_dir = Path(model.trainer.save_dir)
    best = save_dir / "weights" / "best.pt"
    model = YOLO(str(best))
    model.export(format="onnx", imgsz=args.imgsz, opset=17)
    print(f"[done] ONNX -> {best.with_suffix('.onnx')}")


if __name__ == "__main__":
    main()
