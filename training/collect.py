# ============================================================================
#  collect.py — сбор тренировочных кадров (центр экрана = FOV) для YOLO.
#
#  Запускать ВО ВРЕМЯ игры (Pixel Gun 3D). Скрипт захватывает центральную
#  квадратную зону монитора и сохраняет кадры для последующей разметки.
#
#  Режимы:
#   --mode manual   : превью + клавиша 's' = сохранить кадр, 'q' = выход
#                     (лучший для выборочных моментов с «модельками»)
#   --mode auto     : автоматически сохраняет каждые --interval мс
#                     (набрать массу кадров, потом разметить вручную)
#
#  Примеры:
#   python collect.py
#   python collect.py --fov 640 --mode auto --interval 300 --out dataset/images
# ============================================================================

import argparse
import os
import time

import mss
import numpy as np
import cv2


def main() -> None:
    ap = argparse.ArgumentParser(description="Collect YOLO training frames")
    ap.add_argument("--out", default="dataset/images")
    ap.add_argument("--fov", type=int, default=640)
    ap.add_argument("--mode", choices=["manual", "auto"], default="manual")
    ap.add_argument("--interval", type=int, default=300, help="ms between saves in auto mode")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    n = len([f for f in os.listdir(args.out)
             if f.lower().endswith((".jpg", ".png"))])

    sct = mss.mss()
    mon = sct.monitors[1]  # primary
    mw, mh = mon["width"], mon["height"]
    fov = min(args.fov, mw, mh)
    region = {
        "left":   mon["left"] + (mw - fov) // 2,
        "top":    mon["top"]  + (mh - fov) // 2,
        "width":  fov,
        "height": fov,
    }

    print(f"[collect] FOV {fov}x{fov} по центру. "
          f"{'s=сохранить, ' if args.mode == 'manual' else 'авто-сохранение, '}q=выход")
    print(f"[collect] кадры -> {os.path.abspath(args.out)} (уже {n})")

    last_save = 0.0
    while True:
        # mss отдаёт BGRA; берём первые 3 канала (BGR — то, что ждёт YOLO)
        img = np.asarray(sct.grab(region))[:, :, :3]

        cv2.imshow("collect (центр экрана)", img)
        key = cv2.waitKey(1) & 0xFF

        now_ms = time.time() * 1000.0
        do_save = False
        if args.mode == "manual":
            do_save = (key == ord("s"))
        else:
            do_save = (now_ms - last_save) >= args.interval

        if do_save:
            path = os.path.join(args.out, f"frame_{n:06d}.jpg")
            cv2.imwrite(path, img, [cv2.IMWRITE_JPEG_QUALITY, 95])
            last_save = now_ms
            n += 1
            print(f"  saved {os.path.basename(path)}  (всего {n})")

        if key == ord("q"):
            break

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
