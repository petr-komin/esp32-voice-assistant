# Mluvící krabička

Hlasový terminál s ESP32: mikrofon, reproduktor a WebSocket na server, který
audio zpracuje (STT → LLM → TTS) a hlasem odpoví. Bez wake wordu a bez displeje,
první aplikací bude ovládání topení. Vzniká postupně jako seriál, každý krok je
samostatné prostředí ve firmwaru.

- `firmware/` – PlatformIO + Arduino, zatím klasické ESP32 (esp32dev),
- `server/` – služba v Go, zatím jen echo.

## Zapojení (klasické ESP32)

| ESP32   | MAX98357A | INMP441 |
|---------|-----------|---------|
| GPIO 27 | BCLK      |         |
| GPIO 26 | LRC       |         |
| GPIO 25 | DIN       |         |
| GPIO 33 |           | SCK     |
| GPIO 32 |           | WS      |
| GPIO 35 |           | SD      |
| 3V3     |           | VDD     |
| GND     | GND       | GND, L/R |

Zesilovač je zapojený stejně jako v [esp32-i2s-demo](https://github.com/petr-komin/esp32-i2s-demo).
Tlačítko je zatím BOOT (GPIO 0) přímo na desce. Piny jsou v `firmware/include/piny.h`.

## Krok 1: místní echo

Nahraje 3 s z mikrofonu a přehraje je, pořád dokola. Ověří mikrofon i zesilovač bez sítě.

```sh
cd firmware
pio run -e 01_mistni_echo -t upload && pio device monitor
```

V konzoli se vypisuje úroveň nahrávky. Když je špička 0, prohoď v `include/audio.h`
`MIC_CHANNEL` na `I2S_CHANNEL_FMT_ONLY_RIGHT`. Hlasitost mikrofonu se ladí
konstantou `MIC_SHIFT`.

## Krok 2: echo přes server

Server v Dockeru (pro zkoušení v místní síti port otevřený do LAN; na ostrém serveru
`-p 127.0.0.1:8090:8090` a před ním nginx):

```sh
cd server
docker build -t mluvici-krabicka-voice .
mkdir -p nahravky
docker run --rm -p 8090:8090 --user $(id -u):$(id -g) \
  -e VOICE_TOKEN=zmen-me -e RECORD_DIR=/nahravky -v "$PWD/nahravky:/nahravky" \
  mluvici-krabicka-voice
```

`RECORD_DIR` je nepovinný: každá nahrávka se uloží jako WAV, takže jde poslechnout,
co mikrofon opravdu nahrál.

Firmware:

```sh
cd firmware
cp src/02_echo_server/config.example.h src/02_echo_server/config.h   # vyplnit WiFi, IP serveru, token
pio run -e 02_echo_server -t upload && pio device monitor
```

Podrž BOOT a mluv, po puštění se nahrávka vrátí ze serveru a přehraje.

Testy serveru (bez nainstalovaného Go):

```sh
docker run --rm -v "$PWD/server":/src -w /src golang:1.23-alpine go test -v ./...
```

## Protokol krabička ↔ server

WebSocket, textové rámce nesou řídicí JSON, binární rámce audio
(PCM, 16 kHz, 16 bit, mono, po 100 ms).

```
krabička → server
  {"type":"hello","device":"chatka-box","token":"…","fw":"0.1"}
  {"type":"start","format":"pcm16","rate":16000}
  [binární rámce audia]
  {"type":"end"}

server → krabička
  {"type":"ack"}            ← hned po "start": server žije a přijímá
  {"type":"thinking"}       ← po "end": běží zpracování
  {"type":"audio_start","format":"pcm16","rate":16000}
  [binární rámce odpovědi]  ← přehrávají se hned, nečeká se na celou odpověď
  {"type":"done","action":{...}}
  {"type":"error","code":"auth|format|protocol|too_long|..."}
```

Časové limity na krabičce: `ack` do 3 s od `start`, `audio_start` do 15 s od `end`.
Aplikační `ack` je tu proto, že potvrzení TCP neříká, že server data opravdu zpracoval.

## Co zatím chybí

- `wss://` s ověřením certifikátu a nginx (teď obyčejný `ws://` v místní síti),
- LED, offline hlášky, detekce ticha, deep sleep,
- přechod na ESP32-S3.

## Licence

[GPL-3.0](LICENSE)
