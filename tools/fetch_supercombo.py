"""Fetch the pinned upstream model, verify its LFS hash, and retain provenance."""
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "6ca0269f47d048600335ac5776fc7966d9ca339f"
SHA256 = "2365bae967cce21ce68707c30bf2981bb7081ee5c3e6a3dff793e660f23ff622"
SIZE = 57554657
BASE = f"https://raw.githubusercontent.com/commaai/openpilot/{COMMIT}"
URL = f"https://media.githubusercontent.com/media/commaai/openpilot/{COMMIT}/models/supercombo.onnx"


def main():
    assets = ROOT / "android/app/src/main/assets"
    assets.mkdir(parents=True, exist_ok=True)
    model = assets / "supercombo.onnx"
    if not model.exists() or hashlib.sha256(model.read_bytes()).hexdigest() != SHA256:
        pending = model.with_suffix(".download")
        urllib.request.urlretrieve(URL, pending)
        if pending.stat().st_size != SIZE or hashlib.sha256(pending.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError("Upstream Supercombo size/SHA-256 mismatch")
        pending.replace(model)
    license_path = assets / "COMMA-LICENSE.txt"
    urllib.request.urlretrieve(f"{BASE}/LICENSE", license_path)
    provenance = ROOT / "models/comma-v0.8.13"
    provenance.mkdir(parents=True, exist_ok=True)
    for source in ("selfdrive/modeld/models/driving.h", "selfdrive/modeld/models/driving.cc",
                   "selfdrive/modeld/transforms/loadyuv.cl", "common/transformations/model.py",
                   "selfdrive/common/modeldata.h"):
        urllib.request.urlretrieve(f"{BASE}/{source}", provenance / Path(source).name)
    import onnx
    graph = onnx.load(model)
    onnx.checker.check_model(graph)
    def contract(value):
        return {"name": value.name, "shape": [int(d.dim_value) for d in value.type.tensor_type.shape.dim],
                "type": value.type.tensor_type.elem_type}
    manifest = {"upstream": "commaai/openpilot", "release": "v0.8.13", "commit": COMMIT,
                "url": URL, "sha256": SHA256, "bytes": SIZE, "license": "COMMA-LICENSE.txt",
                "inputs": [contract(v) for v in graph.graph.input],
                "outputs": [contract(v) for v in graph.graph.output]}
    (assets / "supercombo-manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
