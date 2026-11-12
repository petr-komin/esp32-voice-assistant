# Hlasový asistent s ESP32

*English: [README.md](README.md)*

Přenosný hlasový terminál: ESP32 s MEMS mikrofonem, reproduktorem a třemi
kontrolními LED posílá řeč přes WebSocket na server. Ten ji přepíše, nechá
jazykový model provést, co je potřeba (první aplikací je ovládání topení
v chatce), a odpoví hlasem. Bez wake wordu a bez displeje, krabička se probudí
zatřesením a začne poslouchat.

Projekt vzniká po krocích jako seriál na YouTube. Každý krok je funkční
a otestovaný celek.

| Krok | Stav |
|---|---|
| 1. Oživení hardwaru, detekce řeči, místní echo | **hotovo** (ESP-IDF) |
| 2. Echo přes server po WebSocketu | server hotový, firmware se převádí |
| 3. TLS (`wss://`) s ověřením certifikátu, nginx | plán |
| 4. Přepis řeči, LLM s nástroji, syntéza řeči | plán |
| 5. Deep sleep, probuzení zatřesením (LSM6DS3), baterie | plán |
| 6. Podepsané OTA aktualizace, secure boot, šifrování flash | plán |

Architektura, inženýrské postupy a podrobnosti jsou v anglickém
[README.md](README.md). Protokol popisuje [docs/protocol.md](docs/protocol.md),
model hrozeb [SECURITY.md](SECURITY.md).

## Zapojení (vývojová deska ESP32-WROOM-32)

| ESP32 | MAX98357A | INMP441 | LED (přes 220–330 Ω na GND) |
|---|---|---|---|
| GPIO 27 | BCLK | | |
| GPIO 26 | LRC | | |
| GPIO 25 | DIN | | |
| GPIO 33 | | SCK | |
| GPIO 32 | | WS | |
| GPIO 35 | | SD | |
| GPIO 21 | | | zelená: poslouchám (svítí), nahrávám (bliká) |
| GPIO 18 | | | žlutá: zpracovávám (svítí), přehrávám (bliká) |
| GPIO 2 | | | červená: chyba (svítí), bez spojení (bliká) |
| 3V3 | VIN | VDD | |
| GND | GND | GND, L/R | katody |

Piny jdou změnit v `idf.py menuconfig` → *Voice terminal* → *Pin assignment*.

## Překlad a nahrání firmwaru

Stačí Docker. `scripts/idf.sh` spouští `idf.py` v kontejneru ESP-IDF
a předává do něj sériové porty.

```sh
scripts/idf.sh set-target esp32        # jednou
scripts/idf.sh build
scripts/idf.sh -p /dev/ttyUSB0 flash
scripts/host-tests.sh                  # unit testy na PC
```

Ve druhém okně terminálu nech běžet `scripts/monitor.py`. Při nahrávání uvolní
sériový port, po nahrání se znovu připojí a resetuje desku, takže uvidíš celý
výpis od startu. Výstup se zároveň ukládá do `logs/serial.log`.

Promluv u mikrofonu. Při nahrávání bliká zelená, pak se nahrávka přehraje
a bliká žlutá. Citlivost detekce řeči se ladí v `menuconfig` → *Voice terminal*
→ *Voice activity detection*.

## Server

```sh
cd server
docker build -t voice-server .
docker run --rm -p 127.0.0.1:8090:8090 -e VOICE_TOKEN="$(openssl rand -hex 16)" voice-server
```

`VOICE_TOKEN` je povinný (aspoň 16 znaků). `RECORD_DIR` je nepovinný: každá
nahrávka se do něj uloží jako WAV.

## Licence

[GPL-3.0](LICENSE)
