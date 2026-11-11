#pragma once

// Zkopíruj do config.h a vyplň. config.h se do gitu nedává.

const char* WIFI_SSID = "nazev-site";
const char* WIFI_PASSWORD = "heslo";

// Zatím obyčejný ws:// v místní síti, wss:// přes nginx přijde později.
const char* SERVER_HOST = "192.168.1.10";
const uint16_t SERVER_PORT = 8090;
const char* SERVER_PATH = "/voice";

// Musí se shodovat s VOICE_TOKEN na serveru.
const char* DEVICE_TOKEN = "zmen-me";
