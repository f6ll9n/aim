# ============================================================================
#  sort_frames.py — ручная сортировка кадров.
#
#  Раскладывает кадры из --source по категориям:
#    b  -> background  (пустой кадр БЕЗ людей = негатив; если source уже
#                       background, то просто ОСТАВЛЯЕТ файл на месте)
#    p  -> people      (кадр с противником/человеком = позитив)
#    x  -> _trash      (меню / загрузка / рабочий стол / битое)
#
#  Клавиши:
#    b        — оставить как фон / в фон (негатив)
#    p        — в люди (позитив)
#    x        — в мусор (_trash)
#    a / d    — назад / вперёд (листать, чтобы поправить ошибку)
#    q / Esc  — выход
#
#  После b/p/x автоматически открывается следующий кадр — сортировка одной рукой.
#
#  Использование:
#    python sort_frames.py                        # сортирует dataset/images/
#    python sort_frames.py --source dataset/background
# ============================================================================

import argparse
import os
import shutil

import cv2

ROOT       = "dataset"
DEFAULT_SRC = os.path.join(ROOT, "images")
BACKGROUND  = os.path.join(ROOT, "background")
PEOPLE      = os.path.join(ROOT, "people")
TRASH       = os.path.join(ROOT, "_trash")


def list_imgs(path):
    exts = (".jpg", ".jpeg", ".png")
    return sorted(f for f in os.listdir(path) if f.lower().endswith(exts))


def main() -> None:
    ap = argparse.ArgumentParser(description="Ручная сортировка кадров")
    ap.add_argument("--source", default=DEFAULT_SRC,
                    help="папка с несортированными кадрами")
    ap.add_argument("--size", type=int, default=720,
                    help="макс. сторона превью (0 = не менять)")
    args = ap.parse_args()

    src = args.source
    if not os.path.isdir(src):
        print("Нет папки:", src)
        return

    for d in (BACKGROUND, PEOPLE, TRASH):
        os.makedirs(d, exist_ok=True)

    src_abs = os.path.abspath(src)
    bg_abs = os.path.abspath(BACKGROUND)
    keep_on_b = src_abs == bg_abs   # если source уже background, 'b' не двигает файл

    files = list_imgs(src)
    if not files:
        print("В", src, "нет кадров для сортировки.")
        return

    i = 0
    cnt = {"background": 0, "people": 0, "trash": 0}

    HINT = "b=фон(пусто)  p=люди  x=мусор  a/d=листать  q=выход"
    print("[sort] кадров:", len(files))
    print("[sort] " + HINT)
    if keep_on_b:
        print("[sort] режим: 'b' ОСТАВЛЯЕТ кадр на месте (в background)")

    def make_disp(img):
        h, w = img.shape[:2]
        if args.size and max(h, w) > args.size:
            s = args.size / max(h, w)
            img = cv2.resize(img, (int(w * s), int(h * s)),
                             interpolation=cv2.INTER_AREA)
        disp = img.copy()
        cv2.putText(disp, f"{i + 1}/{len(files)}  {files[i] if i < len(files) else ''}",
                    (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 220, 0), 2)
        cv2.putText(disp, HINT,
                    (10, disp.shape[0] - 12), cv2.FONT_HERSHEY_SIMPLEX,
                    0.5, (0, 220, 255), 1)
        return disp

    while 0 <= i < len(files):
        f = files[i]
        img = cv2.imread(os.path.join(src, f))
        if img is None:
            files.pop(i)
            continue

        cv2.imshow("sort", make_disp(img))
        k = cv2.waitKey(0) & 0xFF

        if k in (ord("q"), 27):            # q / Esc
            break
        elif k == ord("b"):                # фон (негатив)
            if not keep_on_b:
                shutil.move(os.path.join(src, f), os.path.join(BACKGROUND, f))
            cnt["background"] += 1
            files.pop(i)
            if i >= len(files):
                i = len(files) - 1
        elif k == ord("p"):                # люди (позитив)
            shutil.move(os.path.join(src, f), os.path.join(PEOPLE, f))
            cnt["people"] += 1
            files.pop(i)
            if i >= len(files):
                i = len(files) - 1
        elif k == ord("x"):                # мусор
            shutil.move(os.path.join(src, f), os.path.join(TRASH, f))
            cnt["trash"] += 1
            files.pop(i)
            if i >= len(files):
                i = len(files) - 1
        elif k == ord("d"):                # вперёд без классификации
            i += 1
        elif k == ord("a"):                # назад
            i = max(0, i - 1)

    cv2.destroyAllWindows()
    print("\n[sort] готово:")
    keep_txt = "оставлено" if keep_on_b else "перемещено"
    print(f"  background (пустые): {cnt['background']} ({keep_txt})")
    print(f"  people    (люди):    {cnt['people']} -> {PEOPLE}")
    print(f"  trash     (мусор):   {cnt['trash']} -> {TRASH}")
    print(f"  осталось несортированных: {len(list_imgs(src))}")


if __name__ == "__main__":
    main()
