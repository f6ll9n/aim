# Обучение YOLO под Pixel Gun 3D (детекция игроков)

## Полный пайплайн

```
сбор кадров  →  разметка  →  train/val  →  обучение  →  ONNX  →  C++ (Ort::Value)
  collect.py     labelImg     train.py      ultralytics   best.onnx   твой aim
```

## 1. Сбор данных

```powershell
cd training
python collect.py                        # ручной: 's' сохранить, 'q' выход
python collect.py --mode auto --interval 300   # авто-запись каждые 300 мс
```

- Запускай **во время матча**, ходи/стреляй/меняй карты и скины — нужно разнообразие.
- Кадры падают в `dataset/images/`.
- Чем разнообразнее позы/ракурсы/освещение/карты — тем надёжнее детектор.

## 2. Разметка (главная работа — вручную)

Обвести каждую «модельку» прямоугольником. Проще всего через **labelImg**:

```powershell
pip install labelImg
labelImg dataset/images dataset/classes.txt
```

Создай `dataset/classes.txt` с одной строкой `player`.

- Формат экспорта — **YOLO** → в каждой строке `<class> <x_c> <y_c> <w> <h>` (в долях 0..1).
- Разметка `.txt` ляжет рядом с картинкой → **перенеси их в `dataset/labels/`** с тем же именем файла.

> Не хочется возиться с labelImg — можно разметить в облаке (Roboflow) и скачать в формате YOLO.

**Сколько нужно?** Для старта хватит 200–400 боксов (модель уже что-то поймает), для стабильности — 1000–2000+. Кадрируй так, чтобы модельки попадали в центр FOV (именно там они будут при наведении).

## 3. Обучение

**Вариант A — Google Colab (рекомендую, бесплатный GPU):**

1. Зипуй `dataset/` и залей на Colab.
2. В ячейке:
```bash
!pip install ultralytics
```
```python
from ultralytics import YOLO
model = YOLO("yolov8n.pt")
model.train(data="/content/dataset/data.yaml", epochs=100, imgsz=640, batch=16, device=0)
model.export(format="onnx", imgsz=640, opset=17)
```
3. Скачай `best.onnx`.

**Вариант B — локально на CPU (медленно, но без лишних телодвижений):**

```powershell
pip install ultralytics
python train.py --model yolov8n.pt --epochs 100 --imgsz 640 --batch 8 --device cpu
```

Модели: `yolov8n.pt` (маленькая/быстрая), `yolov11n.pt` (свежее и точнее на малых объектах) — скачаются автоматически.

## 4. Инференс в C++

`best.onnx` → твой конвейер. Вход сети: `1×3×640×640` float, NCHW, RGB, нормированный `x/255`. У тебя в `main.cpp` уже есть заготовка:

```cpp
cv::Mat blob = cv::dnn::blobFromImage(snap.mat, 1/255.0,
              cv::Size(640,640), cv::Scalar(), true, false); // BGR->RGB, HWC->CHW
Ort::Value x = Ort::Value::CreateTensor<float>(memInfo, (float*)blob.ptr(),
              blob.total(), shape, 4);
```

После инференса — выход `[1, 8400, 5]` (или `* , 4 + nc`): `x, y, w, h, confidence`. Постпроцессинг: фильтр по conf + NMS → боксы в пикселях FOV (0..640) → прибавь `DesktopCropRect().left/top`, чтобы получить экранные координаты для наведения.

## Советы по качеству

- `imgsz` должен совпадать с FOV захвата (640).
- Собери минимум пару карт, 2–3 скина, позы в беге/прыжке/прицеле.
- Если модельки мелкие — подними `imgsz` или используй `yolov8s`/`yolov11s` для более точного детекта (дороже по FPS).
