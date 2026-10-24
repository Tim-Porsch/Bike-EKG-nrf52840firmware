# Relink-Unterlagen für den statischen Firmware-Build

Der Firmware-Build verwendet u. a. LGPL-Komponenten aus Arduino-Core und SPI.
Dieses Verzeichnis erlaubt, die Anwendung mit ersetzten Bibliotheksobjekten
erneut zu linken, ohne die eigenen Anwendungsobjekte ändern zu müssen.
Die hierfür erforderlichen Rechte sind in `Bike_ekg_nrf/RIGHTS.md` ausgenommen.

- `objects/`: Anwendungs-, Bibliotheks- und Core-Objekte des Release-Builds;
  Debugabschnitte entfernt, Programminhalt unverändert.
- `support/`: originale Linkerskripte, CMSIS-DSP- und CryptoCell-Archive.
- `link-command.json`: tatsächliche Linkargumente mit portablen Platzhaltern.
- `relink.py`: Linken und HEX-Erzeugung mit der dokumentierten ARM-Toolchain.
- `../dependencies/sources.zip`: zugehörige Bibliotheksquellen samt Lizenzhinweisen.

Beispiel aus dem Paket-Hauptverzeichnis, nachdem ARM-GCC `9-2019q4` verfügbar ist:

```bash
python3 relink/relink.py \
  --toolchain "$HOME/.arduino15/packages/adafruit/tools/arm-none-eabi-gcc/9-2019q4/bin" \
  --output /tmp/bike-ekg-relinked
```

Ohne `--toolchain` werden `arm-none-eabi-gcc` und `arm-none-eabi-objcopy` im PATH
verwendet. Für eine angepasste Bibliothek deren Objektdateien bzw. das Core-Archiv
in einer lokalen Kopie ersetzen. Die Quellen müssen mit den in `BUILD.md` und
`firmware/build.log` dokumentierten Flags kompiliert werden. Keine Änderungen
der geschlossenen ARM-CryptoCell-Bibliothek aus dieser Anleitung ableiten.

Beim Paketieren wurde der Relink mit den unveränderten Objekten ausgeführt und
die erzeugte HEX bytegenau mit `firmware/Bike_ekg_nrf.ino.hex` verglichen.
Geänderte Bibliotheken verändern erwartungsgemäß das Image und erfordern neue
Funktionstests; der Relink-Runner flasht kein Gerät und erzeugt kein DFU-ZIP.
