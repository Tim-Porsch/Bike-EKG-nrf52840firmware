# Bike-EKG – nRF52840-Firmware

Arduino/C++-Firmware **0.12.5** für die projektspezifische nRF52840-/ADS1291-Platine
der Bachelorarbeit. Sie erfasst einen EKG-Kanal, speichert Rohdaten auf SD und
stellt USB- und BLE-Schnittstellen zur Verfügung.

**Forschungsprototyp für kontrollierte, elektrisch geeignete Prüfaufbauten.
Kein zugelassenes Medizinprodukt, kein Diagnosewerkzeug.** Insbesondere ist
RLD standardmäßig eingeschaltet. Keine Personen anschließen, bevor Isolation
und elektrische Sicherheit des gesamten Aufbaus fachlich geprüft sind.

## Inhalt und Datenfluss

`ADS1291 → DRDY-Erfassung → SampleRingBuffer → LogWriter → SD-BIN`

| Dateien | Aufgabe |
| --- | --- |
| `Bike_ekg_nrf.ino` | Zustandsautomat, Befehle, Mess- und Übertragungsmodi |
| `Ads1291.*`, `AdsService.*` | ADC-Treiber, interruptgestützte Erfassung und Verluststatistik |
| `SampleRingBuffer.h`, `TimeService.*` | Samplepuffer und geräteseitige Zeitbasis |
| `SdService.*`, `LogWriter.*` | SD-Versorgung, Dateizugriff und Rohdatenaufzeichnung |
| `SettingsService.*` | Messparameter und persistente Einstellungen im internen Flash |
| `ConsoleService.*` | USB-Konsole und BLE Nordic UART Service (NUS) |
| `PreviewBleService.*`, `PreviewBuffer.h` | BLE-Livevorschau |
| `DisplayBleService.*`, `SignalFilterService.*` | Separater Displaymodus und Q15-Bandpass |
| `SdFileBleService.*`, `SdFileProtocol.h` | BLE-Dateitransport und Metadaten-Upload |
| `RecordingMetadata.h`, `RecordCompression.h`, `Sha256.h` | Metadatenprüfung, Transportkompression und Dateihash |
| `tests/` | Synthetische C++-Hosttests mit Hardware-/Dateisystem-Stubs |

Die Modi Aufzeichnung, USB-Streaming, BLE-Vorschau, Dateizugriff und Display
werden zentral koordiniert; sie sind nicht beliebig parallel nutzbar.
Eine externe BLE-Anwendung ist nicht Teil dieses Firmwareordners. Für die
USB-Konsole genügt ein serielles Terminal; für BLE wird ein passender Client
gemäß den mitgelieferten Protokollen benötigt.

## Hardware und Abhängigkeiten

Benötigt werden die Projektplatine mit nRF52840, ADS1291 und SD-Anbindung sowie
deren Schaltplan. Die Quellen sind **nicht** für eine beliebige nRF52840-Platine
mit unveränderter Pinbelegung geeignet.

Verwendete Softwareabhängigkeiten:

- Arduino-/Adafruit-nRF52-Boardumgebung einschließlich Bluefruit/SoftDevice,
  Adafruit TinyUSB, Adafruit LittleFS/InternalFileSystem, SPI und Nordic-APIs;
- SdFat mit den projektspezifischen Chip-Select-Hooks `sdCsInit`/`sdCsWrite`.

Mehrere Komponenten sind Teil des Boardpakets, nicht unabhängig davon zu
installierende Bibliotheken. Lizenzbedingungen der verwendeten Versionen beachten.

### Pinbelegung laut Quelltext

| Signal | nRF-GPIO |
| --- | --- |
| ADS SCLK / DIN / DOUT | P0.06 / P0.08 / P0.11 |
| ADS CS / RESET / START / DRDY | P1.06 / P1.13 / P1.15 / P1.07 |
| SD MISO / SCK / MOSI | P0.24 / P0.20 / P0.22 |
| SD CS / Kartenerkennung | P1.11 (active low) / P1.02 (active high) |
| SD Versorgungsschalter | P0.10 (active high) |

`SdService.cpp` verwendet zusätzlich Arduino-Pinnummern 1, 28 und 30. Deren
Zuordnung muss zur Boardvariante passen. P1.11 wird direkt über Register
angesteuert, da er in der verwendeten Feather-Variante kein Arduino-Pin ist.

### Verifizierter Firmware-Build

Am 24.10.2026 wurde ein vollständiger Clean-Build erfolgreich durchgeführt:

- Boardauswahl: **Adafruit Feather nRF52840 Express**;
- FQBN: `adafruit:nrf52:feather52840:softdevice=s140v6,debug=l0,debug_output=serial`;
- Boardpaket: **Adafruit nRF52 1.7.0**, SoftDevice-API **S140 6.1.1**;
- Arduino-CLI **1.2.0**, ARM-GCC **9.2.1** (Paket `9-2019q4`);
- SdFat **2.3.0**, Chip-Select-Modus **1** für die Hooks aus `SdService.cpp`.

Alle Bibliotheksversionen, Buildbefehle und Prüfgrenzen stehen in
[`BUILD.md`](BUILD.md). Das Repository enthält HEX und ein
Application-DFU-ZIP sowie Bibliotheksquellen und Relink-Unterlagen.
Boardauswahl ist eine Softwarekonfiguration, keine Aussage, dass die
Projektplatine eine unveränderte Adafruit-Platine ist.

