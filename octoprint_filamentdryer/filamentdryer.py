import octoprint.plugin
import requests

class FilamentDryerPlugin(octoprint.plugin.StartupPlugin,
                          octoprint.plugin.TemplatePlugin,
                          octoprint.plugin.SettingsPlugin,
                          octoprint.plugin.AssetPlugin,
                          octoprint.plugin.SimpleApiPlugin):

    def on_after_startup(self):
        self._logger.info("DryerControl Plugin started")

    def get_settings_defaults(self):
        return {
            "api_url": "http://10.0.0.50",
            "fan_pin": 17,
            "heater_pin": 27,
            "target_temp": 65.0,
            "tolerance": 1.0
        }

    def get_assets(self):
        return {
            "js": ["js/filamentdryer.js"]
        }

    def get_template_configs(self):
        return [
            dict(type="settings", custom_bindings=True),
            dict(type="tab", name="Dryer Graph", custom_bindings=True),
            dict(type="navbar", custom_bindings=True)
        ]

    def get_template_vars(self):
        return dict(
            api_url=self._settings.get(["api_url"])
        )

    def get_api_commands(self):
        return dict(toggle=[])

    def on_api_command(self, command, data):
        if command == "toggle":
            try:
                api_url = self._settings.get(["api_url"])
                resp = requests.get(f"{api_url}/system").json()
                new_state = not resp.get("system_on", False)
                requests.post(f"{api_url}/system", json={"on": new_state})
            except requests.RequestException as e:
                self._logger.error(f"Failed to toggle system state: {e}")
