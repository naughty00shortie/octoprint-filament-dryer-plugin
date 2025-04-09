import setuptools

########################################################################################################################

plugin_identifier = "filamentdryer"
plugin_package = "octoprint_%s" % plugin_identifier
plugin_name = "Filament Dryer Controller"
plugin_version = "0.1.10"
plugin_description = """A plugin to monitor and control temperature using a DHT22 sensor and GPIO"""
plugin_author = "naughty00shortie"
plugin_author_email = "naughty00shortie@gmail.com"
plugin_url = "https://github.com/naughty00shortie/octoprint-filament-dryer-plugin"
plugin_license = "GPL-3.0"
plugin_additional_data = []

########################################################################################################################

def package_data_dirs(source, sub_folders):
    import os
    dirs = []

    for d in sub_folders:
        folder = os.path.join(source, d)
        if not os.path.exists(folder):
            continue

        for dirname, _, files in os.walk(folder):
            dirname = os.path.relpath(dirname, source)
            for f in files:
                dirs.append(os.path.join(dirname, f))

    return dirs

def params():
    name = plugin_name
    version = plugin_version
    description = plugin_description
    author = plugin_author
    author_email = plugin_author_email
    url = plugin_url
    license = plugin_license

    packages = [plugin_package]

    package_data = {plugin_package: package_data_dirs(plugin_package, ['static', 'templates', 'translations'] + plugin_additional_data)}
    include_package_data = True
    python_requires = '>=3.7'
    zip_safe = False
    install_requires = open("requirements.txt").read().split("\n")

    entry_points = {
        "octoprint.plugin": ["%s = %s" % (plugin_identifier, plugin_package)]
    }

    return locals()

setuptools.setup(**params())