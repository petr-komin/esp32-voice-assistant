#pragma once

// Zapojení pro klasické ESP32 (esp32dev).
// Na ESP32-S3 tyhle piny neplatí, při přechodu se budou měnit.

// Zesilovač MAX98357A, stejně jako v esp32-i2s-demo.
const int SPK_BCLK = 27;
const int SPK_LRC = 26;
const int SPK_DIN = 25;

// Mikrofon INMP441. L/R na GND = levý kanál, VDD na 3V3.
const int MIC_SCK = 33;
const int MIC_WS = 32;
const int MIC_SD = 35;  // pin jen pro vstup, na data z mikrofonu stačí

// Tlačítko BOOT na desce, při stisku LOW.
const int BUTTON_PIN = 0;
