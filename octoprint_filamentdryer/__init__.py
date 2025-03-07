import time
import adafruit_dht
import board
import lgpio
import atexit
import octoprint.plugin

class TemperatureControlPlugin(octoprint.plugin.OctoPrintPlugin):
    def __init__(self):
        self.dht_device = adafruit_dht.DHT22(board.D4)
        self.h = lgpio.gpiochip_open(0)
        self.fan_pin = 17
        self.heater_pin = 27
        self.target_temp = 65.0
        self.tolerance = 1.0

        lgpio.gpio_claim_output(self.h, self.fan_pin)
        lgpio.gpio_claim_output(self.h, self.heater_pin)
        lgpio.gpio_write(self.h, self.fan_pin, 1)

    def cleanup(self):
        if self.h:
            lgpio.gpio_write(self.h, self.fan_pin, 0)
            lgpio.gpio_write(self.h, self.heater_pin, 0)
            lgpio.gpiochip_close(self.h)

    def on_after_startup(self):
        # Called after the plugin is fully loaded
        self._logger.info("Temperature Control Plugin started")
        atexit.register(self.cleanup)

    def check_temperature(self):
        try:
            temperature_c = self.dht_device.temperature
            humidity = self.dht_device.humidity

            if temperature_c is not None and humidity is not None:
                self._logger.info(f"Temp: {temperature_c:.1f} C    Humidity: {humidity}%")

                # Control the heater based on the target temperature
                if temperature_c < (self.target_temp - self.tolerance):
                    lgpio.gpio_write(self.h, self.heater_pin, 1)  # Turn heater on
                elif temperature_c > (self.target_temp + self.tolerance):
                    lgpio.gpio_write(self.h, self.heater_pin, 0)  # Turn heater off
        except RuntimeError as err:
            if "Checksum did not validate" in str(err):
                self._logger.warning("DHT22 checksum error, retrying...")
            else:
                self._logger.error(f"Sensor error: {err}")

    def run_temperature_check(self):
        while True:
            self.check_temperature()
            time.sleep(2.0)

    def get_update_information(self):
        return dict(
            displayName="Temperature Control Plugin",
            version=self._plugin_version,
            updateUrl="https://github.com/octoprint/plugins/temperature-control-plugin/releases/latest"
        )
    def on_settings_save(self, data):
        self.target_temp = data.get("target_temperature", 65.0)
        self.tolerance = data.get("tolerance", 1.0)
        self._logger.info(f"Settings saved: Target Temp={self.target_temp}, Tolerance={self.tolerance}")

    def get_update_information(self):
        return dict(
            displayName="Temperature Control Plugin",
            version=self._plugin_version,
            updateUrl="https://github.com/octoprint/plugins/temperature-control-plugin/releases/latest"
        )

    def set_heater_state(self, state):
        lgpio.gpio_write(self.h, self.heater_pin, state)

    def get_settings_defaults(self):
        return {
            "target_temp": 70,
            "fan_always_on": True
        }

    def get_template_configs(self):
        return [
            dict(type="settings", custom_bindings=False, template="filamentdryer_settings.jinja2")
        ]

__plugin_implementation__ = TemperatureControlPlugin()
__plugin_pythoncompat__ = ">=3.7,<4"

