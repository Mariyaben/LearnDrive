"""Validate local TFLite artifacts without downloading models or contacting APIs."""
import argparse
import hashlib
from pathlib import Path
import numpy as np
from ai_edge_litert.interpreter import Interpreter


def check(path: Path, detector: bool) -> None:
    runtime = Interpreter(model_path=str(path), num_threads=1)
    runtime.allocate_tensors()
    inputs, outputs = runtime.get_input_details(), runtime.get_output_details()
    if len(inputs) != 1 or list(inputs[0]["shape"]) != [1, 320, 320, 3]:
        raise ValueError(f"{path.name}: expected one NHWC [1,320,320,3] input")
    for tensor in inputs + outputs:
        dtype = tensor["dtype"]
        if dtype not in (np.float32, np.uint8, np.int8):
            raise ValueError(f"{tensor['name']}: unsupported dtype {dtype}")
        if dtype != np.float32:
            quant = tensor["quantization_parameters"]
            if len(quant["scales"]) != 1 or quant["scales"][0] <= 0:
                raise ValueError(f"{tensor['name']}: valid per-tensor quantization required")
    if detector:
        if len(outputs) != 1:
            raise ValueError("Detector must have one output")
        shape = list(outputs[0]["shape"])
        if len(shape) != 3 or shape[0] != 1 or 84 not in shape[1:]:
            raise ValueError("Expected raw YOLOv8 COCO output")
    else:
        if len(outputs) != 2:
            raise ValueError("Road model must have two outputs")
        a, b = (list(t["shape"]) for t in outputs)
        if a != b or len(a) != 4 or a[:2] != [1, 2]:
            raise ValueError("Expected matching [1,2,H,W] road/lane logits")
    print(path.name, hashlib.sha256(path.read_bytes()).hexdigest())
    for tensor in inputs + outputs:
        print(" ", tensor["name"], tensor["shape"].tolist(), tensor["dtype"].__name__, tensor["quantization"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    check(args.directory / "detector.tflite", True)
    check(args.directory / "road.tflite", False)
