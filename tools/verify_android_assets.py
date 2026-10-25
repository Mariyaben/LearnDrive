"""Check bundled ONNX models, recorded prompts, and optionally their APK copies."""
import argparse
import hashlib
import json
from pathlib import Path
import wave
import zipfile
import re
import struct
import numpy as np
import onnxruntime as ort

def elf_runtime_sections(binary):
    """Compare loaded ELF contents while allowing Gradle to strip debug symbols."""
    assert binary[:6] == b"\x7fELF\x02\x01", "Expected little-endian ELF64"
    section_offset = struct.unpack_from("<Q", binary, 40)[0]
    entry_size, count, names_index = struct.unpack_from("<HHH", binary, 58)
    sections = [struct.unpack_from("<IIQQQQIIQQ", binary, section_offset + i * entry_size)
                for i in range(count)]
    names_section = sections[names_index]
    names = binary[names_section[4]:names_section[4] + names_section[5]]
    loaded = {}
    for name, kind, flags, address, offset, size, *_ in sections:
        if flags & 2:  # SHF_ALLOC; debug and ordinary symbol tables are excluded.
            label = names[name:names.index(b"\0", name)].decode("ascii")
            content = b"" if kind == 8 else binary[offset:offset + size]  # SHT_NOBITS
            loaded[label] = (address, size, hashlib.sha256(content).hexdigest())
    assert loaded, "ELF has no loadable sections"
    return loaded

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--apk", type=Path)
parser.add_argument("--image", type=Path, help="Official Ultralytics bus.jpg for a real detection smoke check")
args = parser.parse_args()
assets = Path(__file__).resolve().parents[1] / "android/app/src/main/assets"
project = assets.parents[4]
native_text = (project / "core/include/learndrive/types.hpp").read_text(encoding="utf-8")
prompt_count = int(re.search(r"promptCount=(\d+)", native_text).group(1))
enum_text = re.search(r"enum class Prompt.*?\{(.*?)\};", native_text, re.S).group(1)
ids = {name: int(value) for name, value in re.findall(r"(\w+)=(\d+)", enum_text)}
native_phrases = {ids[name]: phrase for name, phrase in re.findall(r'case Prompt::(\w+): return "([^"]*)";', native_text)}
render_script = (project / "tools/render_android_prompts.ps1").read_text(encoding="utf-8")
render_phrases = re.findall(r"'([^']*)'", re.search(r"\$phrases = @\((.*?)\)\s*for", render_script, re.S).group(1))
assert render_phrases == [native_phrases[i] for i in range(1, prompt_count + 1)], "Recorded text differs from native prompt IDs"
manifest = json.loads((assets / "model-manifest.json").read_text(encoding="utf-8"))
spatial_manifest=json.loads((assets/"supercombo-manifest.json").read_text(encoding="utf-8"))
assert hashlib.sha256((assets/"supercombo.onnx").read_bytes()).hexdigest()==spatial_manifest["sha256"]
assert (assets/"COMMA-LICENSE.txt").is_file()
print("Verified pinned Comma.ai Supercombo asset and license")
options = ort.SessionOptions()
options.intra_op_num_threads = 2
options.inter_op_num_threads = 1
sessions = {}
for name, expected in manifest["assets"].items():
    path = assets / name
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected["sha256"], name
    session = ort.InferenceSession(str(path), options, providers=["CPUExecutionProvider"])
    assert len(session.get_inputs()) == 1
    assert session.get_inputs()[0].name == "images"
    assert session.get_inputs()[0].shape == [1, 3, 320, 320]
    actual = {out.name: out.shape for out in session.get_outputs()}
    assert actual == expected["outputs"], actual
    sessions[name] = session
    print("Verified", name, actual)
formats = set()
for path in sorted((assets / "audio").glob("prompt-*.wav")):
    with wave.open(str(path), "rb") as clip:
        formats.add((clip.getnchannels(), clip.getframerate(), clip.getsampwidth()))
        values = np.frombuffer(clip.readframes(clip.getnframes()), dtype="<i2")
        assert clip.getcomptype() == "NONE" and clip.getnframes() > clip.getframerate() // 2
        assert np.max(np.abs(values.astype(np.int32))) > 1000, "Silent recording"
        print("Verified voice", path.name, "%.2f seconds" % (clip.getnframes() / clip.getframerate()))
assert formats == {(1, 22050, 2)}
assert {path.name for path in (assets / "audio").glob("prompt-*.wav")} == {f"prompt-{i}.wav" for i in range(1, prompt_count + 1)}
if args.image:
    import cv2
    image = cv2.cvtColor(cv2.imread(str(args.image)), cv2.COLOR_BGR2RGB)
    height, width = image.shape[:2]
    ratio = 320 / max(height, width)
    w, h = round(width * ratio), round(height * ratio)
    padded = np.full((320, 320, 3), 114, dtype=np.uint8)
    x, y = (320-w)//2, (320-h)//2
    padded[y:y+h, x:x+w] = cv2.resize(image, (w, h))
    batch = np.ascontiguousarray(padded.transpose(2, 0, 1)[None], dtype=np.float32) / 255
    detections = sessions["detector.onnx"].run(None, {"images": batch})[0][0]
    scores, categories = detections[4:].max(axis=0), detections[4:].argmax(axis=0)
    found = set(categories[scores >= .4].tolist())
    assert 0 in found and 5 in found, ("Expected person and bus", found)
    masks = sessions["road.onnx"].run(None, {"images": batch})
    assert all(np.isfinite(mask).all() for mask in masks)
    print("Real image: person and bus detected; road model produced finite masks")
if args.apk:
    with zipfile.ZipFile(args.apk) as archive:
        for path in assets.rglob("*"):
            if path.is_file() and path.name != ".gitkeep":
                name = "assets/" + path.relative_to(assets).as_posix()
                assert archive.read(name) == path.read_bytes(), name
        for abi in ("arm64-v8a", "x86_64"):
            assert f"lib/{abi}/liblearndrive.so" in archive.namelist()
            assert f"lib/{abi}/libonnxruntime.so" in archive.namelist()
            assert archive.namelist().count(f"lib/{abi}/libonnxruntime.so")==1,"Duplicate ORT runtime"
            with zipfile.ZipFile(project/".tools/maven-repo/com/microsoft/onnxruntime/onnxruntime-android/1.22.0/onnxruntime-android-1.22.0.aar") as runtime:
                assert elf_runtime_sections(archive.read(f"lib/{abi}/libonnxruntime.so")) == elf_runtime_sections(runtime.read(f"jni/{abi}/libonnxruntime.so")), "Native/Java ORT loadable contents mismatch"
        print("APK contains matching models, voices, notices and both native runtimes")
