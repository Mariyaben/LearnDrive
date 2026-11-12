# Architecture

```text
QGIS menu / toolbar / Processing toolbox / model / batch / Python
                              |
                    AccessLensProvider
                              |
                      AccessAlgorithm
          / reprojection / validation / point R-tree \
                          core.analyze
                     /                 \
        Point / line / polygon sinks   HTML + inline SVG
```

`core.py` contains immutable input/result records, distance classification, gap ranking, population-weighted summaries, and threshold comparisons. It accepts a nearest-facility callback so tests can run without PyQGIS while the application uses QGIS's R-tree. Its fallback search is intentionally a transparent small-data reference, not a performance claim.

`algorithm.py` validates inputs, transforms each point using the Processing context, indexes projected facilities, invokes the core, and checks every feature write. It uses separate sequential index IDs so provider feature ID conventions cannot cause an indexing mismatch. Labels are optional; source feature IDs remain available in outputs.

`report.py` creates an offline HTML document with an SVG schematic map, scale bar, grid north, service radii, population-scaled points, accessible map description, and three threshold scenarios. User-supplied labels, CRS text, and data-source subtitles are HTML-escaped. There are no analytics, remote assets, or network calls.

`plugin.py` registers/removes the provider and UI actions. `demo.py` runs the same Processing algorithm as a user run, applies styles, and adds a dedicated group to the active project. It never clears the user's existing project.

## Design tradeoffs

- **Projected straight-line distances:** easy to audit and useful for screening. A local metre-based projection must be chosen; network access and travel-time modeling are future extensions.
- **Fail-fast validation:** a bad population record cannot silently shrink the denominator. Users clean inputs and rerun.
- **Explicit output schema:** avoids field-name collisions and export-provider constraints. Users can join back using source IDs during the same dataset version.
- **Gap ranking:** transparent population-distance measure, rather than claiming an optimal facility placement algorithm.
- **No extra plugin dependencies:** only QGIS and its bundled Python/Qt; pyproj is needed solely to regenerate the committed fictional coordinate fixtures.
- **QGIS 3.44 target:** one declared API/Qt baseline, capped below 4 until migrated and tested.
- **Whole-result memory:** straightforward implementation and reference tests. Streaming populations/outputs and report sampling remain future scale work.

## Future extensions

1. Route distance and travel time using an appropriately licensed road network.
2. Facility capacity and type filters.
3. Candidate-site comparisons with measured incremental population coverage.
4. Real, cited local data and a documented study area.
5. A QGIS print-layout export with attribution and contextual basemap.

These extensions are not implemented in version 0.1.0.
