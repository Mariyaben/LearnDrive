"""Load a styled, fictional demonstration into the current QGIS project."""

from pathlib import Path

from qgis.PyQt.QtGui import QColor, QDesktopServices
from qgis.PyQt.QtCore import QUrl
from qgis.core import (
    QgsCategorizedSymbolRenderer, QgsCoordinateReferenceSystem,
    QgsFillSymbol, QgsLineSymbol, QgsMarkerSymbol, QgsProcessingContext,
    QgsProcessingFeedback, QgsProcessingUtils, QgsProject,
    QgsRendererCategory, QgsVectorLayer,
)


def load_demo(iface):
    import processing

    project = QgsProject.instance()
    data = Path(__file__).parent / "data"
    demand = QgsVectorLayer(str(data / "communities.geojson"), "Fictional communities", "ogr")
    facilities = QgsVectorLayer(str(data / "facilities.geojson"), "Fictional clinics", "ogr")
    if not demand.isValid() or not facilities.isValid():
        raise RuntimeError("Could not open the bundled demo data.")
    context = QgsProcessingContext()
    context.setProject(project)
    report_path = QgsProcessingUtils.generateTempFilename("accesslens-demo.html")
    outputs = processing.run("accesslens:analyze_access", {
        "DEMAND": demand, "POPULATION": "population", "DEMAND_NAME": "name",
        "FACILITIES": facilities, "FACILITY_NAME": "name", "THRESHOLD": 1000,
        "METRIC_CRS": "EPSG:32643", "OUTPUT": "memory:", "LINKS": "memory:",
        "DATASET": "Fictional Bengaluru-area planning demo — synthetic communities and clinics",
        "BUFFERS": "memory:", "REPORT": report_path,
    }, context=context, feedback=QgsProcessingFeedback())
    # processing.run returns owned layer objects for in-memory sinks.
    def resolve(value):
        if isinstance(value, QgsVectorLayer):
            return value
        layer = context.takeResultLayer(value)
        if layer is None:
            raise RuntimeError(f"Could not retrieve demo result: {value}")
        return layer

    points, links, buffers = (resolve(outputs[key]) for key in ("OUTPUT", "LINKS", "BUFFERS"))
    points.setName("Community access — 1,000 m threshold")
    links.setName("Nearest clinic connections")
    buffers.setName("Clinic service radius — 1,000 m")
    categories = []
    for value, color, name in [(1, "#157e73", "Within 1,000 m"), (0, "#d05b46", "Underserved")]:
        symbol = QgsMarkerSymbol.createSimple({"name": "circle", "color": color, "outline_color": "white", "outline_width": "0.3", "size": "3.5"})
        categories.append(QgsRendererCategory(value, symbol, name))
    points.setRenderer(QgsCategorizedSymbolRenderer("served", categories))
    facilities.renderer().setSymbol(QgsMarkerSymbol.createSimple({"name": "diamond", "color": "#183c4a", "outline_color": "white", "size": "5"}))
    links.renderer().setSymbol(QgsLineSymbol.createSimple({"line_color": "#91aaa5", "line_width": "0.25", "line_style": "dash"}))
    buffers.renderer().setSymbol(QgsFillSymbol.createSimple({"color": "21,126,115,20", "outline_color": "21,126,115,100", "outline_style": "dash"}))
    group = project.layerTreeRoot().addGroup("AccessLens — FICTIONAL DEMO")
    for layer in (facilities, points, links, buffers, demand):
        project.addMapLayer(layer, False)
        node = group.addLayer(layer)
        node.setItemVisibilityChecked(layer is not demand)
    extent = points.extent()
    extent.combineExtentWith(buffers.extent())
    canvas = iface.mapCanvas()
    # Leave the existing project's CRS intact; transform the zoom rectangle if needed.
    from qgis.core import QgsCoordinateTransform

    if not canvas.mapSettings().destinationCrs().isValid():
        project.setCrs(QgsCoordinateReferenceSystem("EPSG:32643"))
    transform = QgsCoordinateTransform(points.crs(), canvas.mapSettings().destinationCrs(), project)
    canvas.setExtent(transform.transformBoundingBox(extent))
    canvas.refresh()
    QDesktopServices.openUrl(QUrl.fromLocalFile(report_path))
    iface.messageBar().pushSuccess("AccessLens", "Fictional demo loaded. Use Save As to save the project; export temporary result layers to GeoPackage to preserve them.")
