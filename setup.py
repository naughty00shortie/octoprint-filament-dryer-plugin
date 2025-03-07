plugin_identifier = "filamentdryer"

plugin_package = "octoprint_filamentdryer"

plugin_name = "Filament Dryer Controller"

plugin_version = "0.1.2"

plugin_description = """A plugin to monitor and control temperature using a DHT22 sensor and GPIO"""

plugin_author = "naughty00shortie"

plugin_author_email = "naughty00shortie@gmail.com"

plugin_url = "https://github.com/naughty00shortie/octoprint-filament-dryer-plugin"

plugin_license = "GPL-3.0"

plugin_requires = [
    "OctoPrint",
    "lgpio",
    "adafruit-circuitpython-dht"
]

plugin_additional_data = []

plugin_addtional_packages = []

plugin_ignored_packages = []

additional_setup_parameters = {}

from setuptools import setup

try:
    import octoprint_setuptools
except:
    print("Could not import OctoPrint's setuptools, are you sure you are running that under "
          "the same python installation that OctoPrint is installed under?")
    import sys
    sys.exit(-1)

setup_parameters = octoprint_setuptools.create_plugin_setup_parameters(
    identifier=plugin_identifier,
    package=plugin_package,
    name=plugin_name,
    version=plugin_version,
    description=plugin_description,
    author=plugin_author,
    mail=plugin_author_email,
    url=plugin_url,
    license=plugin_license,
    requires=plugin_requires,
    additional_packages=plugin_addtional_packages,
    ignored_packages=plugin_ignored_packages,
    additional_data=plugin_additional_data
)

if len(additional_setup_parameters):
    from octoprint.util import dict_merge
    setup_parameters = dict_merge(setup_parameters, additional_setup_parameters)

setup(**setup_parameters)