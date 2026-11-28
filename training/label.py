# ============================================================================
#  label.py — ручная разметка боксов (YOLO) поверх кадров из dataset/people/.
#
#  ДВА КЛАССА (nc=2):
#    0 = player (зелёный)   — вся моделька целиком
#    1 = head   (оранжевый) — голова модели (для аима в голову)
#
#  Управление:
#    ЛКМ + перетаскивание  — нарисовать бокс (текущий класс)
#    ПКМ                   — удалить бокс под курсором
#    1 / 2 / Tab           — выбрать класс (player / head)
#    n / d                 — следующий кадр (автосохраняет)
#    p / a                 — предыдущий кадр (автосохраняет)
#    u / z                 — отменить последний бокс
#    s                     — сохранить в .txt
#    q / Esc               — выход (сохраняет текущий)
#
#  Режимы:
#    python label.py                      # все кадры из dataset/people
#    python label.py --unlabeled          # только ещё неразмеченные кадры
#    python label.py --missing-head       # только кадры, где есть player без head
#    python label.py dataset/_review      # доразметка пропущенных
# ============================================================================

import argparse
import os
from pathlib import Path
import cv2

CLASS_NAMES = {0: "player", 1: "head"}
CLASS_COLORS = {0: (0, 255, 0), 1: (0, 165, 255)}   # BGR: зелёный / оранжевый


def txt_path(img_dir: str, img_name: str) -> str:
    return os.path.join(img_dir, os.path.splitext(img_name)[0] + ".txt")


def list_imgs(img_dir: str, missing_head: bool, unlabeled: bool):
    exts = (".jpg", ".jpeg", ".png")
    all_imgs = sorted(f for f in os.listdir(img_dir) if f.lower().endswith(exts))
    if unlabeled:
        return [f for f in all_imgs if not os.path.exists(txt_path(img_dir, f))]
    if not missing_head:
        return all_imgs
    out = []
    for f in all_imgs:
        p = txt_path(img_dir, f)
        if not os.path.exists(p):
            continue
        lines = [l.strip() for l in open(p, encoding="utf-8", errors="ignore") if l.strip()]
        has0 = any(l.startswith("0 ") for l in lines)
        has1 = any(l.startswith("1 ") for l in lines)
        if has0 and not has1:
            out.append(f)
    return out


def load_boxes(img_dir: str, img_name: str, h: int, w: int):
    out = []
    p = txt_path(img_dir, img_name)
    if not os.path.exists(p):
        return out
    for line in open(p, encoding="utf-8", errors="ignore"):
        parts = line.strip().split()
        if len(parts) < 5:
            continue
        cls, cx, cy, bw, bh = parts[0], *map(float, parts[1:5])
        x1 = int((cx - bw / 2) * w)
        y1 = int((cy - bh / 2) * h)
        x2 = int((cx + bw / 2) * w)
        y2 = int((cy + bh / 2) * h)
        out.append((int(cls), x1, y1, x2, y2))
    return out


def save_boxes(img_dir: str, img_name: str, boxes, h: int, w: int):
    with open(txt_path(img_dir, img_name), "w", encoding="utf-8") as fp:
        for (cls, x1, y1, x2, y2) in boxes:
            x1, x2 = sorted((x1, x2))
            y1, y2 = sorted((y1, y2))
            bw = x2 - x1
            bh = y2 - y1
            if bw < 2 or bh < 2:
                continue
            cx = (x1 + x2) / 2.0
            cy = (y1 + y2) / 2.0
            fp.write(f"{cls} {cx / w:.6f} {cy / h:.6f} "
                     f"{bw / w:.6f} {bh / h:.6f}\n")


