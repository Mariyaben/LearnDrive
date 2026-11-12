from pathlib import Path

from qgis.PyQt.QtWidgets import QAction, QMessageBox
from qgis.PyQt.QtGui import QIcon
from qgis.core import QgsApplication

from .provider import AccessLensProvider


class AccessLensPlugin:
    def __init__(self, iface):
        self.iface = iface
        self.provider = None
        self.actions = []

    def initProcessing(self):
        if self.provider is None:
            self.provider = AccessLensProvider()
            QgsApplication.processingRegistry().addProvider(self.provider)

    def initGui(self):
        self.initProcessing()
        icon = QIcon(str(Path(__file__).with_name("icon.svg")))
        for title, callback in [
            ("Analyze service access gaps…", self.run),
            ("Load fictional AccessLens demo", self.demo),
        ]:
            action = QAction(icon, title, self.iface.mainWindow())
            action.triggered.connect(callback)
            self.iface.addPluginToMenu("&AccessLens", action)
            self.actions.append(action)
        self.iface.addToolBarIcon(self.actions[0])

    def run(self):
        import processing

        processing.execAlgorithmDialog("accesslens:analyze_access", {})

    def demo(self):
        from .demo import load_demo

        try:
            load_demo(self.iface)
        except Exception as exc:
            QgsApplication.messageLog().logMessage(str(exc), "AccessLens")
            QMessageBox.critical(self.iface.mainWindow(), "AccessLens demo", str(exc))

    def unload(self):
        for action in self.actions:
            self.iface.removePluginMenu("&AccessLens", action)
            self.iface.removeToolBarIcon(action)
            action.deleteLater()
        self.actions.clear()
        if self.provider is not None:
            QgsApplication.processingRegistry().removeProvider(self.provider)
            self.provider = None
