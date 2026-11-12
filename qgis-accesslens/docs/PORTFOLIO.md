# Resume and portfolio copy

AccessLens combines a QGIS Processing provider, a standalone calculation core, and offline reporting. The synthetic case study demonstrates the workflow reproducibly; its results are not real-world impact or adoption metrics.

## Project entry

**AccessLens — Public-Service Accessibility Analysis | Python, QGIS, PyQGIS, GeoJSON**

- Developed a QGIS Processing plugin to identify underserved communities using nearest-facility analysis, population-weighted coverage, and a transparent priority score.
- Implemented coordinate transformations, R-tree facility lookup, input validation, and point, line, and service-radius outputs with an offline HTML/SVG analysis report.
- Created a reproducible 36-community, four-facility synthetic case study with independent calculation tests and a separate QGIS integration test suite.

The calculation and report suite has 19 passing tests. QGIS runtime validation is pending; see VALIDATION.md for the current test status.

## Short description

AccessLens evaluates population-weighted proximity to public-service locations inside QGIS. It helps analysts inspect service gaps, compare distance thresholds, and communicate results through spatial layers and a self-contained report. The sample study uses fictional data near Bengaluru.

## Questions to prepare for

**Why use a local projected CRS?** Latitude/longitude units are angular. A suitable local metre-based projection makes planar distance meaningful within the study area. Metre units alone do not guarantee low distortion; the analyst still chooses an appropriate projection.

**Why not just count communities?** A small and a large community should not contribute equally to population coverage. Coverage divides served population by total population.

**What does the priority score mean?** It multiplies population by distance beyond the target in kilometres. It is a transparent screening metric in person-km, not a probability or an optimal site-selection result.

**Why a spatial index?** Repeatedly scanning every facility for each community costs O(N×M) comparisons. The QGIS implementation uses an R-tree for nearest-neighbor retrieval; it has not been performance-benchmarked.

**What can go wrong?** Incorrect source CRSs, unsuitable projections, stale facility locations, invalid/missing population, assumed population centroids, route barriers, and limited facility capacity can change interpretation. The plugin rejects several malformed inputs; it cannot verify source data truth.

**How did you test it?** Explain exact boundary cases, weighted statistics, deterministic ranking, bad inputs, cancellation, randomized distance checks, HTML escaping, and the difference between the independent Python suite and tests inside QGIS. Consult VALIDATION.md for the actual execution status.

## Extending the case study

The next step is a study using a properly sourced local dataset. Record the data license, download date, field meanings, cleaning steps, projection choice, and limitations, and compare the resulting access gaps with local context.
