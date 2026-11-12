"""Check ZIP structure, source contents, checksums and required artifact links."""
import configparser
import hashlib
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def verify():
    dist = ROOT / "dist"
    names = ["accesslens-0.1.0.zip", "accesslens-portfolio-0.1.0.zip"]
    checksums = dict(line.split()[::-1] for line in (dist / "SHA256SUMS.txt").read_text().splitlines())
    for name in names:
        path = dist / name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == checksums[name]
        with zipfile.ZipFile(path) as archive:
            assert archive.testzip() is None
            assert not any("__pycache__" in member or ".build-deps" in member or member.endswith(".pyc") for member in archive.namelist())
            assert not any(member.startswith("/") or ".." in Path(member).parts for member in archive.namelist())
            if name == names[0]:
                assert {member.split("/")[0] for member in archive.namelist()} == {"accesslens"}
                for member in ("__init__.py", "metadata.txt", "LICENSE", "algorithm.py", "data/communities.geojson", "data/facilities.geojson"):
                    assert "accesslens/" + member in archive.namelist()
                metadata = configparser.ConfigParser()
                metadata.read_string(archive.read("accesslens/metadata.txt").decode("utf-8"))
                assert metadata["general"]["name"] == "AccessLens"
                assert metadata["general"].getboolean("hasProcessingProvider")
            else:
                for member in ("README.md", "docs/PORTFOLIO.md", "docs/VALIDATION.md", "docs/report-preview.png", "demo/access-report.html", "tests/qgis_integration.py"):
                    assert "qgis-accesslens/" + member in archive.namelist()
        print(f"PASS {name}: archive, structure and SHA-256 ({path.stat().st_size:,} bytes)")


if __name__ == "__main__":
    verify()
