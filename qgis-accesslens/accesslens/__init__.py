"""AccessLens QGIS plugin. Core calculations also run without QGIS."""

__version__ = "0.1.0"


def classFactory(iface):
    from .plugin import AccessLensPlugin

    return AccessLensPlugin(iface)
