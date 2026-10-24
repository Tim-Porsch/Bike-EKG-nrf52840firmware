# Drittkomponenten des Firmware-Builds

Diese Übersicht ersetzt nicht die Originalbedingungen. Die Rechteerklärung
für den eigenen Projektcode gilt nicht für die folgenden Komponenten.
Das Repository enthält unter `dependencies/sources.zip` die verwendeten
Bibliotheks-/Boardquellen samt dateispezifischen Hinweisen und unter `licenses/`
zusätzliche Lizenztexte. Die Quellen werden nicht unter eine neue Lizenz gestellt.

| Komponente | Lizenzhinweise laut mitgelieferten Originaldateien |
| --- | --- |
| Adafruit nRF52 Arduino-Core, SPI | LGPL 2.1 oder neuer; einzelne Core-/SDK-Dateien unter eigenen Bedingungen |
| Bluefruit52Lib 0.21.0 | MIT, Copyright Adafruit Industries |
| Adafruit TinyUSB 3.6.0 | MIT; eingebundene Unterkomponenten behalten ihre Originalhinweise |
| Adafruit nRFCrypto 0.1.2 | Wrapper MIT; CryptoCell-Bibliothek/Header unter ARM Object Code and Header Files License 1.0 |
| Adafruit LittleFS/InternalFileSystem 0.11.0 | Wrapper MIT; littlefs unter BSD-3-Clause |
| SdFat 2.3.0 | MIT, Copyright Bill Greiman |
| FreeRTOS im Boardpaket | MIT, Copyright Amazon.com, Inc. or its affiliates |
| Nordic nrfx / weitere SDK-Dateien | Dateispezifische Nordic-Bedingungen; u. a. BSD-3-Clause und Beschränkung auf Nordic-ICs |
| CMSIS 5.7.0 | Apache 2.0 und komponentenspezifische Hinweise im CMSIS-Paket |
| ARM-GCC-Runtime / newlib | GCC Runtime Library Exception bzw. newlib/libgloss-Bedingungen; gesammelt in `licenses/arm-toolchain-notices.txt` |

**Zielplattform beachten:** Nordic-Bestandteile dürfen teilweise nur mit
Nordic-ICs verwendet werden. Die proprietären ARM-CryptoCell-Bestandteile
unterliegen gesonderten Einschränkungen. Dies ist kein universell frei
weiterlizenzierbares Gesamtimage. SoftDevice und Bootloader sind nicht als
Flashimages im Firmware-Build enthalten.

## LGPL und statisches Linken

Der Build bindet LGPL-Komponenten statisch ein. Deshalb enthält das Repository
die entsprechenden Quellen, Lizenztexte, Objektdateien und einen geprüften
Relink-Aufruf. Damit kann die Anwendung mit angepassten Bibliotheken neu
verbunden werden; die dafür erforderlichen Rechte werden durch `RIGHTS.md`
nicht eingeschränkt.

Die im Quellarchiv enthaltenen Copyright- und Lizenzhinweise müssen erhalten
bleiben. Bei einer Weitergabe von `firmware/` nicht nur die HEX-Datei isoliert
veröffentlichen, sondern die zugehörigen Dritt-/LGPL-Unterlagen zugänglich halten.
Diese Dokumentation erteilt keine zusätzlichen Rechte an Drittkomponenten.
