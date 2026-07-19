---
name: flash-filament-dryer
description: Build and OTA-flash new firmware to the ESP8266 filament dryer controller (api/sketch_sep03a) from the command line using arduino-cli, without needing the Arduino IDE open. Use this whenever the user asks to flash, upload, push, deploy, or OTA-update firmware to the filament dryer, or wants to test a sketch change on the real device — even if they just say "flash it" or "can you push this to the dryer" without spelling out the details.
---

# Flashing the filament dryer over OTA

The filament dryer is a physical ESP8266 board (no USB connection in normal
operation) that only accepts firmware updates over WiFi via ArduinoOTA. This
skill covers doing that from the command line with `arduino-cli`, which lets
you build and push a change without the user needing the Arduino IDE open.

## Why arduino-cli instead of PlatformIO

This repo has no `platformio.ini`, and the device's flash layout (1MB, tight
~470KB OTA partition) is delicate to get right by hand-picking a PlatformIO
board target and linker script. `arduino-cli` shares Arduino IDE's own board
definitions and its `~/.arduino15` data directory, so it reproduces the exact
board config already known to work for this device with zero guesswork.

## One-time setup (skip if already done)

Check first — if `arduino-cli board list` already shows a Network Port for
this device, setup is done and you can skip to "Every time you flash".

```bash
scoop install arduino-cli
arduino-cli config init
arduino-cli config set board_manager.additional_urls http://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
```

Because this shares the IDE's directory, the libraries the sketch needs (DHT
sensor library, ArduinoJson, Adafruit Unified Sensor) are typically already
installed — check with `arduino-cli lib list` before trying to install
anything, so you don't fetch a newer version that silently changes behavior
under the sketch's feet.

## The board config — don't hand-pick options

The FQBN is `esp8266:esp8266:generic` ("Generic ESP8266 Module"). Before
assuming anything about flash size, crystal frequency, flash mode, etc.,
confirm this device's Arduino IDE Tools-menu settings still look like what's
recorded below (ask the user for a screenshot, or check
`arduino-cli board details -b esp8266:esp8266:generic -f` for the current
defaults) — a board swap is the one thing that would change this:

| Setting | Value |
|---|---|
| Flash Size | 1MB (FS:64KB OTA:~470KB) → `eesz=1M64` |
| Flash Mode | DOUT (compatible) → `FlashMode=dout` |
| Flash Frequency | 40MHz |
| Crystal Frequency | 26MHz |
| CPU Frequency | 80MHz |
| Reset Method | dtr (nodemcu) |
| lwIP Variant | v2 Lower Memory |
| MMU | 32KB cache + 32KB IRAM (balanced) |
| Builtin LED | GPIO2 |

As of this writing, every one of these is already the arduino-cli default for
`esp8266:esp8266:generic` — so the plain FQBN with no `--board-options` is
correct. If a future core update or board swap changes a default, pass the
differing option explicitly (e.g. `--board-options FlashMode=dout`) rather
than letting it silently drift, since a mismatched flash mode/frequency here
is what causes a device to fail to boot after flashing (recoverable only via
a serial reflash, which requires physically opening up the dryer).

## Sketch layout — non-negotiable folder naming

Arduino requires a sketch's containing folder to have the exact same name as
its `.ino` file. The source lives at:

```
api/sketch_sep03a/sketch_sep03a.ino
api/sketch_sep03a/secrets.h            (gitignored — real WiFi/OTA credentials)
api/sketch_sep03a/secrets.h.example    (committed template)
```

If you ever see a "main file missing from sketch" error, or find the `.ino`
sitting directly in `api/` instead of `api/sketch_sep03a/`, that's this rule
being violated — fix the folder structure before anything else, and make
sure `secrets.h` moves along with the `.ino` (Arduino only searches a
sketch's own folder for local `#include`s, not parent directories, so a
`secrets.h` left behind in the wrong folder fails silently at compile time
with "file not found" rather than something more informative).

## Every time you flash

**1. Find the device.** It advertises itself over mDNS as `filament-dryer`.
Don't assume which network port is it — this LAN has several other ESP
boards (wifi-relay, wifi-relay-2, esp32-flow) that also show up in
`arduino-cli board list`. Match by hostname:

```bash
ping filament-dryer.local          # confirms current IP
arduino-cli board list             # should show that IP as a Network Port
```

**2. Safety check before touching anything.** Query the device's own state:

```bash
curl -s http://<device-ip>/system
```

Confirm `heater_on` is `false` before flashing. ArduinoOTA blocks the sketch's
main `loop()` for the whole duration of the flash write (10-30 seconds), so
if the heater relay happens to be on when the transfer starts, it stays
energized with no safety-loop supervision until the device reboots. If
`heater_on` is `true`, either wait a bit for the hysteresis cycle to turn it
off on its own, or shut the whole system down first:
`curl -X POST -d '{"on":false}' http://<device-ip>/system`.

**3. Compile.**

```bash
arduino-cli compile --fqbn esp8266:esp8266:generic "api/sketch_sep03a"
```

Watch the "Code in flash" line in the output — it needs to stay under the
~470KB OTA partition (roughly 481,280 bytes). At last check the sketch used
about 327KB, so there's headroom, but flag it to the user if a change pushes
it noticeably closer to the limit.

**4. Upload over OTA.** The OTA password lives in `secrets.h` as
`otaPassword` — read it from that file rather than assuming it hasn't
changed:

```bash
arduino-cli upload --fqbn esp8266:esp8266:generic -p <device-ip> \
  -F password=<otaPassword> "api/sketch_sep03a"
```

A successful run prints `Authenticating...OK`, a long progress bar, then
`New upload port: <ip> (network)`. Any auth failure means the password in
`secrets.h` is stale relative to what's actually flashed on the device.

**5. Confirm it came back up.** The device needs a few seconds to reboot.
Poll until it responds:

```bash
for i in 1 2 3 4 5 6; do
  sleep 3
  curl -s -m 3 http://<device-ip>/system && break
  echo "not up yet, retrying..."
done
```

A fresh boot always reports `system_on: false` and `safety_latched: false` —
state isn't persisted across reboots, so any previously stuck safety latch
clears automatically as a side effect. This also means the dryer needs to be
turned back on afterward (via OctoPrint or `POST /system {"on": true}`) —
mention this to the user rather than assuming it resumed on its own.

## Committing sketch changes

If the flash follows a code change, commit the sketch changes as normal.
Double-check `git status` doesn't include `secrets.h` — it's gitignored, but
if the sketch folder ever gets restructured again, re-verify with
`git check-ignore -v api/sketch_sep03a/secrets.h` before committing anything.
