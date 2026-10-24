# Quellen der tatsächlich verwendeten Abhängigkeiten

`sources.zip` enthält die beim Release-Build verwendeten Bibliotheks- und Boardquellen:

- `adafruit-nrf52-1.7.0/`: Core, Varianten, sechs verwendete Bibliotheksordner,
  `boards.txt`, `platform.txt` und Original-Lizenz. Keine fremden Projektordner,
  Bootloaderimages oder SoftDeviceimages.
- `SdFat-2.3.0/`: Quellordner, Bibliotheksmetadaten und Lizenz. Die tatsächlich
  verwendete `SdFatConfig.h` enthält `SD_CHIP_SELECT_MODE=1`.
- `CMSIS-5.7.0/`: benötigte Core-/DSP-Header, DSP-Quellen, ComputeLibrary,
  DSP-Projektdateien, passendes Cortex-M4-Hard-Float-Archiv, Paketmetadaten
  und Lizenz. Nicht enthalten sind unbenötigte CMSIS-Dokumentation,
  Beispielprogramme und Archive für andere CPU-Varianten.

Die Begrenzung auf benötigte Komponenten reduziert das Paket; alle enthaltenen
Originalhinweise bleiben erhalten.

Für die Quellprüfung das ZIP in einen eigenen Ordner entpacken. Für einen
Nachbau zuerst die in `Bike_ekg_nrf/BUILD.md` dokumentierte Toolumgebung
installieren. Falls die installierten Bibliotheksquellen abweichen, sind die
beigefügten Quellen des damaligen Builds zum Vergleich verfügbar. Diese
Dateien werden hier nicht automatisch in eine vorhandene Arduino-Installation
kopiert oder dortige eigene Änderungen überschrieben.

Originalbezugsquellen der Pakete:

- Adafruit-Boardindex: <https://adafruit.github.io/arduino-board-index/package_adafruit_index.json>
- Boardpaket: <https://adafruit.github.io/arduino-board-index/boards/adafruit-nrf52-1.7.0.tar.bz2>
- SdFat: <https://github.com/greiman/SdFat>
- CMSIS: <https://github.com/ARM-software/CMSIS_5>

Diese Links dienen der Herkunftsangabe, nicht als Zusicherung dauerhafter
Erreichbarkeit oder als Ersatz für die tatsächlich mitgelieferten Quellen.
