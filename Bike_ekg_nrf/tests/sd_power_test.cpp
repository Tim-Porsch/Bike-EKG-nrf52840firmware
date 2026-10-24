#include "../SdService.h"
// Projektspezifisches Power-/DET-Modell: Karte erst nach eingeschwungener Versorgung
// erkennen. Prüft Initialisierung/Fehler-Retry, nicht echte elektrische Pegel.
#include <SPI.h>
#include <assert.h>
#include <string.h>

int main() {
  SdService sd;
  // Cold start with inserted card: old code reads DET while unpowered and fails.
  assert(!testPowered);
  assert(sd.begin());
  assert(testPowered && testDetectReads == 1 && testMounts == 1);
  assert(!strcmp(sd.lastError(), "none"));
  assert(sd.begin()); // Already mounted: no power cycle, delay or extra mount.
  assert(testDetectReads == 1 && testMounts == 1 && testSpiSetups == 1);
  sd.end();
  assert(!testPowered && testUnmounts == 1);
  assert(sd.begin()); // Repeated access works without reinserting the card.
  sd.end();

  testCardInserted = false;
  const int mounts = testMounts;
  assert(!sd.begin());
  assert(!testPowered && testMounts == mounts);
  assert(!strcmp(sd.lastError(), "SD card not detected"));
  sd.end(); // Failed detection leaves nothing mounted or powered.
  assert(!testPowered);

  testCardInserted = true;
  testMountError = true;
  assert(!sd.begin());
  assert(!testPowered);
  assert(!strcmp(sd.lastError(), "SD initialization failed"));
  testMountError = false;
  assert(sd.begin()); // Retry after initialization failure.
  sd.end();
  assert(!testPowered);
}
