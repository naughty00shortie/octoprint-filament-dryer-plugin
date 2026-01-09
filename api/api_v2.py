import time
import threading
from collections import deque
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import adafruit_dht
import board
import lgpio
import atexit
import uvicorn

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# Initial settings
SETTINGS = {
    "FAN_PIN": 17,
    "HEATER_PIN": 27,
    "TARGET_TEMP": 65.0,
    "TOLERANCE": 1.0
}

# GPIO setup
h = lgpio.gpiochip_open(0)
lgpio.gpio_claim_output(h, SETTINGS["FAN_PIN"])
lgpio.gpio_claim_output(h, SETTINGS["HEATER_PIN"])
lgpio.gpio_write(h, SETTINGS["FAN_PIN"], 0)

# Sensor setup
dht_device = adafruit_dht.DHT22(board.D4)

# Shared state
system_on = False
fan_on = False
heater_on = False
lock = threading.Lock()
history = deque(maxlen=1800)

# Cleanup on exit
def cleanup():
    if h:
        lgpio.gpio_write(h, SETTINGS["FAN_PIN"], 0)
        lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
        lgpio.gpiochip_close(h)

atexit.register(cleanup)


def sensor_loop():
    global system_on, heater_on
    failure_count = 0

    while True:
        try:
            temperature = dht_device.temperature
            humidity = dht_device.humidity

            if temperature is not None and humidity is not None:
                failure_count = 0  # reset on success

                with lock:
                    if system_on:
                        target = SETTINGS["TARGET_TEMP"]
                        tolerance = SETTINGS["TOLERANCE"]

                        if temperature < (target - tolerance):
                            lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 1)
                            heater_on = True
                        elif temperature > (target + tolerance):
                            lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
                            heater_on = False
                    else:
                        target = 0.0
                        lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
                        heater_on = False

                    history.append({
                        "timestamp": time.time(),
                        "target_temp": target,
                        "actual_temp": temperature,
                        "humidity": humidity
                    })

            else:
                failure_count += 1
                print("Warning: Sensor returned None values.")

        except Exception as e:
            failure_count += 1
            print(f"[ERROR] Sensor read failed: {e}")

        if failure_count >= 10:
            print("Too many sensor failures. Trying to reinitialize sensor...")
            try:
                dht_device.exit()
                time.sleep(2)
                dht_device = adafruit_dht.DHT22(board.D4)
                failure_count = 0
            except Exception as e:
                print(f"Reinitialization failed: {e}")

        time.sleep(2)


threading.Thread(target=sensor_loop, daemon=True).start()

# Models
class SettingsModel(BaseModel):
    FAN_PIN: int
    HEATER_PIN: int
    TARGET_TEMP: float
    TOLERANCE: float

class SystemStateModel(BaseModel):
    on: bool

# API Routes
@app.get("/settings")
def get_settings():
    return SETTINGS

@app.post("/settings")
def update_settings(new_settings: SettingsModel):
    with lock:
        for key, value in new_settings.dict().items():
            SETTINGS[key] = value
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
            # Turn everything ON
            lgpio.gpio_write(h, SETTINGS["FAN_PIN"], 1)
            fan_on = True

            # Let the heater logic manage itself in the background loop
            # But turn it off for now until temp is read
            lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
            heater_on = False

        else:
            # Turn everything OFF
            lgpio.gpio_write(h, SETTINGS["FAN_PIN"], 0)
            lgpio.gpio_write(h, SETTINGS["HEATER_PIN"], 0)
            fan_on = False
            heater_on = False

        return { "status": "ok", "system_on": system_on }

@app.get("/history")
def get_history():
    return list(history)

if __name__ == "__main__":
    uvicorn.run("aoi:app", host="0.0.0.0", port=8000)