"""Export the official pretrained models into offline Android assets.

Requires torch 2.1+, ultralytics 8.2.59, onnx, onnxruntime and numpy.
Downloads are build-time only; the Android app never downloads anything.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".tools/models"
ASSETS = ROOT / "android/app/src/main/assets"
CACHE.mkdir(parents=True, exist_ok=True)
ASSETS.mkdir(parents=True, exist_ok=True)
os.environ["YOLO_CONFIG_DIR"] = str(CACHE / "ultralytics")
os.environ["TORCH_HOME"] = str(CACHE / "torch")

SOURCES = {
    "TwinLite.py": "https://raw.githubusercontent.com/chequanghuy/TwinLiteNet/main/model/TwinLite.py",
    "best.pth": "https://raw.githubusercontent.com/chequanghuy/TwinLiteNet/main/pretrained/best.pth",
    "TwinLite-LICENSE.txt": "https://raw.githubusercontent.com/chequanghuy/TwinLiteNet/main/LICENSE",
    "yolov8n.pt": "https://github.com/ultralytics/assets/releases/download/v8.2.0/yolov8n.pt",
    "YOLO-LICENSE.txt": "https://raw.githubusercontent.com/ultralytics/ultralytics/v8.2.59/LICENSE",
}
for name, url in SOURCES.items():
    path = CACHE / name
    if not path.exists():
        print("Downloading", name, flush=True)
        with urllib.request.urlopen(url, timeout=60) as response:
            path.write_bytes(response.read())

import numpy as np
import onnx
import onnxruntime as ort
import torch
from ultralytics import YOLO

torch.set_num_threads(2)
detector_path = YOLO(str(CACHE / "yolov8n.pt")).export(
    format="onnx", imgsz=320, opset=17, simplify=False, dynamic=False, device="cpu"
)
(ASSETS / "detector.onnx").write_bytes(Path(detector_path).read_bytes())

spec = importlib.util.spec_from_file_location("TwinLite", CACHE / "TwinLite.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
road = module.TwinLiteNet().eval()
state = torch.load(CACHE / "best.pth", map_location="cpu", weights_only=True)
road.load_state_dict({key.removeprefix("module."): value for key, value in state.items()})
sample = torch.zeros(1, 3, 320, 320)
torch.onnx.export(road, sample, ASSETS / "road.onnx", opset_version=17,
                  input_names=["images"], output_names=["drivable", "lanes"],
                  do_constant_folding=True)

manifest = {"input": "Float32 NCHW RGB 0..1, letterboxed 320x320",
            "detector_coordinates": "320x320 pixel units", "sources": {}, "assets": {}}
for name, url in SOURCES.items():
    manifest["sources"][name] = {"url": url, "sha256": hashlib.sha256((CACHE / name).read_bytes()).hexdigest()}
for name in ("detector.onnx", "road.onnx"):
    path = ASSETS / name
    onnx.checker.check_model(onnx.load(path))
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    result = session.run(None, {"images": sample.numpy()})
    assert all(np.isfinite(value).all() for value in result)
    if name == "road.onnx":
        torch.manual_seed(0)
        comparison = torch.rand(1, 3, 320, 320)
        result_comparison = session.run(None, {"images": comparison.numpy()})
        reference = [value.detach().numpy() for value in road(comparison)]
        for actual, expected in zip(result_comparison, reference):
            np.testing.assert_allclose(actual, expected, atol=1e-3, rtol=1e-3)
    manifest["assets"][name] = {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                "outputs": {output.name: list(value.shape) for output, value in zip(session.get_outputs(), result)}}
    print(name, manifest["assets"][name], flush=True)
for name in ("TwinLite-LICENSE.txt", "YOLO-LICENSE.txt"):
    (ASSETS / name).write_bytes((CACHE / name).read_bytes())
(ASSETS / "model-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