class Labeler:
    def __init__(self, img_dir: str, img_name: str, default_cls: int):
        self.img_dir = img_dir
        self.img = cv2.imread(os.path.join(img_dir, img_name))
        if self.img is None:
            raise ValueError(img_name)
        self.h, self.w = self.img.shape[:2]
        self.name = img_name
        self.cls = default_cls
        self.boxes = load_boxes(img_dir, img_name, self.h, self.w)
        self.start = None
        self.cur = None
        self.mouse = (self.w // 2, self.h // 2)

    def delete_at(self, x: int, y: int) -> bool:
        hits = []
        for idx, (_, x1, y1, x2, y2) in enumerate(self.boxes):
            lx1, lx2 = sorted((x1, x2))
            ly1, ly2 = sorted((y1, y2))
            if lx1 <= x <= lx2 and ly1 <= y <= ly2:
                area = (lx2 - lx1) * (ly2 - ly1)
                hits.append((area, idx))
        if not hits:
            return False
        hits.sort()
        self.boxes.pop(hits[0][1])
        return True

    def render(self, idx: int, total: int):
        disp = self.img.copy()
        for (cls, x1, y1, x2, y2) in self.boxes:
            col = CLASS_COLORS.get(cls, (0, 255, 0))
            cv2.rectangle(disp, (x1, y1), (x2, y2), col, 2)
            cv2.putText(disp, CLASS_NAMES.get(cls, str(cls))[0].upper(),
                        (x1, max(12, y1 - 4)), cv2.FONT_HERSHEY_SIMPLEX, 0.5, col, 1)

        mx, my = self.mouse
        cv2.line(disp, (mx, 0), (mx, self.h), (90, 90, 90), 1)
        cv2.line(disp, (0, my), (self.w, my), (90, 90, 90), 1)

        if self.start and self.cur:
            cv2.rectangle(disp, self.start, self.cur, CLASS_COLORS[self.cls], 1)

        col = CLASS_COLORS[self.cls]
        cv2.putText(disp, f"[{idx + 1}/{total}] {self.name}  class: {CLASS_NAMES[self.cls]}  "
                          f"boxes: {len(self.boxes)}",
                    (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.55, col, 2)
        cv2.putText(disp, "LMB=box  RMB=del  1/2/Tab=class  n/p=next/prev  u=undo  q=quit",
                    (10, self.h - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 220, 255), 1)
        return disp


def main():
    default_dir = str(Path(__file__).resolve().parent / "dataset" / "people")
    ap = argparse.ArgumentParser()
    ap.add_argument("folder", nargs="?", default=default_dir)
    ap.add_argument("--unlabeled", action="store_true",
                    help="показать только неразмеченные кадры (без .txt)")
    ap.add_argument("--missing-head", action="store_true",
                    help="показать только кадры с player без head (сразу в режиме класса head)")
    args = ap.parse_args()

    img_dir = args.folder
    if not os.path.isdir(img_dir):
        rel = Path(__file__).resolve().parent / img_dir
        if rel.is_dir():
            img_dir = str(rel)
    files = list_imgs(img_dir, args.missing_head, args.unlabeled)
    if not files:
        print("Нет подходящих кадров в", img_dir)
        return

    default_cls = 1 if args.missing_head else 0
    i = 0
    lab = Labeler(img_dir, files[i], default_cls)

    def on_mouse(event, x, y, flags, _):
        lab.mouse = (x, y)
        if event == cv2.EVENT_LBUTTONDOWN:
            lab.start = (x, y)
            lab.cur = (x, y)
        elif event == cv2.EVENT_MOUSEMOVE and lab.start:
            lab.cur = (x, y)
        elif event == cv2.EVENT_LBUTTONUP and lab.start:
            x1, y1 = lab.start
            x2, y2 = x, y
            lab.start = lab.cur = None
            if abs(x2 - x1) > 3 and abs(y2 - y1) > 3:
                lab.boxes.append((lab.cls, x1, y1, x2, y2))
        elif event == cv2.EVENT_RBUTTONDOWN:
            lab.delete_at(x, y)

    cv2.namedWindow("label")
    cv2.setMouseCallback("label", on_mouse)

    def switch_to(idx):
        nonlocal lab
        cur_cls = lab.cls
        save_boxes(img_dir, lab.name, lab.boxes, lab.h, lab.w)
        lab = Labeler(img_dir, files[idx], cur_cls)

    print("[label] кадров:", len(files))

    while True:
        cv2.imshow("label", lab.render(i, len(files)))
        k = cv2.waitKey(16) & 0xFF

        if k in (ord("q"), 27):
            save_boxes(img_dir, lab.name, lab.boxes, lab.h, lab.w)
            break
        elif k == ord("1"):
            lab.cls = 0
        elif k == ord("2"):
            lab.cls = 1
        elif k == 9:
            lab.cls = 1 - lab.cls
        elif k in (ord("n"), ord("d")):
            i = min(i + 1, len(files) - 1)
            switch_to(i)
        elif k in (ord("p"), ord("a")):
            i = max(i - 1, 0)
            switch_to(i)
        elif k in (ord("u"), ord("z")) and lab.boxes:
            lab.boxes.pop()
        elif k == ord("s"):
            save_boxes(img_dir, lab.name, lab.boxes, lab.h, lab.w)
            print("saved", lab.name, f"({len(lab.boxes)} боксов)")

    cv2.destroyAllWindows()
    print("\n[label] готово.")


if __name__ == "__main__":
    main()
