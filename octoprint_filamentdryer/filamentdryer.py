import octoprint.plugin
import requests

API_BASE = "http://localhost:8000"  # Modify if needed

class FilamentDryerPlugin(octoprint.plugin.StartupPlugin,
                          octoprint.plugin.TemplatePlugin,
                          octoprint.plugin.SettingsPlugin,
                          octoprint.plugin.AssetPlugin,
                          octoprint.plugin.SimpleApiPlugin):

    def on_after_startup(self):
        self._logger.info("DryerControl Plugin started")

    ##~~ SettingsPlugin mixin
    def get_settings_defaults(self):
        return {
            "fan_pin": 17,
            "heater_pin": 27,
            "target_temp": 65.0,
            "tolerance": 1.0
        }

    def on_settings_save(self, data):
        octoprint.plugin.SettingsPlugin.on_settings_save(self, data)
        settings = {
            "FAN_PIN": self._settings.get(["fan_pin"]),
            "HEATER_PIN": self._settings.get(["heater_pin"]),
            "TARGET_TEMP": self._settings.get(["target_temp"]),
            "TOLERANCE": self._settings.get(["tolerance"])
        }
        try:
            requests.post(f"{API_BASE}/settings", json=settings)
        except requests.RequestException as e:
            self._logger.error(f"Error updating dryer settings: {e}")

    ##~~ AssetPlugin mixin
    def get_assets(self):
        return {
            "js": ["js/filamentdryer.js"]
        }

    ##~~ TemplatePlugin mixin
    def get_template_configs(self):
        return [
            dict(type="settings", custom_bindings=False),
            dict(type="tab", name="Dryer Graph"),
        ]

    ##~~ SimpleApiPlugin mixin
    def get_api_commands(self):
        return dict(toggle=[])

    def on_api_command(self, command, data):
        if command == "toggle":
            try:
                resp = requests.get(f"{API_BASE}/system").json()
                new_state = not resp.get("system_on", False)
                requests.post(f"{API_BASE}/system", json={"on": new_state})
            except requests.RequestException as e:
                self._logger.error(f"Failed to toggle system state: {e}")