Mit Version 0.12.5 wurden SD-Aufzeichnung, USB-Konsole/Streaming,
BLE-Steuerung/Vorschau und BLE-SD-Download auf der Projektplatine erprobt.
Die beigefügten Release-Images wurden nicht erneut auf Hardware getestet.
Für den Displaymodus liegt kein dokumentierter Hardwaretest vor.

## Bedienung über USB

Serielles Terminal mit 115200 Baud öffnen, Befehle zeilenweise senden.
Zum Einstieg `help`, `state`, `status` und `config` verwenden.
Der BLE-Gerätename im normalen Konsolenmodus lautet `Bike-EKG`.

Beispiel für einen kurzen Prüfstandstest mit internem ADC-Testsignal und SD:

```text
input test
rld off
rate 250
gain 6
test_duration 10
config
start
```

Eine vorhandene SD-Karte ist erforderlich; `stop` beendet die Aufzeichnung
vorzeitig. Testsignal und RLD-Einstellung ersetzen keine elektrische Isolation.
Nach dem Test die gewünschten Messparameter explizit wieder einstellen.

Weitere Befehle:

| Befehl | Bedeutung |
| --- | --- |
| `rate <125\|250\|500>` | Abtastrate in SPS |
| `gain <1\|2\|3\|4\|6\|8\|12>` | ADC-Verstärkung |
| `input <normal\|short\|test>` | Eingang wählen |
| `rld <on\|off>` | RLD konfigurieren |
| `test_duration <seconds>` | Messdauer, maximal 4200 Sekunden |
| `time <unix_seconds>` | Gerätezeit setzen |
| `filter <on\|off>` | Filter für den Streamingpfad wählen |
| `filter band <low_hz> <high_hz>` | Filtergrenzen konfigurieren |
| `config reset` | Persistente Einstellungen auf Standardwerte zurücksetzen |
| `stream <start\|stop>` | USB-Textstream starten/beenden |
| `display start` | In den separaten BLE-Displaymodus wechseln |

Standardwerte: 500 SPS, Gain 6, Eingang `normal`, RLD **an**, Messdauer
300 Sekunden, Filterband 0,5–40 Hz. Gespeicherte Einstellungen können davon
abweichen. Konfigurationsänderungen werden bei der nächsten Messung angewendet.

## Dateiformat und BLE-Protokolle

- SD-Dateien heißen `EKG00000.BIN` usw.; vorhandene Namen werden nicht überschrieben.
- BEKG v1: 512-Byte-Header, 12-Byte-Datensätze (`sampleNumber:u32`,
  `timestampUs:u32`, `adcValue:i32`), 512-Byte-Abschlussblock; Little Endian.
- SD speichert **Rohwerte**, keine durch den MCU-Bandpass gefilterten Werte.
  ADC-Counts sind keine kalibrierten mV.
- Der 32-Bit-Mikrosekundenzähler läuft nach etwa 71,6 Minuten über. Empfänger
  müssen Zeitüberläufe und Samplelücken berücksichtigen.
- CRC32 und SHA-256 prüfen Datenintegrität, nicht Geräteauthentizität oder
  die physiologische Richtigkeit einer Messung.

Schnittstellendetails:
[BLE-Vorschau](PREVIEW_PROTOCOL.md), [SD-Dateitransport](SD_FILE_PROTOCOL.md),
[Displaymodus](DISPLAY_PROTOCOL.md). NUS-Text, Vorschau, Dateitransport und
Displaydaten sind unterschiedliche Protokolle.

## Hosttests ohne Hardware

Voraussetzungen: Python 3 und ein C++17-fähiger `g++`. Aus diesem Ordner:

```bash
python3 tests/run_host_tests.py
# Optional unter Linux mit Address-/UndefinedBehavior-Sanitizern:
python3 tests/run_host_tests.py --sanitize
```

Der Runner kompiliert alle elf Testprogramme in einem temporären Ordner und
führt sie aus. `CXX` kann einen alternativen Compilerpfad angeben; `TMPDIR`
steuert das temporäre Verzeichnis. Es werden keine Geräte angesprochen.
Stubs bilden nur die benötigten APIs nach, nicht Hardwaretiming, Funk oder
elektrische Eigenschaften. Zielhardware zusätzlich prüfen: SD fehlt/voll,
Spannungsausfall, BLE-Abbruch, Wiederverbindung und Verluststatistik.

## Weitergabe, Sicherheit und Rechte

- BLE-Zugriffe sind offen konfiguriert, ohne erzwungene Authentifizierung oder
  Verschlüsselung. Messdaten und Steuerbefehle sind dadurch nicht angemessen
  gegen unberechtigten Zugriff geschützt. Nur kontrollierte Testumgebungen nutzen.
- Keine realen Messdaten, personenbezogenen Metadaten oder lokalen Zugangsdaten
  zusammen mit dem Quellpaket weitergeben.
- Der eigene Code wird zur Einsicht und fachlichen Prüfung bereitgestellt,
  **nicht unter einer allgemeinen Open-Source-Lizenz**. Siehe
  [`RIGHTS.md`](RIGHTS.md). Lizenzen der Fremdkomponenten bleiben unberührt;
  Übersicht in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
