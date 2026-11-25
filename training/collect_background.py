# ============================================================================
#  collect_background.py — сбор НЕГАТИВНЫХ (фоновых) кадров для YOLO.
#
#  Назначение: набрать кадры БЕЗ врагов (пустые карты, коридоры, стены,
#  небо, меню) — это снижает ложные срабатывания модели. Кадры сразу
#  сохраняются в dataset/background/ и НЕ требуют разметки (train.py
#  сам добавляет им пустой .txt = «объектов нет»).
#
#  ВАЖНО: снимается ТОЛЬКО центр экрана (FOV, 640x640 по умолчанию) —
#  ровно та зона, которую видит модель на инференсе в capture_demo.exe.
#  Не снимай фулл-скрин: при обучении он сожмётся и даст другой ракурс.
#
#  Используй на ПУСТЫХ картах / участках, где нет ни врагов, ни союзников.
#
#  Режимы:
#   --mode auto    (по умолчанию): непрерывно, каждые --interval мс,
#                   с защитой от дублей (не сохраняет почти одинаковые кадры).
#                   Просто беги по пустым картам и крути камерой.
#   --mode manual  : превью + клавиша 's' = сохранить один кадр.
#
#  Управление:  q / Esc — выход
#
#  Пример:
#   python collect_background.py
#   python collect_background.py --interval 150 --min-diff 4
# ============================================================================

import argparse
import os
import time
from pathlib import Path

import mss
import numpy as np
import cv2


def count_imgs(folder: str) -> int:
    exts = (".jpg", ".jpeg", ".png")
    if not os.path.isdir(folder):
        return 0
    return len([f for f in os.listdir(folder) if f.lower().endswith(exts)])


def main() -> None:
    ap = argparse.ArgumentParser(description="Collect YOLO background (negative) frames")
    ap.add_argument("--out", default=None,
                    help="папка для кадров (по умолчанию dataset/background рядом со скриптом)")
    ap.add_argument("--fov", type=int, default=640, help="размер центрального кропа")
    ap.add_argument("--mode", choices=["auto", "manual"], default="auto")
    ap.add_argument("--interval", type=int, default=200,
                    help="мс между сохранениями в auto-режиме")
    ap.add_argument("--min-diff", type=float, default=6.0,
                    help="мин. средняя разница между кадрами, чтобы сохранить (анти-дубли)")
    args = ap.parse_args()

    if args.out is None:
        args.out = str(Path(__file__).resolve().parent / "dataset" / "background")

    os.makedirs(args.out, exist_ok=True)
    n = count_imgs(args.out)

    sct = mss.mss()
    mon = sct.monitors[1]  # основной монитор
    mw, mh = mon["width"], mon["height"]
    fov = min(args.fov, mw, mh)
    region = {
        "left":   mon["left"] + (mw - fov) // 2,
        "top":    mon["top"]  + (mh - fov) // 2,
        "width":  fov,
        "height": fov,
    }

    print(f"[bg] FOV {fov}x{fov} по центру монитора")
    print(f"[bg] сохранение -> {os.path.abspath(args.out)} (уже {n} кадров)")
    if args.mode == "auto":
        print(f"[bg] AUTO: беги по пустым картам, крути камерой. q=выход")
    else:
        print(f"[bg] MANUAL: s=сохранить, q=выход")

    last_img = None
    last_save = 0.0

    while True:
        # mss отдаёт BGRA; берём первые 3 канала -> BGR (то, что ждёт YOLO)
        img = np.asarray(sct.grab(region))[:, :, :3]

        disp = img.copy()
        cv2.putText(disp, f"background | saved {n}", (12, 32),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
        cv2.putText(disp, "auto: беги/крути камеру  |  manual: s=снимок  |  q=выход",
                    (12, 62), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 1)
        cv2.imshow("collect_background", disp)
        key = cv2.waitKey(1) & 0xFF

        now_ms = time.time() * 1000.0
        do_save = (key == ord("s")) if args.mode == "manual" \
            else ((now_ms - last_save) >= args.interval)

        if do_save:
            # анти-дубли: пропускаем почти одинаковые кадры (когда стоишь на месте)
            if last_img is not None:
                diff = float(np.mean(
                    np.abs(img.astype(np.int16) - last_img.astype(np.int16))))
            else:
                diff = 1e9

            if diff >= args.min_diff:
                path = os.path.join(args.out, f"bg_{n:06d}.jpg")
                cv2.imwrite(path, img, [cv2.IMWRITE_JPEG_QUALITY, 92])
                last_img = img.copy()
                n += 1
                print(f"  saved bg_{n - 1:06d}  (diff={diff:.1f}, всего {n})")
            last_save = now_ms

        if key in (ord("q"), 27):  # 27 = Esc
            break

    cv2.destroyAllWindows()
    print(f"[bg] готово. Всего в {os.path.abspath(args.out)}: {count_imgs(args.out)} кадров")


if __name__ == "__main__":
    main()
