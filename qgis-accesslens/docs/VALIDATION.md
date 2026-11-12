# Validation record

Recorded **26 October 2026**.

| Check | Status | Evidence |
| --- | --- | --- |
| Calculation and report suite | PASS | 19 tests with Python 3.11.0 using `python -m unittest discover -s tests -p "test_*.py" -v` |
| Python syntax | PASS | `python -m compileall -q accesslens tools tests` |
| Demo generation | PASS | 36 community and four facility features; pyproj transformation from EPSG:32643 to EPSG:4326 |
| Report visual inspection | PASS | Inspected in Chromium; full-page screenshot in `report-preview.png`; no horizontal overflow at desktop viewport |
| QGIS installation / GUI | NOT RUN | No QGIS or PyQGIS runtime available in Windows or the checked Ubuntu environment |
| QGIS integration suite | NOT RUN | Ten integration tests supplied in `tests/qgis_integration.py`; require QGIS's Python environment |
| QGIS 4 compatibility | NOT CLAIMED | Plugin metadata caps compatibility below 4 |
| Performance benchmark | NOT RUN | No scalability or speed claim |

## Reproducible demo results

At a 1,000 m threshold in EPSG:32643:

| Metric | Reference value |
| --- | --- |
| Communities | 36 |
| Facilities | 4 |
| Total synthetic population | 19,640 |
| Served population | 10,183 |
| Underserved population | 9,457 |
| Population coverage | 51.8482688391% |
| Underserved communities | 15 |
| Population-weighted mean distance | 1,030.4460391058 m |
| Maximum nearest-facility distance | 2,280.3508501983 m |

The reference CSV/summary/report use exact projected fixture coordinates and the independent calculation core. The plugin demo reprojects the committed WGS 84 GeoJSON back to the local CRS and uses QGIS's R-tree; the integration suite checks agreement within floating-point transformation tolerance. Runtime agreement has not yet been observed.

Calculation/report tests cover inclusive distance boundaries, population weighting, excess-distance ranking, ties, zero population, invalid values, empty inputs, duplicate IDs, callback validation, cancellation, monotone threshold scenarios, randomized nearest-distance invariants, demo reproducibility, HTML escaping, and SVG validity.

## QGIS acceptance checklist

Run in QGIS **3.44**, record its exact version/OS, and update this record with observed results:

- [ ] Install the ZIP through Plugin Manager without metadata/import errors.
- [ ] Enable AccessLens; confirm its menu, toolbar action, and Processing provider appear.
- [ ] Run `tests/qgis_integration.py` in QGIS's configured Python environment; all ten tests must pass.
- [ ] Load the fictional demo without clearing an existing project.
- [ ] Confirm 36 result points, 36 connection features, and four radius polygons.
- [ ] Inspect styles, zoom extent, report opening, source labels, and population fields.
- [ ] Save real-data results to GeoPackage and reopen all three outputs.
- [ ] Check a mixed-source-CRS run; verify metres against a known local distance.
- [ ] Reject geographic/foot-based/EPSG:3857 analysis CRSs and missing/negative population.
- [ ] Cancel a run; verify it is not presented as success and discard partial outputs.
- [ ] Export demo temporary results, save a `.qgz` project, close/reopen it, and verify persistence.
- [ ] Disable/re-enable the plugin; verify the actions/provider are removed and restored cleanly.

Do not treat successful plain-Python tests or a rendered HTML report as evidence that these unchecked QGIS steps passed.
