import octoprint.plugin
import lgpio
import adafruit_dht
import board
import threading
import time

class TemperatureControllerPlugin(octoprint.plugin.StartupPlugin,
                                  octoprint.plugin.TemplatePlugin,
                                  octoprint.plugin.AssetPlugin,
                                  octoprint.plugin.SimpleApiPlugin,
                                  octoprint.plugin.SettingsPlugin):

    FAN_PIN = 17
    HEATER_PIN = 27
    TARGET_TEMP = 70.0
    TOLERANCE = 1.5
    RUNNING = True

    def on_after_startup(self):
        self.dht_device = adafruit_dht.DHT22(board.D4)
        self.h = lgpio.gpiochip_open(0)
        lgpio.gpio_claim_output(self.h, self.FAN_PIN)
        lgpio.gpio_claim_output(self.h, self.HEATER_PIN)
        lgpio.gpio_write(self.h, self.FAN_PIN, 1)  # Fan always on

        self._logger.info("TemperatureController Plugin Started")

        self.temp_thread = threading.Thread(target=self.monitor_temperature, daemon=True)
        self.temp_thread.start()

    def monitor_temperature(self):
        while self.RUNNING:
            try:
                temperature_c = self.dht_device.temperature
                humidity = self.dht_device.humidity

                if temperature_c is not None and humidity is not None:
                    self._logger.info(f"Temp: {temperature_c:.1f} C, Humidity: {humidity}%")

                    if temperature_c < (self.TARGET_TEMP - self.TOLERANCE):
                        lgpio.gpio_write(self.h, self.HEATER_PIN, 1)
                    elif temperature_c > (self.TARGET_TEMP + self.TOLERANCE):
                        lgpio.gpio_write(self.h, self.HEATER_PIN, 0)

            except RuntimeError:
                pass  # Ignore DHT22 read errors

            time.sleep(5)

    def get_api_commands(self):
        return {
            "get_temp": [],
            "set_target_temp": ["temp"]
        }

    def on_api_command(self, command, data):
        if command == "get_temp":
            temperature_c = self.dht_device.temperature
            humidity = self.dht_device.humidity
            return {"temperature": temperature_c, "humidity": humidity}

        elif command == "set_target_temp":
            self.TARGET_TEMP = float(data.get("temp", 70.0))
            return {"status": "Target temperature updated"}

    def on_shutdown(self):
        self.RUNNING = False
        lgpio.gpio_write(self.h, self.FAN_PIN, 0)
        lgpio.gpio_write(self.h, self.HEATER_PIN, 0)
        lgpio.gpiochip_close(self.h)

    def get_settings_defaults(self):
        return {
            "target_temp": 70,
            "fan_always_on": True
        }

    def get_template_configs(self):
        return [
            dict(type="settings", custom_bindings=False, template="filamentdryer_settings.jinja2")
        ]

__plugin_pythoncompat__ = ">=2.7,<4"