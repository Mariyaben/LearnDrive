"""Optional Maven Central prefetch for environments with slow Java networking."""
import hashlib
from pathlib import Path
import urllib.request
import zipfile

root = Path(__file__).resolve().parents[1]
relative = "com/microsoft/onnxruntime/onnxruntime-android/1.22.0"
directory = root / ".tools/maven-repo" / relative
directory.mkdir(parents=True, exist_ok=True)
base = "https://repo.maven.apache.org/maven2/" + relative + "/"
for extension in ("pom", "aar"):
    name = "onnxruntime-android-1.22.0." + extension
    path = directory / name
    if not path.exists():
        with urllib.request.urlopen(base + name, timeout=60) as response:
            path.write_bytes(response.read())
    print(name, path.stat().st_size, flush=True)
aar = directory / "onnxruntime-android-1.22.0.aar"
with zipfile.ZipFile(aar) as archive:
    assert archive.testzip() is None, "Incomplete Maven artifact"
print("Maven Central artifact CRC checked; SHA256", hashlib.sha256(aar.read_bytes()).hexdigest(), flush=True)
