// Echo přes server: podržíš BOOT a mluvíš, po puštění server pošle audio zpět.
// Protokol je popsaný v README.md v kořeni repozitáře.

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "audio.h"
#include "config.h"

const char* FW_VERSION = "0.1";
const size_t FRAME_SAMPLES = SAMPLE_RATE / 10;      // 100 ms audia v jednom rámci
const unsigned long MAX_RECORD_MS = 30000;          // server víc nepřijme
const unsigned long ACK_TIMEOUT_MS = 3000;          // od "start" do "ack"
const unsigned long AUDIO_START_TIMEOUT_MS = 15000;  // od "end" do "audio_start"

enum State { OFFLINE, IDLE, RECORDING, WAITING, PLAYING };

WebSocketsClient ws;
State state = OFFLINE;
unsigned long stateSince = 0;
bool ackReceived = false;
size_t bytesSent = 0;
size_t bytesReceived = 0;
int16_t frame[FRAME_SAMPLES];

void setState(State s) {
  state = s;
  stateSince = millis();
}

unsigned long inState() {
  return millis() - stateSince;
}

void sendJson(JsonDocument& doc) {
  String text;
  serializeJson(doc, text);
  ws.sendTXT(text);
}

void handleText(const char* text, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, text, len)) {
    Serial.printf("Nečitelná zpráva: %.*s\n", (int)len, text);
    return;
  }
  const char* type = doc["type"] | "";

  if (strcmp(type, "ack") == 0) {
    ackReceived = true;
  } else if (strcmp(type, "thinking") == 0) {
    Serial.println("Server zpracovává...");
  } else if (strcmp(type, "audio_start") == 0) {
    Serial.printf("Přehrávám odpověď (%lu ms od konce řeči)\n", inState());
    bytesReceived = 0;
    setState(PLAYING);
  } else if (strcmp(type, "done") == 0) {
    Serial.printf("Hotovo, přijato %u B audia\n", bytesReceived);
    setState(IDLE);
  } else if (strcmp(type, "error") == 0) {
    Serial.printf("Chyba ze serveru: %s\n", (const char*)(doc["code"] | "?"));
    setState(IDLE);
  }
}

void onEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED: {
      Serial.println("Připojeno k serveru");
      JsonDocument hello;
      hello["type"] = "hello";
      hello["device"] = "chatka-box";
      hello["token"] = DEVICE_TOKEN;
      hello["fw"] = FW_VERSION;
      sendJson(hello);
      setState(IDLE);
      Serial.println("Podrž BOOT a mluv");
      break;
    }
    case WStype_DISCONNECTED:
      if (state != OFFLINE) {
        if (state != IDLE) Serial.println("Chyba: spojení spadlo uprostřed");
        Serial.println("Odpojeno od serveru");
      }
      setState(OFFLINE);
      break;
    case WStype_TEXT:
      handleText((const char*)payload, len);
      break;
    case WStype_BIN:
      if (state == PLAYING) {
        // Přehrává se rovnou, i2s_write počká, dokud se data nevejdou do DMA.
        speakerWrite((const int16_t*)payload, len / 2);
        bytesReceived += len;
      }
      break;
    default:
      break;
  }
}

void connectWifi() {
  Serial.printf("Připojuji k WiFi %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\nIP adresa: %s\n", WiFi.localIP().toString().c_str());
}

void startRecording() {
  micFlush();
  JsonDocument start;
  start["type"] = "start";
  start["format"] = "pcm16";
  start["rate"] = SAMPLE_RATE;
  sendJson(start);
  ackReceived = false;
  bytesSent = 0;
  setState(RECORDING);
  Serial.println("Nahrávám...");
}

void stopRecording() {
  JsonDocument end;
  end["type"] = "end";
  sendJson(end);
  Serial.printf("Odesláno %u B (%.1f s)\n", bytesSent, bytesSent / 2.0f / SAMPLE_RATE);
  setState(WAITING);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  speakerInit();
  micInit();
  connectWifi();

  ws.begin(SERVER_HOST, SERVER_PORT, SERVER_PATH);
  ws.onEvent(onEvent);
  ws.setReconnectInterval(3000);
}

void loop() {
  ws.loop();
  bool pressed = digitalRead(BUTTON_PIN) == LOW;

  switch (state) {
    case OFFLINE:
    case PLAYING:
      break;

    case IDLE:
      if (pressed) startRecording();
      break;

    case RECORDING:
      // micRead čeká 100 ms, tím je zároveň ošetřený zákmit tlačítka.
      micRead(frame, FRAME_SAMPLES);
      ws.sendBIN((uint8_t*)frame, sizeof(frame));
      bytesSent += sizeof(frame);

      if (!ackReceived && inState() > ACK_TIMEOUT_MS) {
        Serial.println("Chyba: server nepotvrdil začátek (ack)");
        ws.disconnect();
        setState(OFFLINE);
      } else if (!pressed || inState() > MAX_RECORD_MS) {
        stopRecording();
      }
      break;

    case WAITING:
      if (inState() > AUDIO_START_TIMEOUT_MS) {
        Serial.println("Chyba: odpověď nepřišla včas");
        ws.disconnect();
        setState(OFFLINE);
      }
      break;
  }
}
