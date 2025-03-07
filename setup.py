from setuptools import setup

PLUGIN_IDENTIFIER = "filamentdryer"
PLUGIN_PACKAGE = "octoprint_filamentdryer"
PLUGIN_NAME = "OctoPrint-FilamentTemperatureController"
PLUGIN_VERSION = "0.1.0"
PLUGIN_DESCRIPTION = "A plugin to monitor and control temperature using a DHT22 sensor and GPIO"
PLUGIN_AUTHOR = "Albertus"
PLUGIN_AUTHOR_EMAIL = "naughty00shortie@gmail.com"
PLUGIN_URL = "https://github.com/naughty00shortie/octoprint-filament-dryer-plugin"

setup(
    name=PLUGIN_NAME,
    version=PLUGIN_VERSION,
    description=PLUGIN_DESCRIPTION,
    author=PLUGIN_AUTHOR,
    author_email=PLUGIN_AUTHOR_EMAIL,
    url=PLUGIN_URL,
    packages=[PLUGIN_PACKAGE],
    install_requires=["OctoPrint"],
    entry_points={
        "octoprint.plugin": [
            f"{PLUGIN_IDENTIFIER} = {PLUGIN_PACKAGE}"
        ]
    },
)
