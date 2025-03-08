import time
import adafruit_dht
import board
import subprocess
import atexit
import octoprint.plugin

class FilamentDryerPlugin(octoprint.plugin.StartupPlugin,
                          octoprint.plugin.SettingsPlugin,
                          octoprint.plugin.TemplatePlugin):

    def get_settings_defaults(self):
        return {
            "target_temp": 65.0,
            "tolerance": 1.0,
            "fan_on_cmd": "echo 'Fan ON'",
            "fan_off_cmd": "echo 'Fan OFF'",
            "element_on_cmd": "echo 'Element ON'",
            "element_off_cmd": "echo 'Element OFF'"
        }

    def run_command(self, command):
        if command:
            try:
                subprocess.run(command, shell=True, check=True)
            except subprocess.CalledProcessError as e:
                self._logger.error(f"Command failed: {command}, Error: {e}")

    def cleanup(self):
        self.run_command(self._settings.get(["fan_off_cmd"]))
        self.run_command(self._settings.get(["element_off_cmd"]))

    def monitor_temperature(self):
        dht_device = adafruit_dht.DHT22(board.D4)
        atexit.register(self.cleanup)

        self.run_command(self._settings.get(["fan_on_cmd"]))

        try:
            while True:
                try:
                    temperature_c = dht_device.temperature
                    humidity = dht_device.humidity

                    if temperature_c is not None and humidity is not None:
                        print(f"Temp: {temperature_c:.1f} C    Humidity: {humidity}%")

                        target_temp = float(self._settings.get(["target_temp"]))
                        tolerance = float(self._settings.get(["tolerance"]))

                        if temperature_c < (target_temp - tolerance):
                            self.run_command(self._settings.get(["element_on_cmd"]))
                        elif temperature_c > (target_temp + tolerance):
                            self.run_command(self._settings.get(["element_off_cmd"]))

                except RuntimeError as err:
                    if "Checksum did not validate" in str(err):
                        print("Warning: DHT22 checksum error, retrying...")
                    else:
                        print("Sensor error:", err)

                time.sleep(2.0)

        except KeyboardInterrupt:
            pass
        finally:
            self.cleanup()

    def on_after_startup(self):
        self._logger.info("Starting Filament Dryer Monitor...")
        self.monitor_temperature()

    def get_template_configs(self):
        return [
            dict(type="settings", custom_bindings=False, template="filamentdryer_settings.jinja2")
        ]

    def get_settings_defaults(self):
        return {
            "target_temp": 65.0,
            "tolerance": 1.0,
            "fan_on_cmd": "pinctrl set 17 out 1",
            "fan_off_cmd": "pinctrl set 17 out 0",
            "element_on_cmd": "pinctrl set 27 out 1",
            "element_off_cmd": "pinctrl set 27 out 0",
        }

    def get_api_commands(self):
        return {
            "start": [],
            "stop": []
        }

    def on_api_command(self, command, data):
        fan_on_cmd = self._settings.get(["fan_on_cmd"])
        fan_off_cmd = self._settings.get(["fan_off_cmd"])
        element_on_cmd = self._settings.get(["element_on_cmd"])
        element_off_cmd = self._settings.get(["element_off_cmd"])

        if command == "start":
            subprocess.run(fan_on_cmd, shell=True)
            subprocess.run(element_on_cmd, shell=True)
        elif command == "stop":
            subprocess.run(fan_off_cmd, shell=True)
            subprocess.run(element_off_cmd, shell=True)

    def get_template_configs(self):
        return [{"type": "settings", "custom_bindings": True}]

__plugin_name__ = "Filament Dryer"
__plugin_pythoncompat__ = ">=2.7,<4"


def __plugin_load__():
    global __plugin_implementation__
    __plugin_implementation__ = FilamentDryerPlugin()


