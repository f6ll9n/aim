# ============================================================================
#  collect_data.py — сбор датасета + АВТОХОДЬБА (WASD) прямо во время игры.
#
#  Захватывает центральную квадратную зону (FOV, 640x640 — ровно то, что видит
#  модель) и умеет сам водить персонажа, чтобы набирать футаж без рук.
#
#  Горячие клавиши (работают глобально, даже когда фокус на игре):
#    F4           — кадр в папку «люди»   (с врагами/союзниками)
#    F5           — кадр в папку «фон»    (пустой кадр)
#    F6           — автосъёмка фона вкл/выкл (каждые --interval мс, анти-дубли)
#    F7           — АВТОХОДЬБА WASD вкл/выкл (W вперёд + стрейфы A/D + прыжки)
#    Esc          — выход
#
#  ВАЖНО для автоходьбы:
#   * Скрипт инжектит WASD через SendInput -> клавиши идут в окно, у которого
#     фокус. Поэтому ИГРА должна быть в фокусе. Запускай с --no-preview и после
#     старта кликни в окно игры.
#   * Кадры «фона» не требуют разметки (train.py сам добавит пустой .txt).
#
#  Примеры:
#    python collect_data.py --no-preview
#    python collect_data.py --fov 640 --interval 150 --min-diff 4
# ============================================================================

import argparse
import ctypes
import os
import random
import time
from pathlib import Path

import mss
import numpy as np
import cv2


# ---------------------------------------------------------------------------
#  WinAPI-ввод: SendInput (инъекция WASD) + GetAsyncKeyState (чтение хоткеев)
# ---------------------------------------------------------------------------
VK_W = 0x57
VK_A = 0x41
VK_S = 0x53
VK_D = 0x44
VK_SPACE = 0x20
VK_F4 = 0x73
VK_F5 = 0x74
VK_F6 = 0x75
VK_F7 = 0x76
VK_ESC = 0x1B

KEYEVENTF_KEYUP = 0x0002
_user32 = ctypes.windll.user32


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", ctypes.c_ushort),
        ("wScan", ctypes.c_ushort),
        ("dwFlags", ctypes.c_ulong),
        ("time", ctypes.c_ulong),
        ("dwExtraInfo", ctypes.c_ulonglong),
    ]


class INPUTUNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_ulong), ("u", INPUTUNION)]


def send_key(vk: int, up: bool) -> None:
    kb = KEYBDINPUT(vk, 0, KEYEVENTF_KEYUP if up else 0, 0, 0)
    x = INPUT(1, INPUTUNION(ki=kb))
    _user32.SendInput(1, ctypes.byref(x), ctypes.sizeof(INPUT))


def press(vk: int) -> None:
    send_key(vk, False)


def release(vk: int) -> None:
    send_key(vk, True)


def tap(vk: int, hold: float = 0.08) -> None:
    press(vk)
    time.sleep(hold)
    release(vk)


def is_down(vk: int) -> bool:
    return (_user32.GetAsyncKeyState(vk) & 0x8000) != 0


# ---------------------------------------------------------------------------
#  Автоходьба: W вперёд + периодические стрейфы A/D + прыжки (случайные).
# ---------------------------------------------------------------------------
class AutoMover:
    def __init__(self):
        self.w_down = False
        self.strafe_vk = 0
        self.strafe_until = 0.0
        self.next_turn = 0.0
        self.next_jump = 0.0

    def start(self) -> None:
        press(VK_W)
        self.w_down = True
        self.strafe_vk = 0
        self.strafe_until = 0.0
        self.next_turn = time.time() + random.uniform(1.5, 3.5)
        self.next_jump = time.time() + random.uniform(2.0, 5.0)

    def stop(self) -> None:
        if self.w_down:
            release(VK_W)
            self.w_down = False
        if self.strafe_vk:
            release(self.strafe_vk)
            self.strafe_vk = 0

    def step(self, now: float) -> None:
        if self.strafe_vk:
            if now >= self.strafe_until:
                release(self.strafe_vk)
                self.strafe_vk = 0
                self.next_turn = now + random.uniform(1.5, 3.5)
        else:
            if now >= self.next_turn:
                self.strafe_vk = random.choice((VK_A, VK_D))
                press(self.strafe_vk)
                self.strafe_until = now + random.uniform(0.7, 1.5)
        if now >= self.next_jump:
            tap(VK_SPACE, 0.08)
            self.next_jump = now + random.uniform(2.5, 6.0)


