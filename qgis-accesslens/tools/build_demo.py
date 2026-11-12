"""Regenerate fictional data, reference results and report (requires pyproj)."""
import csv
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from accesslens.core import Demand, Facility, analyze
from accesslens.report import render_report


def build():
    from pyproj import Transformer

    to_wgs84 = Transformer.from_crs(32643, 4326, always_xy=True)
    communities = [
        Demand(f"C{r*6+c+1:02}", f"Community {r*6+c+1:02}",
               777000 + c * 800 + (r % 2) * 120,
               1433000 + r * 750, 120 + ((r * 271 + c * 173) % 880))
        for r in range(6) for c in range(6)
    ]
    facilities = [
        Facility("F01", "West clinic (fictional)", 777450, 1433550),
        Facility("F02", "Central clinic (fictional)", 779200, 1434400),
        Facility("F03", "North clinic (fictional)", 777850, 1436100),
        Facility("F04", "East clinic (fictional)", 780850, 1435900),
    ]
    provenance = "Fictional Bengaluru-area planning demo — synthetic communities and clinics"
    data = ROOT / "accesslens" / "data"
    data.mkdir(parents=True, exist_ok=True)
    for filename, records in [("communities.geojson", communities), ("facilities.geojson", facilities)]:
        features = []
        for record in records:
            lon, lat = to_wgs84.transform(record.x, record.y)
            properties = {"id": record.id, "name": record.name, "data_source": "synthetic"}
            if isinstance(record, Demand):
                properties["population"] = record.population
            features.append({"type": "Feature", "properties": properties,
                             "geometry": {"type": "Point", "coordinates": [lon, lat]}})
        (data / filename).write_text(json.dumps({"type": "FeatureCollection", "features": features}, indent=2) + "\n", encoding="utf-8")
    fixture = {"crs": "EPSG:32643", "provenance": provenance,
               "communities": [vars(row) for row in communities],
               "facilities": [vars(row) for row in facilities]}
    (data / "projected_fixture.json").write_text(json.dumps(fixture, indent=2) + "\n", encoding="utf-8")
    result = analyze(communities, facilities, 1000)
    output = ROOT / "demo"
    output.mkdir(exist_ok=True)
    (output / "access-report.html").write_text(render_report(result, "EPSG:32643", provenance), encoding="utf-8")
    (output / "expected-summary.json").write_text(json.dumps(result.summary(), indent=2) + "\n", encoding="utf-8")
    with (output / "community-access.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["community_id", "community", "population", "facility_id", "distance_m", "served", "priority_person_km", "gap_rank"])
        for row in result.rows:
            writer.writerow([row.demand.id, row.demand.name, row.demand.population,
                             row.facility.id, round(row.distance_m, 6), int(row.served),
                             round(row.priority, 6), row.gap_rank])
    print(json.dumps(result.summary(), indent=2))


if __name__ == "__main__":
    build()
