from qgis.core import QgsProcessingProvider

from .algorithm import AccessAlgorithm


class AccessLensProvider(QgsProcessingProvider):
    def id(self):
        return "accesslens"

    def name(self):
        return "AccessLens"

    def longName(self):
        return "AccessLens — Public service access analysis"

    def loadAlgorithms(self):
        self.addAlgorithm(AccessAlgorithm())
