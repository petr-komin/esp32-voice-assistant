# ESP32 voice assistant

[![CI](https://github.com/petr-komin/esp32-voice-assistant/actions/workflows/ci.yml/badge.svg)](https://github.com/petr-komin/esp32-voice-assistant/actions/workflows/ci.yml)

*Česky: [README.cs.md](README.cs.md)*

A battery-powered voice terminal: an ESP32 with a MEMS microphone, a speaker and
three status LEDs streams speech over a WebSocket to a server, which transcribes
it, lets an LLM act on it (the first application is heating control for a
cottage) and answers with synthesised speech. No wake word and no display: the
device wakes on a shake and starts listening.

The project is built step by step; each step is a working, tested system.

| Step | Status |
|---|---|
| 1. Hardware bring-up, voice activity detection, local echo | **done** (ESP-IDF) |
| 2. Echo through the server over WebSocket | server done, firmware port in progress |
| 3. TLS (`wss://`) with certificate verification, nginx | planned |
| 4. Speech-to-text, LLM with tools, text-to-speech | planned |
| 5. Deep sleep, wake on shake (LSM6DS3), battery | planned |
| 6. Signed OTA updates, secure boot, flash encryption | planned |

## Architecture

```
┌──────────── ESP32 ─────────────┐        ┌────────── server (Docker) ──────────┐
│ INMP441 ─I2S1─▶ dsp ─▶ vad ─┐  │  wss   │  nginx (TLS) ─▶ voice service (Go)  │
│                             ├─▶├───────▶│      STT ─▶ LLM + tools ─▶ TTS      │
│ MAX98357A ◀─I2S0─ playback ◀┘  │        │                 │                   │
│ LEDs ◀─ status_led             │        │        heating schedule, sensors    │
└────────────────────────────────┘        └─────────────────────────────────────┘
```

* **firmware/** – ESP-IDF 6.0 (C11), FreeRTOS tasks, one component per concern:
  * `dsp` – microphone sample conversion, DC blocking, levels (pure C, host-tested)
  * `vad` – energy-based voice activity detection with adaptive noise floor and
    pre-roll buffer (pure C, host-tested)
  * `audio` – I2S capture and playback on the `i2s_std` driver
  * `status_led` – device state on three LEDs, timer-driven blinking
  * `main` – application; pins and tuning in `menuconfig` (*Voice terminal* menu)
* **server/** – Go service, WebSocket protocol, static binary on `scratch`.
* **docs/protocol.md** – device ↔ server protocol.
* **SECURITY.md** – threat model and vulnerability reporting.

## Engineering practices

* Reproducible builds: firmware is built in the pinned `espressif/idf:v6.0.3`
  container, the same locally and in CI.
* Warnings as errors (`-Wall -Wextra -Wshadow -Wdouble-promotion …`), stack
  protector, FreeRTOS stack canaries, task watchdog, brown-out detection.
* Hardware-independent logic is unit-tested on the host with Unity under
  AddressSanitizer and UBSan.
* `clang-format` and `cppcheck` for the firmware; `gofmt`, `go vet`, race
  detector and `govulncheck` for the server.
* No dynamic allocation in the audio path; buffers are sized at start-up.
* CI on every push; GitHub Actions pinned to commit SHAs, Dependabot updates.

## Hardware (ESP32-WROOM-32 dev board)

| ESP32 | MAX98357A | INMP441 | LEDs (via 220–330 Ω to GND) |
|---|---|---|---|
| GPIO 27 | BCLK | | |
| GPIO 26 | LRC | | |
| GPIO 25 | DIN | | |
| GPIO 33 | | SCK | |
| GPIO 32 | | WS | |
| GPIO 35 | | SD | |
| GPIO 21 | | | green – listening (on), recording (blinking) |
| GPIO 18 | | | yellow – busy (on), playing (blinking) |
| GPIO 2 | | | red – error (on), offline (blinking) |
| 3V3 | VIN | VDD | |
| GND | GND | GND, L/R | cathodes |

All pins can be changed in `idf.py menuconfig` → *Voice terminal* → *Pin assignment*.

## Building the firmware

Requires Docker only; `scripts/idf.sh` runs `idf.py` in the ESP-IDF container
and passes serial ports through.

```sh
scripts/idf.sh set-target esp32        # once
scripts/idf.sh build
scripts/idf.sh -p /dev/ttyUSB0 flash
scripts/host-tests.sh                  # unit tests on the PC
```

Keep `scripts/monitor.py` running in a second terminal: it releases the serial
port while `scripts/idf.sh` flashes, then reconnects and resets the board so the
full boot log is shown. Output is also appended to `logs/serial.log`.

Speak near the microphone: the green LED blinks while recording, then the
recording is played back while the yellow LED blinks. Voice activity detection
is tuned in `menuconfig` → *Voice terminal* → *Voice activity detection*.

## Running the server

```sh
cd server
docker build -t voice-server .
docker run --rm -p 127.0.0.1:8090:8090 -e VOICE_TOKEN="$(openssl rand -hex 16)" voice-server
```

| Variable | Meaning |
|---|---|
| `VOICE_TOKEN` | shared device token, at least 16 characters (required) |
| `LISTEN_ADDR` | default `:8090` |
| `RECORD_DIR` | optional; every utterance is saved there as WAV for debugging |

In production the container port is published on localhost only and nginx
terminates TLS in front of it (`/voice` location with WebSocket upgrade).

## License

[GPL-3.0](LICENSE)
