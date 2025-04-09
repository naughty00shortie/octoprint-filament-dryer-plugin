import time
import threading
from collections import deque
from fastapi import FastAPI
from pydantic import BaseModel
import adafruit_dht
import board
import lgpio
import atexit
import uvicorn

app = FastAPI()

SETTINGS = {
    "FAN_PIN": 17,
    "HEATER_PIN": 27,
    "TARGET_TEMP": 65.0,
    "TOLERANCE": 1.0
}

h = lgpio.gpiochip_open(0)
lgpio.gpio_claim_output(h, SETTINGS["FAN_PIN"])
lgpio.gpio_claim_output(h, SETTINGS["HEATER_PIN"])
lgpio.gpio_write(h, SETTINGS["FAN_PIN"], 1)

dht_device = adafruit_dht.DHT22(board.D4)

system_on = False
fan_on = False
heater_on = False
lock = threading.Lock()
history = deque(maxlen=900)


def cleanup():
    if h:
        lgpio.gpio_write(h, SETTINGS["FAN_PIN"], 0)
        lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
        lgpio.gpiochip_close(h)

atexit.register(cleanup)


def sensor_loop():
    global system_on, heater_on
    while True:
        with lock:
            if system_on:
                try:
                    temperature = dht_device.temperature
                    humidity = dht_device.humidity

                    if temperature is not None and humidity is not None:
                        target = SETTINGS["TARGET_TEMP"]
                        tolerance = SETTINGS["TOLERANCE"]

                        history.append({
                            "timestamp": time.time(),
                            "target_temp": target,
                            "actual_temp": temperature,
                            "humidity": humidity
                        })


                        if temperature < (target - tolerance):
                            lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 1)
                            heater_on = True
                        elif temperature > (target + tolerance):
                            lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
                            heater_on = False

                except RuntimeError as err:
                    print(f"Sensor error: {err}")

        time.sleep(2)

threading.Thread(target=sensor_loop, daemon=True).start()

class SettingsModel(BaseModel):
    FAN_PIN: int
    HEATER_PIN: int
    TARGET_TEMP: float
    TOLERANCE: float

class SystemStateModel(BaseModel):
    on: bool



@app.get("/settings")
def get_settings():
    return SETTINGS

@app.post("/settings")
def update_settings(new_settings: SettingsModel):
    global heater_on, fan_on
    with lock:
        for key, value in new_settings.dict().items():
            SETTINGS[key] = value

        lgpio.gpio_write(h, SETTINGS["FAN_PIN"], 1)
        lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
        fan_on = True
        heater_on = False

    return { "status": "updated", "settings": SETTINGS }

@app.get("/system")
def get_system_state():
    return {
        "system_on": system_on,
        "fan_on": fan_on,
        "heater_on": heater_on
    }

@app.post("/system")
def set_system_state(state: SystemStateModel):
    global system_on, fan_on, heater_on
    with lock:
        system_on = state.on

        if system_on:
            lgpio.gpio_write(h, SETTINGS["FAN_PIN"], 1)
            fan_on = True

            lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
            heater_on = False

        else:
            lgpio.gpio_write(h, SETTINGS["FAN_PIN"], 0)
            lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
            fan_on = False
            heater_on = False

        return { "status": "ok", "system_on": system_on }


@app.get("/history")
def get_history():
    return list(history)

if __name__ == "__main__":
    uvicorn.run("dryer_api:app", host="0.0.0.0", port=8000)
