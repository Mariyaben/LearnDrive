"""Create a tiny causal ONNX decoder to test real C++ ORT execution, not SLM reasoning."""
import sys
from pathlib import Path
import numpy as np
import onnx
from onnx import TensorProto as T, helper as h, numpy_helper as nh

response = '{"action_prompt":"Hold lane and distance","priority":2,"suppress_other_alerts":true}'
table = np.full((len(response) + 1, 128), -20, dtype=np.float32)
for i, token in enumerate([ord(c) for c in response] + [0]):
    table[i, token] = 20


def create(path, dtype):
    inputs = [h.make_tensor_value_info("input_ids", T.INT64, [1, "sequence"]),
              h.make_tensor_value_info("attention_mask", T.INT64, [1, "total"]),
              h.make_tensor_value_info("position_ids", T.INT64, [1, "sequence"])]
    inputs += [h.make_tensor_value_info(f"past_key_values.0.{part}", dtype, [1, 1, "past", 1])
               for part in ("key", "value")]
    outputs = [h.make_tensor_value_info("logits", dtype, [1, 1, 128])]
    outputs += [h.make_tensor_value_info(f"present.0.{part}", dtype, [1, 1, "total", 1])
                for part in ("key", "value")]
    nodes = [h.make_node("Shape", ["past_key_values.0.key"], ["cache_shape"]),
             h.make_node("Gather", ["cache_shape", "index"], ["past_length"], axis=0),
             h.make_node("Gather", ["table", "past_length"], ["selected_logits"], axis=0),
             h.make_node("Unsqueeze", ["selected_logits", "axes01"], ["float_logits"]),
             h.make_node("Cast", ["float_logits"], ["logits"], to=dtype),
             h.make_node("Cast", ["input_ids"], ["ids_float"], to=dtype),
             h.make_node("Unsqueeze", ["ids_float", "axes13"], ["cache_step"])]
    nodes += [h.make_node("Concat", [f"past_key_values.0.{part}", "cache_step"], [f"present.0.{part}"], axis=2)
              for part in ("key", "value")]
    constants = [nh.from_array(table, "table"), nh.from_array(np.array(2, dtype=np.int64), "index"),
                 nh.from_array(np.array([0, 1], dtype=np.int64), "axes01"),
                 nh.from_array(np.array([1, 3], dtype=np.int64), "axes13")]
    model = h.make_model(h.make_graph(nodes, "causal-test-fixture", inputs, outputs, constants),
                         opset_imports=[h.make_opsetid("", 17)], ir_version=9)
    onnx.checker.check_model(model)
    onnx.save(model, path)


path = Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
create(path, T.FLOAT)
create(str(path) + ".fp16.onnx", T.FLOAT16)
print(f"Created FP32/FP16 decoder fixtures at {path}")
