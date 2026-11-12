"""Run with QGIS's Python interpreter, or exec this file in its Python console.

Not collected by plain-Python tests: a missing QGIS runtime must not be reported
as successful integration testing.
"""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from qgis.PyQt.QtCore import QVariant
from qgis.core import (
    QgsApplication, QgsCoordinateReferenceSystem, QgsCoordinateTransform,
    QgsFeature, QgsField, QgsGeometry, QgsPointXY, QgsProcessingContext,
    QgsProcessingException, QgsProcessingFeedback, QgsProject, QgsVectorLayer,
)

owned_app = QgsApplication.instance() is None
app = QgsApplication([], False) if owned_app else QgsApplication.instance()
if owned_app:
    app.initQgis()
sys.path.insert(0, str(Path(QgsApplication.pkgDataPath()) / "python" / "plugins"))
from processing.core.Processing import Processing
Processing.initialize()
import processing
from accesslens.provider import AccessLensProvider

provider = QgsApplication.processingRegistry().providerById("accesslens")
owned_provider = provider is None
if owned_provider:
    provider = AccessLensProvider()
    QgsApplication.processingRegistry().addProvider(provider)


class QgisIntegration(unittest.TestCase):
    def setUp(self):
        self.context = QgsProcessingContext()
        self.context.setProject(QgsProject.instance())
        self.feedback = QgsProcessingFeedback()
        self.data = ROOT / "accesslens" / "data"
        self.demand = QgsVectorLayer(str(self.data / "communities.geojson"), "Demand", "ogr")
        self.facilities = QgsVectorLayer(str(self.data / "facilities.geojson"), "Clinics", "ogr")
        self.parameters = {
            "DEMAND": self.demand, "POPULATION": "population", "DEMAND_NAME": "name",
            "FACILITIES": self.facilities, "FACILITY_NAME": "name", "THRESHOLD": 1000,
            "METRIC_CRS": "EPSG:32643", "OUTPUT": "memory:", "LINKS": "memory:", "BUFFERS": "memory:",
        }

    def run_algorithm(self, **overrides):
        return processing.run("accesslens:analyze_access", {**self.parameters, **overrides}, context=self.context, feedback=self.feedback)

    def layer(self, value):
        if isinstance(value, QgsVectorLayer):
            return value
        return self.context.getMapLayer(value)

    def point_layer(self, coordinates, population=None, crs="EPSG:32643"):
        layer = QgsVectorLayer(f"Point?crs={crs}", "Fixture", "memory")
        provider = layer.dataProvider()
        provider.addAttributes([QgsField("population", QVariant.Double)])
        layer.updateFields()
        for i, point in enumerate(coordinates):
            feature = QgsFeature(layer.fields())
            feature.setGeometry(QgsGeometry.fromPointXY(QgsPointXY(*point)))
            feature.setAttributes([population[i] if population is not None else 1])
            self.assertTrue(provider.addFeature(feature))
        layer.updateExtents()
        return layer

    def test_demo_counts_reprojection_and_reference_distances(self):
        result = self.run_algorithm()
        points = self.layer(result["OUTPUT"])
        links = self.layer(result["LINKS"])
        buffers = self.layer(result["BUFFERS"])
        self.assertEqual((points.featureCount(), links.featureCount(), buffers.featureCount()), (36, 36, 4))
        self.assertEqual(points.crs().authid(), "EPSG:32643")
        import csv
        with (ROOT / "demo/community-access.csv").open(encoding="utf-8") as stream:
            reference = {row["community"]: row for row in csv.DictReader(stream)}
        for feature in points.getFeatures():
            expected = reference[feature["community"]]
            self.assertAlmostEqual(feature["distance_m"], float(expected["distance_m"]), places=4)
            self.assertEqual(feature["served"], int(expected["served"]))
            self.assertEqual(feature["gap_rank"], int(expected["gap_rank"]))
        for feature in buffers.getFeatures():
            self.assertTrue(feature.geometry().isGeosValid())
            self.assertGreater(feature.geometry().area(), 3_130_000)

    def test_population_totals_match_expected_summary(self):
        points = self.layer(self.run_algorithm()["OUTPUT"])
        rows = list(points.getFeatures())
        expected = json.loads((ROOT / "demo/expected-summary.json").read_text(encoding="utf-8"))
        served = sum(row["population"] for row in rows if row["served"])
        self.assertEqual(served, expected["served_population"])
        self.assertEqual(sum(row["population"] for row in rows), expected["population"])

    def test_rejects_angular_web_mercator_and_foot_units(self):
        for crs in ("EPSG:4326", "EPSG:3857", "EPSG:2263"):
            with self.subTest(crs=crs), self.assertRaises(QgsProcessingException):
                self.run_algorithm(METRIC_CRS=crs)

    def test_rejects_missing_or_negative_population(self):
        for population in (None, -1):
            layer = self.point_layer([(777000, 1433000)], [population])
            with self.subTest(population=population), self.assertRaises(QgsProcessingException):
                self.run_algorithm(DEMAND=layer, DEMAND_NAME="")

    def test_rejects_empty_facility_layer(self):
        empty = self.point_layer([])
        with self.assertRaises(QgsProcessingException):
            self.run_algorithm(FACILITIES=empty, FACILITY_NAME="")

    def test_rejects_missing_geometry(self):
        layer = self.point_layer([])
        feature = QgsFeature(layer.fields())
        feature.setAttributes([10])
        layer.dataProvider().addFeature(feature)
        with self.assertRaises(QgsProcessingException):
            self.run_algorithm(DEMAND=layer, DEMAND_NAME="")

    def test_inclusive_boundary_and_metric_distance(self):
        demand = self.point_layer([(777300, 1433400)], [25])
        clinics = self.point_layer([(777000, 1433000)])
        result = self.run_algorithm(DEMAND=demand, DEMAND_NAME="", FACILITIES=clinics, FACILITY_NAME="", THRESHOLD=500)
        row = next(self.layer(result["OUTPUT"]).getFeatures())
        self.assertEqual(row["distance_m"], 500)
        self.assertEqual(row["served"], 1)

    def test_different_input_crs_produces_same_distance(self):
        demand = self.point_layer([(777300, 1433400)], [25])
        to_wgs = QgsCoordinateTransform(QgsCoordinateReferenceSystem("EPSG:32643"), QgsCoordinateReferenceSystem("EPSG:4326"), QgsProject.instance())
        clinic = to_wgs.transform(QgsPointXY(777000, 1433000))
        clinics = self.point_layer([(clinic.x(), clinic.y())], crs="EPSG:4326")
        result = self.run_algorithm(DEMAND=demand, DEMAND_NAME="", FACILITIES=clinics, FACILITY_NAME="", THRESHOLD=1000)
        row = next(self.layer(result["OUTPUT"]).getFeatures())
        self.assertAlmostEqual(row["distance_m"], 500, places=5)

    def test_geopackage_and_html_outputs_can_be_reopened(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder)
            result = self.run_algorithm(OUTPUT=str(target / "access.gpkg"), LINKS=str(target / "links.gpkg"), BUFFERS=str(target / "buffers.gpkg"), REPORT=str(target / "report.html"), DATASET="<fictional>")
            for key in ("OUTPUT", "LINKS", "BUFFERS"):
                layer = QgsVectorLayer(result[key], key, "ogr")
                self.assertTrue(layer.isValid())
                self.assertGreater(layer.featureCount(), 0)
                del layer
            html = (target / "report.html").read_text(encoding="utf-8")
            self.assertIn("&lt;fictional&gt;", html)
            self.assertIn("<svg", html)

    def test_cancellation_is_not_reported_as_success(self):
        from accesslens.algorithm import AccessAlgorithm
        self.feedback.cancel()
        algorithm = AccessAlgorithm()
        algorithm.initAlgorithm()
        with self.assertRaisesRegex(QgsProcessingException, "canceled"):
            algorithm.processAlgorithm(self.parameters, self.context, self.feedback)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(QgisIntegration)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if owned_provider:
        QgsApplication.processingRegistry().removeProvider(provider)
    if owned_app:
        app.exitQgis()
    if not result.wasSuccessful():
        raise SystemExit(1)
