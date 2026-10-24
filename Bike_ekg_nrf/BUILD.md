# Buildkonfiguration – Firmware 0.12.5

Buildstand: 24.10.2026. Die folgende Konfiguration wurde für die beigefügten
Release-Images verwendet.

## Board, Tools und Bibliotheken

| Komponente | Version / Einstellung |
| --- | --- |
| Boardauswahl | Adafruit Feather nRF52840 Express |
| Boardpaket | `adafruit:nrf52@1.7.0` |
| FQBN | `adafruit:nrf52:feather52840:softdevice=s140v6,debug=l0,debug_output=serial` |
| Variante | `feather_nrf52840_express` |
| SoftDevice-API / DFU-Anforderung | S140 6.1.1 / `0x00B6` |
| Debug / Ausgabe | Level 0 (Release) / Serial |
| Arduino-IDE | 2.3.6, Flatpak `cc.arduino.IDE2` |
| Arduino-CLI | 1.2.0, Commit `9c495211`, 24.02.2025 |
| ARM-Toolchain | `9-2019q4`: GCC 9.2.1 20191025 |
| CMSIS | 5.7.0 |
| adafruit-nrfutil | 0.5.3.post16 |
| Adafruit TinyUSB Library | 3.6.0, im Boardpaket |
| SPI | 1.0, im Boardpaket |
| Adafruit Bluefruit nRF52 Libraries | 0.21.0, im Boardpaket |
| Adafruit nRFCrypto | 0.1.2, transitiv durch Bluefruit |
| Adafruit Little File System Libraries | 0.11.0, im Boardpaket |
| Adafruit Internal File System on Bluefruit nRF52 | 0.11.0, im Boardpaket; Verzeichnis heißt dort `InternalFileSytem` |
| SdFat | 2.3.0, externe Bibliothek |

Das Boardpaket bestimmt auch Nordic-/FreeRTOS-Komponenten und die gemeinsam
ausgelieferten Bibliotheken. Diese nicht separat auf neuere Versionen aktualisieren,
wenn der dokumentierte Stand nachgebaut werden soll. C/C++-Flags stammen aus
`platform.txt` des Boardpakets (u. a. `-Ofast`, `gnu++11`, Cortex-M4 Hard-Float).

### SdFat: zwingend Chip-Select-Modus 1

`SD_CHIP_SELECT_MODE` muss auf **1** gesetzt sein. In diesem Modus können die
schwachen SdFat-Standardfunktionen
durch `sdCsInit` und `sdCsWrite` aus `SdService.cpp` ersetzt werden. Diese steuern
P1.11 direkt. Modus 0 ist für diesen Sketch nicht die dokumentierte Konfiguration.

Der Build-Runner setzt zusätzlich `-DSD_CHIP_SELECT_MODE=1` für alle
Übersetzungseinheiten, einschließlich der Bibliothek. Ein `#define` nur in der
`.ino` wäre hierfür nicht ausreichend. Der Runner setzt außerdem
`-ffile-prefix-map`, damit lokale Benutzerpfade nicht in Debugdaten oder
`__FILE__`-Zeichenketten veröffentlicht werden. Die Firmwarelogik wird nicht geändert.

## Build wiederholen

Den vollständigen Ordner `Bike_ekg_nrf` beibehalten. Voraussetzung sind Arduino-CLI
1.2.0, das Boardpaket und SdFat in den oben genannten Versionen. Bei einer neuen
Umgebung können die Versionen mit folgenden Befehlen installiert werden
(Netzwerkzugriff erforderlich):

```bash
arduino-cli core update-index --additional-urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
arduino-cli core install adafruit:nrf52@1.7.0 --additional-urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
arduino-cli lib install 'SdFat@2.3.0'
```

Unter Linux muss `adafruit-nrfutil` im PATH verfügbar sein; sonst scheitert nach
erfolgreichem Linken die DFU-Paketierung. Die verwendete Flatpak-Umgebung
enthält das Tool bereits. Der Compiler selbst wird vom Boardpaket bereitgestellt.

Aus dem Repository-Hauptverzeichnis, mit normal installierter CLI:

```bash
python3 Bike_ekg_nrf/tools/build_firmware.py \
  --build-path /tmp/bike-ekg-build \
  --output /tmp/bike-ekg-artifacts
```

Alternativ mit der für den Release-Build verwendeten Arduino-IDE-Flatpak-Installation:

```bash
python3 Bike_ekg_nrf/tools/build_firmware.py --flatpak \
  --config-file "$HOME/.arduinoIDE/arduino-cli.yaml" \
  --build-path /tmp/bike-ekg-build \
  --output /tmp/bike-ekg-artifacts
```

Der erste Befehl unterscheidet sich nur im Aufrufweg der CLI und den lokalen
Pfaden; validiert wurde der Flatpak-Aufruf. Der Runner erstellt einen Clean-Build
und führt **keinen Upload** aus. Rohlogs verbleiben lokal neben dem Buildordner.
Im Release liegt ein um private Pfade bereinigtes Buildprotokoll.

`dependencies/sources.zip` im Repository-Hauptverzeichnis enthält einen Snapshot der
tatsächlich verwendeten Boardquellen, benötigten CMSIS-Komponenten und SdFat, einschließlich deren
Originalhinweise und der lokalen SdFat-Konfiguration. Er ist für die Quellprüfung
und den Bibliotheksnachbau maßgeblich; Toolchain und CLI sind nicht mitgeliefert.
Die Installation aus dem Netz allein ist kein Nachweis eines bitidentischen
Neubaus. Ein Build in einer vollständig neu installierten Umgebung wurde nicht
zusätzlich durchgeführt.

## Buildprodukte und Flashgrenzen

Der Release-Build belegt laut Arduino-CLI **189.308 Byte Programmspeicher**
(23 % von 815.104) und **52.788 Byte statischen RAM** (22 % von 237.568).
Diese RAM-Angabe erfasst keine gemessene maximale Stack-/Heapbelegung.

`firmware/` im Repository enthält:

- `Bike_ekg_nrf.ino.hex`: Intel-HEX der Anwendung;
- `Bike_ekg_nrf.ino.zip`: von adafruit-nrfutil erzeugtes Application-DFU-Paket;
- `build.log`: bereinigtes Protokoll des erfolgreichen Builds.

Die Images enthalten **nicht** Bootloader oder SoftDevice und sind kein
vollständiges Erstprogrammierungsimage für einen leeren nRF52840.
DFU setzt einen kompatiblen Bootloader und die passende SoftDevice-Konfiguration
voraus. Eine bestimmte Bootloaderversion oder ein verifizierter Flashablauf
werden mit diesem Paket nicht vorgegeben. Keinen fremden Bootloader oder
SoftDevice auf Verdacht flashen.

Mit Version 0.12.5 wurden SD-Aufzeichnung, USB-Konsole/Streaming,
BLE-Steuerung/Vorschau und BLE-SD-Download auf der Projektplatine erprobt.
Die beigefügten Release-Images wurden nicht erneut auf Hardware getestet.
Für den Displaymodus und systematische Fehlerfalltests liegen keine
dokumentierten Hardwaretests vor.

Für LGPL-Neulinken siehe `relink/README.md` im Repository-Hauptverzeichnis.
SHA-256-Prüfsummen dienen dem Bytevergleich, nicht als Signatur.
