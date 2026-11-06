"""Create an integrity-checked local .ldnav pack from prepared offline data."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import zipfile


def package(output, tiles, basemap, places, region_id, name, bounds, source, search_db=None):
    if output.exists():
        raise ValueError("Output exists; choose a new versioned filename")
    if len(bounds) != 4 or not (-180 <= bounds[0] < bounds[2] <= 180 and -85 <= bounds[1] < bounds[3] <= 85):
        raise ValueError("Invalid coverage bounds")
    with basemap.open("rb") as stream:
        header = stream.read(127)
    if len(header) != 127 or header[:8] != b"PMTiles\x03" or header[99] != 1:
        raise ValueError("Expected a PMTiles v3 vector archive")
    with tarfile.open(tiles) as tar:
        count = 0
        for member in tar:
            p = Path(member.name)
            if not member.isfile() or p.is_absolute() or ".." in p.parts or p.suffix != ".gph":
                raise ValueError("Expected graph-only tar with safe relative .gph names")
            count += 1
        if not count:
            raise ValueError("Empty routing graph")
    payload = {"routing/tiles.tar": tiles, "map.pmtiles": basemap}
    if search_db:
        import sqlite3
        with search_db.open('rb') as stream:
            if stream.read(16)!=b'SQLite format 3\x00' or search_db.stat().st_size>96*1024*1024:raise ValueError('Invalid destination database')
        with sqlite3.connect(search_db.as_uri()+'?mode=ro',uri=True) as db:
            if db.execute('PRAGMA application_id').fetchone()[0]!=0x4C444E56:raise ValueError('Wrong destination schema')
            if db.execute('PRAGMA integrity_check').fetchone()[0]!='ok':raise ValueError('Broken destination index')
        payload['places.sqlite']=search_db
    if places:
        rows = json.loads(places.read_text(encoding="utf-8"))
        if not isinstance(rows, list) or len(rows) > 500000 or places.stat().st_size > 96*1024*1024:
            raise ValueError("Invalid places index")
        payload["places.json"] = places
    files = {name: {"bytes": path.stat().st_size,
                    "sha256": hashlib.file_digest(path.open("rb"), "sha256").hexdigest()}
             for name, path in payload.items()}
    if sum(v["bytes"] for v in files.values()) > 2*1024*1024*1024:
        raise ValueError("Pack exceeds current mobile 2 GiB import limit")
    metadata = {"schema_version": 1, "id": region_id, "name": name, "bounds": bounds,
                "routing_engine": "valhalla-3.9.1", "map_schema": "protomaps", "places_schema": 3 if search_db else (2 if places and any("kind" in row for row in rows) else 1),
                "attribution": "© OpenStreetMap contributors · ODbL-1.0; Protomaps basemap",
                "source": source, "coverage_note": "Bounds describe a coverage envelope; graph availability is checked by the local router. Border routes may require a larger pack.",
                "files": files}
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as z:
        z.writestr("manifest.json", json.dumps(metadata, ensure_ascii=False, indent=2))
        for filename, path in payload.items():
            z.write(path, filename)
    report = {"file": str(output), "bytes": output.stat().st_size,
              "sha256": hashlib.file_digest(output.open("rb"), "sha256").hexdigest(), "manifest": metadata}
    output.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--tiles", type=Path, required=True)
    p.add_argument("--map", type=Path, required=True)
    p.add_argument("--places", type=Path)
    p.add_argument("--search-db",type=lambda value:Path(value).resolve())
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--id", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--bounds", nargs=4, type=float, required=True)
    p.add_argument("--source", required=True)
    a = p.parse_args()
    report = package(a.output, a.tiles, a.map, a.places, a.id, a.name, a.bounds, a.source,a.search_db)
    print(json.dumps({k: report[k] for k in ("file", "bytes", "sha256")}))


if __name__ == "__main__":
    main()
