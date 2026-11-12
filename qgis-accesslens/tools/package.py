"""Build deterministic QGIS-installable and complete source ZIPs."""
from pathlib import Path
import hashlib
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {".git", ".venv", "__pycache__", ".pytest_cache", "dist", ".build-deps"}


def files(folder):
    return sorted(path for path in folder.rglob("*") if path.is_file()
                  and not any(part in EXCLUDED for part in path.relative_to(ROOT).parts)
                  and path.suffix not in {".pyc", ".pyo"})


def archive(target, paths, base, prefix=""):
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as stream:
        for path in paths:
            name = prefix + path.relative_to(base).as_posix()
            entry = zipfile.ZipInfo(name, date_time=(2026, 10, 26, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16
            stream.writestr(entry, path.read_bytes())


def build():
    target = ROOT / "dist"
    target.mkdir(exist_ok=True)
    plugin = target / "accesslens-0.1.0.zip"
    source = target / "accesslens-portfolio-0.1.0.zip"
    archive(plugin, files(ROOT / "accesslens"), ROOT)
    archive(source, files(ROOT), ROOT, "qgis-accesslens/")
    hashes = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in (plugin, source))
    (target / "SHA256SUMS.txt").write_text(hashes, encoding="utf-8")
    print(hashes)


if __name__ == "__main__":
    build()
