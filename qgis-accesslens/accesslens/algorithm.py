"""QGIS Processing adapter: reprojection, R-tree search and spatial outputs."""

from math import hypot, isfinite
from pathlib import Path

from qgis.PyQt.QtCore import QVariant
from qgis.core import (
    NULL, QgsCoordinateTransform, QgsCsException, QgsFeature, QgsFeatureSink,
    QgsField, QgsFields, QgsGeometry, QgsPointXY, QgsProcessing,
    QgsProcessingAlgorithm, QgsProcessingException,
    QgsProcessingParameterCrs, QgsProcessingParameterFeatureSink,
    QgsProcessingParameterFeatureSource, QgsProcessingParameterField,
    QgsProcessingParameterFileDestination, QgsProcessingParameterNumber,
    QgsProcessingParameterString,
    QgsSpatialIndex, QgsUnitTypes, QgsWkbTypes,
)

from .core import Demand, Facility, analyze
from .report import render_report


class AccessAlgorithm(QgsProcessingAlgorithm):
    def name(self):
        return "analyze_access"

    def displayName(self):
        return "Analyze service access gaps"

    def group(self):
        return "Service planning"

    def groupId(self):
        return "service_planning"

    def createInstance(self):
        return AccessAlgorithm()

    def shortHelpString(self):
        return (
            "Assess population-weighted access to service locations. Input layers must "
            "contain single points, with valid CRSs. Select a local metre-based projected "
            "CRS (e.g. the local UTM zone). EPSG:3857 is rejected because its distance "
            "distortion makes it unsuitable. Distance is straight-line, not road travel. "
            "Population must be finite and nonnegative; invalid records stop the run. "
            "Priority = population × max(distance − threshold, 0) / 1000, in person-km. "
            "A point on the threshold is served. A gap rank of 0 means served. "
            "Outputs replace original attributes with a documented analysis schema. "
            "For large datasets, omit the HTML report to avoid a large embedded map."
        )

    def initAlgorithm(self, config=None):
        self.addParameter(QgsProcessingParameterFeatureSource(
            "DEMAND", "Community points", [QgsProcessing.TypeVectorPoint]))
        self.addParameter(QgsProcessingParameterField(
            "POPULATION", "Population field", parentLayerParameterName="DEMAND",
            type=QgsProcessingParameterField.Numeric))
        self.addParameter(QgsProcessingParameterField(
            "DEMAND_NAME", "Community label field", parentLayerParameterName="DEMAND",
            optional=True))
        self.addParameter(QgsProcessingParameterFeatureSource(
            "FACILITIES", "Service location points", [QgsProcessing.TypeVectorPoint]))
        self.addParameter(QgsProcessingParameterField(
            "FACILITY_NAME", "Service label field", parentLayerParameterName="FACILITIES",
            optional=True))
        self.addParameter(QgsProcessingParameterNumber(
            "THRESHOLD", "Access threshold (metres)",
            type=QgsProcessingParameterNumber.Double, defaultValue=1000, minValue=0.01))
        self.addParameter(QgsProcessingParameterCrs(
            "METRIC_CRS", "Local projected CRS in metres"))
        self.addParameter(QgsProcessingParameterString(
            "DATASET", "Report data source / subtitle", defaultValue="User-provided data", optional=True))
        self.addParameter(QgsProcessingParameterFeatureSink(
            "OUTPUT", "Community access results", QgsProcessing.TypeVectorPoint))
        self.addParameter(QgsProcessingParameterFeatureSink(
            "LINKS", "Connections to nearest service", QgsProcessing.TypeVectorLine))
        self.addParameter(QgsProcessingParameterFeatureSink(
            "BUFFERS", "Service distance circles (overlaps retained)", QgsProcessing.TypeVectorPolygon))
        self.addParameter(QgsProcessingParameterFileDestination(
            "REPORT", "Offline analysis report", "HTML files (*.html)", optional=True,
            createByDefault=False))

    def processAlgorithm(self, parameters, context, feedback):
        def check_cancel():
            if feedback.isCanceled():
                raise QgsProcessingException("AccessLens analysis canceled; discard partial outputs.")

        check_cancel()
        crs = self.parameterAsCrs(parameters, "METRIC_CRS", context)
        if (not crs.isValid() or crs.isGeographic()
                or crs.mapUnits() != QgsUnitTypes.DistanceMeters
                or crs.authid() == "EPSG:3857"):
            raise QgsProcessingException("Choose a local projected CRS in metres, such as UTM. EPSG:3857 is not supported.")
        threshold = self.parameterAsDouble(parameters, "THRESHOLD", context)

        def load_points(key, name_key, population_key=None):
            source = self.parameterAsSource(parameters, key, context)
            if source is None:
                raise QgsProcessingException(f"Could not open {key} input.")
            if not source.sourceCrs().isValid():
                raise QgsProcessingException(f"{key} has no valid source CRS.")
            label_field = self.parameterAsString(parameters, name_key, context)
            pop_field = self.parameterAsString(parameters, population_key, context) if population_key else None
            for field in (label_field, pop_field):
                if field and source.fields().indexFromName(field) < 0:
                    raise QgsProcessingException(f"{key}: field {field!r} does not exist.")
            if population_key and not pop_field:
                raise QgsProcessingException("Select a numeric population field.")
            transform = QgsCoordinateTransform(source.sourceCrs(), crs, context.transformContext())
            result = []
            for feature in source.getFeatures():
                check_cancel()
                geometry = QgsGeometry(feature.geometry())
                if (geometry.isNull() or geometry.isEmpty() or geometry.isMultipart()
                        or QgsWkbTypes.geometryType(geometry.wkbType()) != QgsWkbTypes.PointGeometry):
                    raise QgsProcessingException(f"{key} feature {feature.id()}: expected one nonempty point.")
                try:
                    geometry.transform(transform)
                except QgsCsException as exc:
                    raise QgsProcessingException(f"{key} feature {feature.id()}: could not transform coordinates: {exc}") from exc
                point = geometry.asPoint()
                if not isfinite(point.x()) or not isfinite(point.y()):
                    raise QgsProcessingException(f"{key} feature {feature.id()}: invalid transformed coordinates.")
                label = feature[label_field] if label_field else None
                if label is None or label == NULL or str(label).strip() == "":
                    label = f"{key.title()} {feature.id()}"
                args = (str(feature.id()), str(label), point.x(), point.y())
                if population_key:
                    try:
                        population = float(feature[pop_field])
                    except (ValueError, TypeError, OverflowError) as exc:
                        raise QgsProcessingException(f"{key} feature {feature.id()}: population is missing or not numeric.") from exc
                    result.append(Demand(*args, population))
                else:
                    result.append(Facility(*args))
            return result

        feedback.pushInfo("Transforming inputs to the analysis CRS…")
        demands = load_points("DEMAND", "DEMAND_NAME", "POPULATION")
        facilities = load_points("FACILITIES", "FACILITY_NAME")
        check_cancel()
        all_points = demands + facilities
        if all_points:
            span = hypot(max(p.x for p in all_points) - min(p.x for p in all_points),
                         max(p.y for p in all_points) - min(p.y for p in all_points))
            if span > 200000:
                feedback.pushWarning("Study extent exceeds 200 km. Check local projection distortion before interpreting distances.")
        index = QgsSpatialIndex()
        # Independent sequential index IDs avoid provider-specific feature ID assumptions.
        for i, facility in enumerate(facilities):
            check_cancel()
            feature = QgsFeature()
            feature.setId(i)
            feature.setGeometry(QgsGeometry.fromPointXY(QgsPointXY(facility.x, facility.y)))
            if not index.addFeature(feature):
                raise QgsProcessingException("Could not build the facility spatial index.")

        completed = 0

        def nearest(x, y):
            nonlocal completed
            ids = index.nearestNeighbor(QgsPointXY(x, y), 1)
            if not ids:
                raise QgsProcessingException("Spatial index found no nearest facility.")
            # QGIS can return multiple IDs on a tie. Distances/coverage are identical.
            selected = min(ids, key=lambda i: (hypot(x - facilities[i].x, y - facilities[i].y), facilities[i].id))
            completed += 1
            feedback.setProgress(15 + 45 * completed / max(1, len(demands)))
            return facilities[selected].id

        try:
            result = analyze(demands, facilities, threshold, nearest=nearest, check_cancel=check_cancel)
        except ValueError as exc:
            raise QgsProcessingException(str(exc)) from exc
        fields = QgsFields()
        for name, kind in [
            ("source_id", QVariant.String), ("community", QVariant.String),
            ("population", QVariant.Double), ("facility_id", QVariant.String),
            ("facility", QVariant.String), ("distance_m", QVariant.Double),
            ("served", QVariant.Int), ("priority_pk", QVariant.Double), ("gap_rank", QVariant.Int),
        ]:
            fields.append(QgsField(name, kind))
        buffer_fields = QgsFields()
        for name, kind in [("facility_id", QVariant.String), ("facility", QVariant.String), ("radius_m", QVariant.Double)]:
            buffer_fields.append(QgsField(name, kind))

        output = {}
        sinks = {}
        for key, schema, kind in [
            ("OUTPUT", fields, QgsWkbTypes.Point),
            ("LINKS", fields, QgsWkbTypes.LineString),
            ("BUFFERS", buffer_fields, QgsWkbTypes.Polygon),
        ]:
            sink, destination = self.parameterAsSink(parameters, key, context, schema, kind, crs)
            if sink is None:
                raise QgsProcessingException(f"Could not create {key} output.")
            sinks[key] = sink
            output[key] = destination

        def insert(key, schema, attributes, geometry):
            check_cancel()
            feature = QgsFeature(schema)
            feature.setAttributes(attributes)
            feature.setGeometry(geometry)
            if not sinks[key].addFeature(feature, QgsFeatureSink.FastInsert):
                raise QgsProcessingException(f"Failed to write {key}: {sinks[key].lastError()}")

        for i, row in enumerate(result.rows):
            point = QgsPointXY(row.demand.x, row.demand.y)
            facility = QgsPointXY(row.facility.x, row.facility.y)
            attributes = [
                row.demand.id, row.demand.name, row.demand.population,
                row.facility.id, row.facility.name, row.distance_m,
                int(row.served), row.priority, row.gap_rank,
            ]
            insert("OUTPUT", fields, attributes, QgsGeometry.fromPointXY(point))
            insert("LINKS", fields, attributes, QgsGeometry.fromPolylineXY([point, facility]))
            feedback.setProgress(60 + 30 * (i + 1) / len(result.rows))
        for facility in facilities:
            geometry = QgsGeometry.fromPointXY(QgsPointXY(facility.x, facility.y)).buffer(threshold, 32)
            insert("BUFFERS", buffer_fields, [facility.id, facility.name, threshold], geometry)
        for sink in sinks.values():
            sink.finalize()
        report = self.parameterAsFileOutput(parameters, "REPORT", context)
        if report:
            check_cancel()
            try:
                dataset = self.parameterAsString(parameters, "DATASET", context) or "User-provided data"
                Path(report).write_text(render_report(result, crs.authid() or crs.description(), dataset), encoding="utf-8")
            except OSError as exc:
                raise QgsProcessingException(f"Could not write the report: {exc}") from exc
            output["REPORT"] = report
        feedback.pushInfo(str(result.summary()))
        feedback.setProgress(100)
        return output
