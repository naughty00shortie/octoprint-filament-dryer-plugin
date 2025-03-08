import octoprint.plugin
import threading
import time
import subprocess

class FilamentDryerPlugin(octoprint.plugin.StartupPlugin,
                          octoprint.plugin.TemplatePlugin,
                          octoprint.plugin.SettingsPlugin,
                          octoprint.plugin.SimpleApiPlugin):

    def __init__(self):
        self.monitor_thread = None
        self.running = False

    def get_settings_defaults(self):
        return {
            "target_temp": 65.0,
            "tolerance": 1.0,
            "fan_on_cmd": "pinctrl set 17 op dh",
            "fan_off_cmd": "pinctrl set 17 op dl",
            "element_on_cmd": "pinctrl set 27 op dh",
            "element_off_cmd": "pinctrl set 27 op dl",
        }
    
    def execute_command(self, command):
        if command:
            self._logger.info(f"Executing command: {command}")
            subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

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
                            subprocess.run(element_on_cmd, shell=True)
                        elif temperature_c > (target_temp + tolerance):
                            subprocess.run(element_off_cmd, shell=True)

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
            subprocess.run(fan_on_cmd, shell=True)
            self.start_monitoring()
        elif command == "stop":
            subprocess.run(fan_off_cmd, shell=True)
            self.stop_monitoring()

    def get_template_configs(self):
        return [
            {"type": "settings", "custom_bindings": True},
            {"type": "navbar", "custom_bindings": True}
        ]

    def get_api(self):
        return {
            "plugin/filamentdryer": {
                "method": "POST",
                "commands": ["start", "stop"],
                "callback": self.on_api_command
            }
        }

__plugin_name__ = "Filament Dryer"
__plugin_pythoncompat__ = ">=2.7,<4"


def __plugin_load__():
    global __plugin_implementation__
    __plugin_implementation__ = FilamentDryerPlugin()
