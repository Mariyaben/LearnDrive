# Demo data

All community locations, clinic locations, names, and population figures in this folder are **synthetic**. They are not actual census records or an inventory of real clinics. Coordinates lie near Bengaluru only to illustrate use of WGS 84 / UTM zone 43N (EPSG:32643).

- `communities.geojson`: 36 fictional points with name, ID, population and source label.
- `facilities.geojson`: four fictional clinics with name, ID and source label.
- `projected_fixture.json`: exact generated metre coordinates used for reference calculations.

GeoJSON coordinates are longitude/latitude in WGS 84 (RFC 7946). The fixture uses EPSG:32643 and is not GeoJSON. Regenerate with `tools/build_demo.py`, which uses pyproj for coordinate transformations. No network data are downloaded.

To the extent legally possible, the creators dedicate the synthetic data to the public domain under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). Code is licensed separately under GPL-3.0-or-later.
