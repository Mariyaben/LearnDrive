# A two-minute demonstration

**Before recording:** run the QGIS acceptance checklist in VALIDATION.md. The data are synthetic. Do not describe the results as findings about Bengaluru's actual clinics or population.

1. **Problem, 15 seconds.** “I built AccessLens to turn community population points and public-service locations into a reproducible assessment of access gaps.”
2. **Open, 20 seconds.** Show the installed plugin and select **Load fictional AccessLens demo**. Explain that the demo contains 36 invented communities and four invented clinics near Bengaluru.
3. **Map, 30 seconds.** Point out service circles, teal served communities, coral underserved communities, and connections to nearest clinics. Open the point attribute table and show `distance_m`, `population`, `priority_pk`, and `gap_rank`.
4. **Report, 25 seconds.** Show population coverage and the priority table. Explain why population-weighted coverage differs from a percentage of communities. Compare the 500 m, 1,000 m, and 1,500 m scenarios.
5. **Custom run, 20 seconds.** Open the algorithm dialog. Show the two input layers, population field, distance threshold, local projected CRS, and persistent GeoPackage destinations.
6. **Engineering, 10 seconds.** Explain that QGIS's R-tree finds nearest facilities while an independent Python core calculates and tests the statistics.

End with one real limitation: “These are straight-line screening distances; road barriers and facility capacity need additional modeling.”

For screenshots, capture the styled QGIS map with the AccessLens layer group and the exported report. Avoid showing only the code editor. Export temporary results before saving a portfolio `.qgz` project.
