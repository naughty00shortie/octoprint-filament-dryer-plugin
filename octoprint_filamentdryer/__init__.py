# octoprint_filamentdryer/__init__.py
from .filamentdryer import FilamentDryerPlugin

__plugin_name__ = "Filament Dryer Controller"
__plugin_implementation__ = FilamentDryerPlugin()
__plugin_pythoncompat__ = ">=2.7,<4"