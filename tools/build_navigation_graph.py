"""Build a local Valhalla 3.9.1 graph. Run with the pinned pyvalhalla wheel.

No server is started. On Windows the Linux wheel can run in an existing WSL
Ubuntu/Python 3.12 environment; all output paths must be in the workspace.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--kerala-country-parent", action="store_true")
    args = parser.parse_args()
    import valhalla
    from valhalla.valhalla_build_config import config as defaults
    from valhalla.config import _sanitize_config
    import copy

    if not valhalla.__version__.startswith("3.9.1"):
        raise RuntimeError("Expected pinned Valhalla 3.9.1")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tiles = out / "tiles"
    tiles.mkdir(exist_ok=True)
    archive = out / "tiles.tar"
    config = _sanitize_config(copy.deepcopy(defaults))
    config["mjolnir"].update(tile_dir=str(tiles), tile_extract="", concurrency=args.threads,
                             admin=str(out / "admins.sqlite"), timezone="")
    config["logging"] = {"type": "std_out", "color": False}
    file = out / "build-config.json"
    file.write_text(json.dumps(config), encoding="utf-8")
    binaries = Path(valhalla.__file__).parent / "bin"
    start = time.monotonic()
    for tool in ("valhalla_build_admins", "valhalla_build_tiles"):
        executable = binaries / tool
        executable.chmod(executable.stat().st_mode | 0o111)
        subprocess.run([str(executable), "-c", str(file), str(args.source.resolve())], check=True)
        if tool == "valhalla_build_admins" and args.kerala_country_parent:
            from navigation_country_admin import kerala_parent
            kerala_parent(out / "admins.sqlite", Path(valhalla.__file__).parent.parent)
    # Sorted, regular graph tiles only; no symlinks, absolute names or arbitrary
    # build files in the on-device graph extract. Valhalla supports unindexed tar.
    graph_files = sorted(tiles.rglob("*.gph"))
    if not graph_files:
        raise RuntimeError("Graph builder produced no routing tiles")
    with tarfile.open(archive, "w", format=tarfile.USTAR_FORMAT) as tar:
        for path in graph_files:
            if path.is_symlink():
                raise ValueError("Symlink in graph output")
            info=tar.gettarinfo(str(path),arcname=path.relative_to(tiles).as_posix())
            info.mtime=0;info.uid=info.gid=0;info.uname=info.gname="";info.mode=0o644
            with path.open("rb") as stream:tar.addfile(info,stream)
    config["mjolnir"]["tile_extract"] = str(archive)
    config["mjolnir"]["tile_dir"] = ""
    (out / "route-config.json").write_text(json.dumps(config), encoding="utf-8")
    report = {"engine": valhalla.__version__, "source_bytes": args.source.stat().st_size,
              "source_sha256": hashlib.file_digest(args.source.open("rb"), "sha256").hexdigest(),
              "graph_tiles": len(graph_files), "archive_bytes": archive.stat().st_size,
              "archive_sha256": hashlib.file_digest(archive.open("rb"), "sha256").hexdigest(),
              "build_seconds": time.monotonic() - start}
    (out / "build-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