# ---------------------------------------------------------------------------
#  Съёмка
# ---------------------------------------------------------------------------
def count_imgs(folder: str) -> int:
    exts = (".jpg", ".jpeg", ".png")
    if not os.path.isdir(folder):
        return 0
    return len([f for f in os.listdir(folder) if f.lower().endswith(exts)])


def save(img, folder: str, prefix: str, n: int) -> int:
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, f"{prefix}_{n:06d}.jpg")
    cv2.imwrite(path, img, [cv2.IMWRITE_JPEG_QUALITY, 95])
    return n + 1


def main() -> None:
    base = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description="Collect YOLO dataset + auto-move (WASD)")
    ap.add_argument("--people", default=str(base / "dataset" / "people"))
    ap.add_argument("--background", default=str(base / "dataset" / "background"))
    ap.add_argument("--fov", type=int, default=640, help="размер центрального кропа")
    ap.add_argument("--interval", type=int, default=200,
                    help="мс между авто-кадрами фона (F6)")
    ap.add_argument("--min-diff", type=float, default=6.0,
                    help="мин. разница между авто-кадрами (анти-дубли)")
    ap.add_argument("--no-preview", action="store_true",
                    help="без окна превью (нужно для автоходьбы: фокус на игре)")
    args = ap.parse_args()

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

    np_ = count_imgs(args.people)
    nb = count_imgs(args.background)

    print(f"[collect] FOV {fov}x{fov} по центру монитора")
    print(f"[collect]   F4=люди ({np_})   F5=фон ({nb})")
    print(f"[collect]   F6=автосъёмка   F7=АВТОХОДЬБА   Esc=выход")
    if args.no_preview:
        print("[collect] без превью: кликни в окно игры, жми F7 — пойдёт сам.")

    mover = AutoMover()
    auto_move = False
    auto_cap = False
    last_save = 0.0
    last_bg_img = None
    key_prev = {}

    def edge(vk: int) -> bool:
        cur = is_down(vk)
        p = key_prev.get(vk, False)
        key_prev[vk] = cur
        return cur and not p

    try:
        while True:
            # mss отдаёт BGRA; первые 3 канала -> BGR (то, что ждёт YOLO)
            img = np.asarray(sct.grab(region))[:, :, :3]

            if args.no_preview:
                time.sleep(0.01)
            else:
                disp = img.copy()
                status = f"people[{np_}] bg[{nb}]"
                status += "  MOVE" if auto_move else ""
                status += "  CAP" if auto_cap else ""
                cv2.putText(disp, status, (10, 28),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                cv2.putText(disp, "F4 люд | F5 фон | F6 съёмка | F7 ходьба | Esc",
                            (10, 54), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 1)
                cv2.imshow("collect_data", disp)
                cv2.waitKey(1)

            now = time.time()
            if auto_move:
                mover.step(now)

            # --- горячие клавиши ---
            if edge(VK_F4):
                np_ = save(img, args.people, "frame", np_)
                print(f"  люди -> frame_{np_ - 1:06d}  (всего {np_})")
            if edge(VK_F5):
                nb = save(img, args.background, "bg", nb)
                print(f"  фон  -> bg_{nb - 1:06d}  (всего {nb})")
            if edge(VK_F6):
                auto_cap = not auto_cap
                print(f"  автосъёмка фона: {'ON' if auto_cap else 'OFF'}")
            if edge(VK_F7):
                auto_move = not auto_move
                if auto_move:
                    mover.start()
                    print("  АВТОХОДЬБА: ON (W вперёд + стрейфы + прыжки)")
                else:
                    mover.stop()
                    print("  АВТОХОДЬБА: OFF")
            if edge(VK_ESC):
                break

            # --- автосъёмка фона ---
            if auto_cap and (now - last_save) >= args.interval / 1000.0:
                diff = 1e9
                if last_bg_img is not None:
                    diff = float(np.mean(
                        np.abs(img.astype(np.int16) - last_bg_img.astype(np.int16))))
                if diff >= args.min_diff:
                    nb = save(img, args.background, "bg", nb)
                    last_bg_img = img.copy()
                    print(f"  авто-фон -> bg_{nb - 1:06d}  (diff={diff:.1f})")
                last_save = now
    finally:
        mover.stop()
        cv2.destroyAllWindows()

    print(f"[collect] готово. люди={np_}, фон={nb}")


if __name__ == "__main__":
    main()
