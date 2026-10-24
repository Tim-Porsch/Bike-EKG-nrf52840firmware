# Bike-EKG – nRF52840-Firmware

Firmware **0.12.5** für das Bike-EKG-Messsystem der Bachelorarbeit von Tim Porsch.
Die Firmware erfasst einen Kanal des ADS1291, speichert Rohdaten auf SD und
stellt USB- und BLE-Schnittstellen für Steuerung, Vorschau und Dateitransport bereit.

**Forschungsprototyp für kontrollierte Prüfaufbauten. Kein zugelassenes
Medizinprodukt und kein Diagnosewerkzeug.** Die Pinbelegung ist auf die
Projektplatine abgestimmt, nicht auf beliebige nRF52840-Boards.

## Dokumentation

- [Firmware und Bedienung](Bike_ekg_nrf/README.md)
- [Hardware-, Board- und Buildkonfiguration](Bike_ekg_nrf/BUILD.md)
- [BLE-Livevorschau](Bike_ekg_nrf/PREVIEW_PROTOCOL.md)
- [BLE-SD-Dateitransport](Bike_ekg_nrf/SD_FILE_PROTOCOL.md)
- [BLE-Displayprotokoll](Bike_ekg_nrf/DISPLAY_PROTOCOL.md)
- [Nutzungsrechte](LICENSE.md) und [Drittkomponenten](Bike_ekg_nrf/THIRD_PARTY_NOTICES.md)

## Repository-Inhalt

| Pfad | Inhalt |
| --- | --- |
| `Bike_ekg_nrf/` | Arduino-Sketch, C++-Quellen, Dokumentation, Hosttests und Buildskript |
| `firmware/` | Anwendung als Intel-HEX und DFU-ZIP sowie Buildprotokoll |
| `dependencies/` | Verwendete Bibliotheks- und Boardquellen mit Originalhinweisen |
| `licenses/` | Lizenztexte der Drittkomponenten |
| `relink/` | Objektdateien und Skript zum erneuten Linken der statisch verbundenen Anwendung |
| `BUILDINFO.json` | Maschinenlesbare Buildkonfiguration und Image-Prüfsumme |
| `SHA256SUMS` | SHA-256-Prüfsummen der Paketdateien |

Dieses Repository enthält ausschließlich die Firmware und zugehörige Unterlagen.
Externe Bedienanwendungen, Schaltpläne und Messdaten sind nicht enthalten.

## Build und Tests

Buildkonfiguration: **Adafruit Feather nRF52840 Express**, **Adafruit nRF52
Boards 1.7.0**, **S140-API 6.1.1**, **SdFat 2.3.0**, Chip-Select-Modus **1**.
Die Boardauswahl bezeichnet die Softwarekonfiguration der Projektplatine.
Weitere Versionen und Voraussetzungen stehen in [BUILD.md](Bike_ekg_nrf/BUILD.md).

```bash
# Integrität der Paketdateien prüfen:
sha256sum -c SHA256SUMS

# Elf synthetische Hosttestprogramme (Python 3 und g++ erforderlich):
python3 Bike_ekg_nrf/tests/run_host_tests.py --sanitize

# Firmware mit eingerichteter Arduino-CLI-/Boardumgebung kompilieren:
python3 Bike_ekg_nrf/tools/build_firmware.py \
  --build-path /tmp/bike-ekg-build \
  --output /tmp/bike-ekg-artifacts
```

Die beigefügten Images enthalten **nur die Anwendung**, keinen Bootloader und
kein SoftDevice. Sie sind kein Erstprogrammierungsimage für einen leeren Chip.
Der Build belegt 189.308 Byte Programmspeicher und 52.788 Byte statischen RAM.

## Sicherheit und Nutzung

BLE-Zugriffe sind offen konfiguriert; RLD ist standardmäßig eingeschaltet.
Isolation und elektrische Sicherheit des vollständigen Aufbaus müssen vor
einem Anschluss an Personen fachlich geprüft werden. ADC-Counts sind keine
kalibrierten mV. Hosttests ersetzen keine Hardware- oder Sicherheitsprüfung.

Der eigene Projektcode ist zur Einsicht und fachlichen Prüfung verfügbar,
**nicht unter einer allgemeinen Open-Source-Lizenz**. Drittkomponenten behalten
ihre eigenen Lizenzen. Details: [LICENSE.md](LICENSE.md).

## Version und Zitierung

Softwarestand: **0.12.5**, 24.10.2026. Für die Referenz auf diesen Stand den
[Release `v0.12.5-ba`](https://github.com/Tim-Porsch/Bike-EKG-nrf52840firmware/releases/tag/v0.12.5-ba)
verwenden. Zitiermetadaten stehen in [CITATION.cff](CITATION.cff).
