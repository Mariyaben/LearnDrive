"""Expose the SAME Maven AAR's C++ headers/library to CMake (no duplicate ORT)."""
from pathlib import Path
import zipfile
ROOT=Path(__file__).resolve().parents[1]
aar=ROOT/'.tools/maven-repo/com/microsoft/onnxruntime/onnxruntime-android/1.22.0/onnxruntime-android-1.22.0.aar'
target=ROOT/'android/ort-sdk'
with zipfile.ZipFile(aar) as archive:
    for name in archive.namelist():
        if name.startswith('headers/') or (name.startswith('jni/') and name.endswith('/libonnxruntime.so')):
            archive.extract(name,target)
print(target)
