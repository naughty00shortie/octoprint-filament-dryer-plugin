import octoprint.plugin
import subprocess
import threading
import time

class FilamentDryerPlugin(octoprint.plugin.StartupPlugin,
                          octoprint.plugin.TemplatePlugin,
                          octoprint.plugin.SettingsPlugin,
                          octoprint.plugin.SimpleApiPlugin):

    def __init__(self):
        self.monitor_thread = None
        self.running = False

    def get_settings_defaults(self):
        return dict(
            target_temp = 65.0,
            tolerance = 1.0,
            fan_on_cmd = "pinctrl set 17 op dh",
            fan_off_cmd = "pinctrl set 17 op dl",
            element_on_cmd = "pinctrl set 27 op dh",
            element_off_cmd = "pinctrl set 27 op dl"
    )

    def execute_command(self, command):
        if command:
            self._logger.info(f"Executing: {command}")
            result = subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self._logger.info(f"Output: {result.stdout.decode().strip()}, Error: {result.stderr.decode().strip()}")

    def start_monitoring(self):
        if self.monitor_thread and self.monitor_thread.is_alive():
            return
        self.running = True
        self.monitor_thread = threading.Thread(target=self.monitor_temperature, daemon=True)
        self.monitor_thread.start()

    def stop_monitoring(self):
        self.running = False
        if self.monitor_thread:
            self.monitor_thread.join()

    def monitor_temperature(self):
        try:
            import adafruit_dht
            import board
            dht_device = adafruit_dht.DHT22(board.D4)

            while self.running:
                try:
                    temperature_c = dht_device.temperature
                    if temperature_c is not None:
                        target_temp = self._settings.get_float(["target_temp"])
                        tolerance = self._settings.get_float(["tolerance"])
                        element_on_cmd = self._settings.get(["element_on_cmd"])
                        element_off_cmd = self._settings.get(["element_off_cmd"])

                        if temperature_c < (target_temp - tolerance):
                            self.execute_command(element_on_cmd)
                        elif temperature_c > (target_temp + tolerance):
                            self.execute_command(element_off_cmd)

                    time.sleep(2)

                except RuntimeError as e:
                    if "Checksum did not validate" in str(e):
                        self._logger.warning("DHT22 checksum error, retrying...")
                    else:
                        self._logger.error(f"DHT22 sensor error: {e}")

        except Exception as e:
            self._logger.error(f"Error in monitoring thread: {e}")

    def get_api_commands(self):
        return {
            "start": [],
            "stop": []
        }

    def on_api_command(self, command, data):
        fan_on_cmd = self._settings.get(["fan_on_cmd"])
        fan_off_cmd = self._settings.get(["fan_off_cmd"])

        if command == "start":
            self.execute_command(fan_on_cmd)
            self.start_monitoring()
        elif command == "stop":
            self.execute_command(fan_off_cmd)
            self.stop_monitoring()

    def get_template_configs(self):
        return [
            {"type": "settings", "custom_bindings": True},
            {"type": "navbar", "custom_bindings": True}
        ]

    def get_permissions(self):
        return [
            {
                "role": "SETTINGS_READ",
                "permissions": ["PLUGIN_FILAMENTDRYER_VIEW"],
                "description": "Allows reading filament dryer settings",
                "default_groups": ["user", "admin"]
            },
            {
                "role": "SETTINGS_WRITE",
                "permissions": ["PLUGIN_FILAMENTDRYER_EDIT"],
                "description": "Allows modifying filament dryer settings",
                "default_groups": ["admin"]
            }
        ]
def __plugin_load__():
    global __plugin_implementation__
    __plugin_implementation__ = FilamentDryerPlugin()

    global __plugin_hooks__
    __plugin_hooks__ = {
        "octoprint.comm.protocol.gcode.queuing": __plugin_implementation__.hook_gcode_queuing,
        "octoprint.events.register_custom_events": __plugin_implementation__.register_custom_events,
        "octoprint.access.permissions": __plugin_implementation__.get_additional_permissions,
        "octoprint.server.api.before_request": __plugin_implementation__._hook_octoprint_server_api_before_request,
    }

