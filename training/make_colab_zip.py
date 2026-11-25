# ============================================================================
#  make_colab_zip.py — собирает aim_colab.zip для обучения на Google Colab.
#
#  Кладёт в архив ТОЛЬКО:
#    * размеченные позитивы (people/*.jpg у которых есть .txt рядом)
#    * все негативы (background/*.jpg)
#    * train.py + dataset/data.yaml + dataset/classes.txt
#
#  Неразмеченные кадры из people/ сознательно НЕ попадают в архив —
#  иначе YOLO посчитал бы их «пустыми» (негативами) и испортил модель.
#
#  После каждого дообучения/досъёма просто перезапусти:
#    python make_colab_zip.py
# ============================================================================

import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATASET = ROOT / "dataset"
STAGE  = ROOT / "_colab_build" / "aim_dataset"
ZIP    = ROOT / "aim_colab.zip"

EXTS = (".jpg", ".jpeg", ".png")


def main() -> None:
    shutil.rmtree(STAGE.parent, ignore_errors=True)
    (STAGE / "dataset" / "people").mkdir(parents=True)
    (STAGE / "dataset" / "background").mkdir(parents=True)

    # служебные файлы
    shutil.copy2(ROOT / "train.py", STAGE / "train.py")
    shutil.copy2(DATASET / "data.yaml", STAGE / "dataset" / "data.yaml")
    shutil.copy2(DATASET / "classes.txt", STAGE / "dataset" / "classes.txt")

    # размеченные позитивы (jpg + matching .txt)
    people = DATASET / "people"
    labeled = [p for p in sorted(people.iterdir())
               if p.suffix.lower() in EXTS
               and (people / (p.stem + ".txt")).exists()]

    n_head = 0
    for p in labeled:
        shutil.copy2(p, STAGE / "dataset" / "people" / p.name)
        src_txt = people / (p.stem + ".txt")
        shutil.copy2(src_txt, STAGE / "dataset" / "people" / (p.stem + ".txt"))
        # считаем, сколько кадров содержит класс head (1)
        for line in src_txt.read_text().splitlines():
            parts = line.split()
            if parts and parts[0] == "1":
                n_head += 1
                break

    # негативы
    nbg = 0
    for p in sorted((DATASET / "background").iterdir()):
        if p.suffix.lower() in EXTS:
            shutil.copy2(p, STAGE / "dataset" / "background" / p.name)
            nbg += 1

    # архив
    if ZIP.exists():
        ZIP.unlink()
    with zipfile.ZipFile(ZIP, "w", zipfile.ZIP_DEFLATED) as z:
        for f in STAGE.rglob("*"):
            if f.is_file():
                z.write(f, f.relative_to(STAGE.parent))

    mb = ZIP.stat().st_size / 1e6
    print(f"[zip] позитивы (размечено): {len(labeled)}  (из них с классом head: {n_head})")
    print(f"[zip] негативы (background): {nbg}")
    print(f"[zip] -> {ZIP.name}: {mb:.1f} MB")


if __name__ == "__main__":
    main()
