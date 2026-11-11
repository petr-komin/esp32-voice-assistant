// Místní echo: 3 s nahraje z mikrofonu a pak je přehraje. Pořád dokola.
// Nahrávka a přehrání se střídají, takže reproduktor nepíská zpětnou vazbou.

#include <Arduino.h>
#include "audio.h"

const int RECORD_SECONDS = 3;

int16_t* recording;
size_t recordingSamples;

void setup() {
  Serial.begin(115200);
  speakerInit();
  micInit();

  // Bez PSRAM je souvislý blok paměti omezený, 3 s = 96 kB se většinou vejdou.
  recordingSamples = SAMPLE_RATE * RECORD_SECONDS;
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (recordingSamples * 2 > largest - 8192) {
    recordingSamples = (largest - 8192) / 2;
  }
  recording = (int16_t*)malloc(recordingSamples * 2);
  if (!recording) {
    Serial.println("Chyba: nedostatek paměti");
    while (true) delay(1000);
  }
  Serial.printf("Nahrávka %.1f s\n", (float)recordingSamples / SAMPLE_RATE);
}

void loop() {
  Serial.println("Mluv...");
  micFlush();
  micRead(recording, recordingSamples);
  printLevel(recording, recordingSamples);

  Serial.println("Přehrávám");
  speakerWrite(recording, recordingSamples);
  delay(500);
}
