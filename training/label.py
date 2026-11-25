# ============================================================================
#  label.py — ручная разметка боксов (YOLO) поверх кадров из dataset/people/.
#
#  Работа: открывает кадры по одному, обводишь модели мышкой, сохраняешь.
#  Выход: рядом с каждым jpg в people/ создаётся .txt в формате YOLO:
#        <class> <x_center> <y_center> <width> <height>   (все в долях 0..1)
#
#  ДВА КЛАССА (nc=2):
#    0 = player (зелёный)  — вся моделька целиком
#    1 = head   (оранжевый) — голова той же модели (для аима в голову)
#
#  Управление:
#    левая кнопка мыши + перетаскивание  — нарисовать бокс (текущий класс)
#    1 / 2                                — выбрать класс (player / head)
#    Tab                                  — быстро переключить класс
#    n / d                                — следующий кадр (автосохраняет)
#    p / a                                — предыдущий кадр (автосохраняет)
#    u / z                                — убрать последний бокс
#    s                                    — сохранить в .txt
#    q / Esc                              — выход (сохраняет текущий)
# ============================================================================

import os
import sys

import cv2

# Папку можно переопределить аргументом:  python label.py dataset/_review
IMG = sys.argv[1] if len(sys.argv) > 1 else "dataset/people"

CLASS_NAMES = {0: "player", 1: "head"}
CLASS_COLORS = {0: (0, 255, 0), 1: (0, 165, 255)}   # BGR: зелёный / оранжевый
DEFAULT_CLASS = 0

files_ = None                   # заполняется/обновляется в main


def list_imgs():
    exts = (".jpg", ".jpeg", ".png")
    return sorted(f for f in os.listdir(IMG) if f.lower().endswith(exts))


def txt_path(img_name: str) -> str:
    return os.path.join(IMG, os.path.splitext(img_name)[0] + ".txt")


def load_boxes(img_name: str, h: int, w: int):
    """Читает существующую разметку -> список пиксельных (cls,x1,y1,x2,y2)."""
    out = []
    p = txt_path(img_name)
    if not os.path.exists(p):
        return out
    for line in open(p):
        parts = line.strip().split()
        if len(parts) < 5:
            continue
        cls, cx, cy, bw, bh = parts[0], *map(float, parts[1:5])
        x1 = int((cx - bw / 2) * w); y1 = int((cy - bh / 2) * h)
        x2 = int((cx + bw / 2) * w); y2 = int((cy + bh / 2) * h)
        out.append((int(cls), x1, y1, x2, y2))
    return out


def save_boxes(img_name: str, boxes, h: int, w: int):
    """Записывает пиксельные боксы в YOLO .txt (нормализуя)."""
    with open(txt_path(img_name), "w") as fp:
        for (cls, x1, y1, x2, y2) in boxes:
            x1, x2 = sorted((x1, x2)); y1, y2 = sorted((y1, y2))
            bw = x2 - x1; bh = y2 - y1
            if bw < 2 or bh < 2:
                continue
            cx = (x1 + x2) / 2.0; cy = (y1 + y2) / 2.0
            fp.write(f"{cls} {cx / w:.6f} {cy / h:.6f} "
                     f"{bw / w:.6f} {bh / h:.6f}\n")


class Labeler:
    def __init__(self, img_name):
        self.img = cv2.imread(os.path.join(IMG, img_name))
        if self.img is None:
            raise ValueError(img_name)
        self.h, self.w = self.img.shape[:2]
        self.name = img_name
        self.cls = DEFAULT_CLASS
        self.boxes = load_boxes(img_name, self.h, self.w)  # [(cls,x1,y1,x2,y2)]
        self.start = None   # (x, y) начало перетаскивания
        self.cur = None     # (x, y) текущая позиция

    def render(self):
        disp = self.img.copy()
        for (cls, x1, y1, x2, y2) in self.boxes:
            col = CLASS_COLORS.get(cls, (0, 255, 0))
            cv2.rectangle(disp, (x1, y1), (x2, y2), col, 2)
            cv2.putText(disp, CLASS_NAMES.get(cls, str(cls))[0].upper(),
                        (x1, max(12, y1 - 4)), cv2.FONT_HERSHEY_SIMPLEX,
                        0.5, col, 1)
        if self.start and self.cur:
            cv2.rectangle(disp, self.start, self.cur, (0, 0, 255), 1)
        col = CLASS_COLORS[self.cls]
        cv2.putText(disp, f"{self.name}  класс: {CLASS_NAMES[self.cls]}  "
                          f"боксов: {len(self.boxes)}",
                    (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.6, col, 2)
        cv2.putText(disp, "мышь=бокс  1=player 2=head Tab=перекл  n/p=листать  u=отмена  q=выход",
                    (10, self.h - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 220, 255), 1)
        return disp


def main():
    global files_
    files_ = list_imgs()
    if not files_:
        print("В", IMG, "нет кадров. Сначала отсортируй (sort_frames.py).")
        return

    i = 0
    lab = Labeler(files_[i])

    def on_mouse(event, x, y, flags, _):
        if event == cv2.EVENT_LBUTTONDOWN:
            lab.start = (x, y); lab.cur = (x, y)
        elif event == cv2.EVENT_MOUSEMOVE and lab.start:
            lab.cur = (x, y)
        elif event == cv2.EVENT_LBUTTONUP and lab.start:
            x1, y1 = lab.start; x2, y2 = x, y
            lab.start = lab.cur = None
            if abs(x2 - x1) > 3 and abs(y2 - y1) > 3:
                lab.boxes.append((lab.cls, x1, y1, x2, y2))

    cv2.namedWindow("label")
    cv2.setMouseCallback("label", on_mouse)

    def switch_to(idx):
        nonlocal lab
        save_boxes(lab.name, lab.boxes, lab.h, lab.w)
        lab = Labeler(files_[idx])

    print("[label] кадров:", len(files_))
    print("[label] классы: 0=player  1=head. Рисуй мышкой, 1/2 или Tab — выбор класса.")

    while True:
        cv2.imshow("label", lab.render())
        k = cv2.waitKey(16) & 0xFF

        if k in (ord("q"), 27):
            save_boxes(lab.name, lab.boxes, lab.h, lab.w)
            break
        elif k == ord("1"):
            lab.cls = 0
        elif k == ord("2"):
            lab.cls = 1
        elif k == 9:                       # Tab
            lab.cls = 1 - lab.cls
        elif k in (ord("n"), ord("d")):
            i = min(i + 1, len(files_) - 1)
            switch_to(i)
        elif k in (ord("p"), ord("a")):
            i = max(i - 1, 0)
            switch_to(i)
        elif k in (ord("u"), ord("z")) and lab.boxes:
            lab.boxes.pop()
        elif k == ord("s"):
            save_boxes(lab.name, lab.boxes, lab.h, lab.w)
            print("saved", lab.name, f"({len(lab.boxes)} боксов)")

    cv2.destroyAllWindows()
    print("\n[label] готово. Размечено кадров:",
          sum(1 for f in files_ if os.path.exists(txt_path(f))))


if __name__ == "__main__":
    main()
