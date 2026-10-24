# SD-Dateien über BLE, Version 1 (Firmware ab 0.9.0)

Lesender Zugriff auf `EKG00000.BIN` bis `EKG99999.BIN` im SD-Wurzelverzeichnis.
Keine Lösch-, Schreib- oder beliebigen Pfadoperationen. NUS und das bestehende
Display-/Vorschauprotokoll bleiben unverändert. Keine neuen `help`-Befehle.
Firmware 0.10.0 ergänzt die Übertragung von Angaben für **neue** Aufzeichnungen
und eine Dateiliste mit gespeicherten Texten. Bestehende SD-Dateien bleiben read-only.

| Element | UUID | Eigenschaften |
| --- | --- | --- |
| Service | `7f530001-1212-efde-1523-785feabcd123` | |
| Control | `7f530002-1212-efde-1523-785feabcd123` | Write with response, maximal 20 Bytes |
| Response | `7f530003-1212-efde-1523-785feabcd123` | Read, variable Länge, maximal 512 Bytes |
| Data (ab 0.11.0) | `7f530004-1212-efde-1523-785feabcd123` | Notify, maximal 244 Bytes, passend zu ATT-MTU |

## Ablauf und Zuverlässigkeit

Der Kontrollkanal ist bewusst Pull-basiert: einen Auftrag schreiben, danach die
Antwort lesen. Eine bestätigte BLE-Schreiboperation quittiert zunächst nur den
Empfang im BLE-Stack; SD-Arbeit erfolgt in der Hauptschleife. Bis zum Abschluss
kann der Antwortwert noch die vorherige Request-ID enthalten. Der Browser liest
weiter, bis ID und Operation passen. Die fertige Antwort bleibt bis zum nächsten
Auftrag unverändert lesbar. Bei kleiner ATT-MTU nutzt der Stack Long Reads;
MTU 23 ist zulässig. Hardware-/Browserkompatibilität und Durchsatz sind zu testen.
Vor Bestätigung einer neuen Anfrage wird der Lesewert auf Operation 0
(noch ausstehend) gesetzt. So kann auch eine neu geladene Seite mit wieder bei
1 beginnenden IDs keine alte Antwort der vorherigen Verbindung akzeptieren.

Jeder Client vergibt fortlaufende u16-Request-IDs (mit Überlauf). Wiederholungen
eines Auftrags müssen **dieselbe ID und identische Bytes** verwenden. Der MCU
erkennt die letzte Anfrage und führt sie nicht erneut aus: insbesondere wird kein
Dateieintrag übersprungen. Der Browser wiederholt nach 3 Sekunden ohne passende
Antwort, maximal drei Schreibversuche. Einzelne hängende GATT-Aufrufe sind auf
5 Sekunden begrenzt; dann muss die Verbindung neu aufgebaut werden.

Jeder Payload enthält eine CRC32-Prüfsumme (IEEE, Polynomial `0xEDB88320`, Init und
finales XOR `0xFFFFFFFF`). Bei CRC-Fehlern liest der Browser denselben Antwortwert
bis zu dreimal. Danach wird abgebrochen, keine Teil-CSV gespeichert.
Zusätzlich werden Dateioffsets, Gesamtlänge, Header und Footer geprüft.
Es gibt keine vollständige Dateiprüfsumme im bestehenden BIN-Dateiformat.

Aufträge sind nur in Idle erlaubt. Während ein Verzeichnis oder eine Datei offen
ist, gilt der exklusive Zustand `files`; Messung und Vorschau können nicht starten.
Nach Listenende, Close, SD-Fehler, Disconnect oder 15 Sekunden ohne Auftrag werden
Handles geschlossen und die SD-Stromversorgung wieder abgeschaltet. Ein Download
liest höchstens 496 Bytes auf einmal; die Datei wird nicht im MCU-RAM gehalten.
Ein abgebrochener Download wird nicht automatisch fortgesetzt. Ab 0.11.0 nutzt
der Browser für Dateidaten Notifications statt des alten Read-Pull-Ablaufs.

Ab Firmware 0.12.1 wird die SD-Versorgung vor der Kartenerkennung eingeschaltet
und die vorhandene Einschaltwartezeit von 500 ms abgewartet. Beim Adafruit-4682-
Modul liegt der 4,7-kΩ-Pull-up von DET an der geschalteten SD-Versorgung; eine
Abfrage bei ausgeschalteter Versorgung kann eine eingelegte Karte nicht zuverlässig
erkennen. Bei fehlender Karte oder Initialisierungsfehler wird wieder abgeschaltet.
Die Korrektur gilt zentral auch für den Aufzeichnungsstart.

Host-Regressionstest der echten `SdService.cpp` mit versorgungsabhängigem DET:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I Bike_ekg_nrf/tests/sd_power_stubs -I Bike_ekg_nrf/tests/sd_stubs Bike_ekg_nrf/tests/sd_power_test.cpp Bike_ekg_nrf/SdService.cpp -o /tmp/sd-power-test
/tmp/sd-power-test
```

## Anfrage (Little Endian)

| Offset | Typ | Inhalt |
| --- | --- | --- |
| 0 | u8 | `0xF1` |
| 1 | u8 | Version `1` |
| 2 | u16 | Request-ID |
| 4 | u8 | Operation |
| 5 | u8 | Reserviert `0` |
| 6 | variabel | Payload |

| Operation | Wert | Payload | Ergebnis |
| --- | --- | --- | --- |
| List | 1 | leer | Verzeichnis neu öffnen, erster Eintrag oder Done |
| Next | 2 | leer | Nächster passender Eintrag oder Done |
| Open | 3 | 12 ASCII-Bytes Dateiname, ohne Nullbyte | Datei öffnen, Größe |
| Read | 4 | u32 absoluter Dateioffset | Bis zu 496 Bytes ab dieser Position |
| Close | 5 | leer | Sitzung schließen, auch ohne offene Datei erlaubt |
| MetadataBegin | 6 | u16 erwartete META-Länge (12–468 Bytes) | Upload starten, einmaliger u32-Token im Wertfeld |
| MetadataChunk | 7 | u16 Offset + 1–12 Bytes | Fortlaufend in RAM übernehmen, bestätigte Bytezahl im Wertfeld |
| MetadataSeal | 8 | u32 CRC32 über alle META-Bytes | Vollständigkeit/UTF-8 prüfen, Token bestätigen |
| ListInfo | 9 | leer | Erster Dateieintrag inklusive Metadaten |
| NextInfo | 10 | leer | Nächster Dateieintrag inklusive Metadaten |
| StreamStart | 11 | leer, nach Open und Notification-Subscribe | Sitzung im Wertfeld; Payload: u32 Größe, u16 Fenster, u16 ATT-MTU |
| StreamAck | 12 | u16 Sitzung + u32 Offset | Zusammenhängend empfangene Bytes bestätigen; Offset im Wertfeld |
| StreamSeek | 13 | u16 Sitzung + u32 Offset | Ab dieser Empfangsposition wiederholen; Offset im Wertfeld |
| StreamFinish | 14 | u16 Sitzung | Nach vollständigem ACK: Größe im Wertfeld, 32 Bytes SHA-256 im Payload |
| StreamStartCompressed (ab 0.12.0) | 15 | leer, wie StreamStart | Gleiche Antwort wie StreamStart; Roh- und Delta-Rahmen erlaubt |
| ListInfoNewest (ab 0.12.2) | 16 | leer | Dateiliste mit Metadaten, höchste Dateinummer zuerst; Fortsetzung mit NextInfo |

### Dynamische Dateiliste, neueste Nummer zuerst (ab 0.12.2)

`ListInfoNewest` erfasst zunächst ausschließlich passende Dateinamen im
SD-Wurzelverzeichnis. Pro `update()` werden höchstens 16 Verzeichniseinträge
geprüft; Header werden dabei noch nicht gelesen. Ein festes 12.500-Byte-Bitset
deckt `EKG00000.BIN` bis `EKG99999.BIN` vollständig ab, unabhängig von Anzahl und
Verzeichnisreihenfolge. Danach werden die vorhandenen Nummern absteigend
abgearbeitet. Erst unmittelbar vor der jeweiligen Antwort wird der gespeicherte
Header der betreffenden Datei gelesen. Name, Größe und Metadatenformat entsprechen
`ListInfo`; `NextInfo` liefert den nächsten älteren Eintrag.

Retries mit identischer ID setzen eine noch laufende Namenssuche fort bzw.
liefern die bereits fertige Antwort unverändert erneut. Close, Disconnect,
Fehler und Timeout räumen wie bisher die SD-Sitzung auf. Beim nächsten Listenstart
wird der Namensindex vollständig zurückgesetzt. Die alten Operationen `List`,
`Next` und `ListInfo` behalten ihr bisheriges Verhalten.

Der Webclient zeigt jede empfangene Zeile sofort an und baut bestehende Zeilen
nicht neu auf. Bei explizitem „Invalid SD operation“ für Operation 16 verwendet er
`ListInfo` und sortiert die eintreffenden Zeilen weiterhin absteigend ein.
Bei alter Firmware kann der erste empfangene Eintrag daher noch eine ältere
Datei sein. Abbruch erhält bereits geladene Zeilen als gekennzeichnete Teilliste;
ein Download wird erst nach Freigabe der laufenden Listensitzung erlaubt.

## Antwort (Little Endian)

| Offset | Typ | Inhalt |
| --- | --- | --- |
| 0 | u8 | `0xF2` |
| 1 | u8 | Version `1` |
| 2 | u16 | Request-ID |
| 4 | u8 | Operation |
| 5 | u8 | Status: 0 = Ok, 1 = Done, 2 = Error |
| 6 | u16 | Payloadlänge N |
| 8 | u32 | List/Next/Open: Dateigröße; Read: Dateioffset; sonst 0 |
| 12 | u32 | CRC32 des Payloads (0 bei leerem Payload) |
| 16 | N Bytes | List/Next: Dateiname; Read: Dateibytes; Error: UTF-8-Fehlertext |

Bei Read genau am Dateiende wird Done mit leerem Payload zurückgegeben. Der
Browser beendet den normalen Empfang bereits bei der von Open gelieferten Größe
und bestätigt den Sitzungsabschluss mit Close.

## Schneller, vollständig geprüfter Download ab 0.11.0

Browser: Data abonnieren, Open, StreamStart; der aktuelle Client öffnet die Datei
bereits vor dem Abonnieren. Notifications können vor der StreamStart-Antwort
eintreffen und werden begrenzt gepuffert. Jeder Datenrahmen (Little Endian):

| Offset | Typ | Inhalt |
| --- | --- | --- |
| 0 | u8 | `0xF3` |
| 1 | u8 | Rahmenversion 1 |
| 2 | u16 | Streamsitzung (ungleich 0) |
| 4 | u32 | Absoluter Dateioffset |
| 8 | u32 | CRC32 über Bytes 0–7, gefolgt von Nutzbytes ab Offset 12 |
| 12 | variabel | 1–232 Dateibytes, insgesamt höchstens min(244, ATT-MTU − 3) Bytes |

Der MCU sendet bis zu 4096 unbestätigte Dateibytes ohne Einzellanfrage. Der
Browser akzeptiert nur korrekte CRC, Sitzung und exakt den nächsten erwarteten
Offset. Er bestätigt spätestens nach einem halben Fenster (2048 Bytes); eine ACK
gibt weitere Sendekapazität frei. Alle ACK/Seek-Aufträge benutzen weiterhin den
bestätigten, idempotenten Kontrollkanal. Seek darf weder unter den letzten ACK
noch über das bisher gesendete Ende springen: `acked <= offset <= sent`.
Der Browser fordert nach Stillstand ab dem ersten fehlenden Byte erneut an;
nach mehr als fünf Wiederholungen ohne Fortschritt wird abgebrochen. Alte,
doppelte, fremde oder nach einer Lücke eintreffende Pakete werden nicht angehängt.

Der MCU berechnet inkrementell SHA-256 über die direkt aus der exklusiv geöffneten
SD-Datei gelesenen Originalbytes in Dateireihenfolge. Bei Seek werden bereits
gehashte Bytes übersprungen, auch wenn sich die Paketgrenzen verändern. Ein
vorbereiteter Datenrahmen wird bei vollem BLE-Sendepuffer unverändert erneut
versucht. So werden weder Hash noch Dateiposition doppelt fortgeschrieben.
Ein zweiter SD-Durchlauf und ein vollständiger MCU-Dateipuffer sind nicht nötig.

Nach vollständigem Empfang: ACK bis Dateigröße, StreamFinish, SHA-256 im Browser
via Web Crypto berechnen und alle 32 Bytes vergleichen. Nur gleiche Dateigröße
und gleicher Hash erlauben den CSV-Export; zusätzlich gelten die bisherigen
Header-/Footer-Prüfungen. Der Hash umfasst Header, Messdaten und Footer. CRC32
hilft bei der gezielten Transportreparatur, SHA-256 prüft den Gesamtinhalt.
Der Browser speichert den bestätigten BIN-Hash als CSV-Metadatum und zeigt den
erfolgreichen Abgleich sowie die ATT-MTU an. Fehlende Streaming-Unterstützung
führt zum Updatehinweis, nicht zum stillen Rückfall auf ungeprüften Empfang.

Close, SD-Fehler, Abmelden des Notification-Empfängers, Disconnect und
Inaktivität schließen den Stream. Alte Read-Operationen bleiben für bestehende
Clients erhalten, sind während Streaming aber gesperrt. SHA-256 ist ein
Inhaltsabgleich, keine Geräteauthentifizierung und keine Erkennung von bereits
vor dem Transfer fehlerhaften Messdaten auf SD.

## Verlustfreie Transportkompression ab 0.12.0

Der neue Browser startet mit Operation 15 statt 11. Eine alte Firmware 0.11.0
weist diese Operation mit `Invalid SD operation or session` ab und schließt die
Datei. Nur in diesem Fall wird die Datei erneut geöffnet (Größe erneut prüfen)
und Operation 11 verwendet, weiterhin mit abschließendem SHA-256-Abgleich.
Operation 11 bleibt auf neuer Firmware unverändert und sendet ausschließlich
Rohrahmen Version 1. Operation 15 darf rohe und komprimierte Pakete mischen.

Komprimierter Datenrahmen:

| Offset | Typ | Inhalt |
| --- | --- | --- |
| 0 | u8 | `0xF3` |
| 1 | u8 | Rahmenversion 2 (Delta-Codec v1) |
| 2 | u16 | Streamsitzung |
| 4 | u32 | Absoluter Offset der **rekonstruierten** Originalbytes |
| 8 | u32 | CRC32 über Bytes 0–7, gefolgt von allen Bytes ab Offset 12 |
| 12 | u16 | Rekonstruierte Bytelänge: 24–384, Vielfaches von 12 |
| 14 | 12 Bytes | Erster Datensatz vollständig, unverändert |
| 26 | variabel | Drei Differenzwerte pro weiterem Datensatz |

Datensätze werden als drei Little-Endian-u32-Bitmuster behandelt. Jede Differenz
ist `current - previous` modulo 2^32. ZigZag bildet sie mit
`(delta << 1) XOR (0 - (delta >> 31))` auf u32 ab. Danach unsigned LEB128
(niedrige 7 Bits zuerst, Fortsetzungsbit 0x80, maximal 5 Bytes). Der Decoder
berechnet `delta = (value >> 1) XOR (0 - (value & 1))` und addiert modulo 2^32
zum jeweiligen Feld des vorherigen Datensatzes. So bleiben sämtliche 32 Bits
erhalten, einschließlich ADC-Vorzeichen, Lücken und Zeitstempelüberläufen.

Jeder Block ist eigenständig. Es gibt keine Abhängigkeit zum vorherigen Paket.
Unvollständige, überlange, nichtkanonische oder überlaufende Varints, zusätzliche
Bytes und ungültige rekonstruierte Längen werden abgewiesen. Erst nach CRC-Prüfung
wird in einen maximal 384-Byte-Puffer dekodiert. Der Browser akzeptiert weiterhin
nur exakt den nächsten Dateioffset und eine passende Sitzung. Fenster, ACK, Seek,
Dateigröße und finaler SHA-256 beziehen sich immer auf **Originalbytes**.

Der MCU komprimiert nur vollständig ausgerichtete Messdatensätze zwischen den
512-Byte-Header-/Footerblöcken. Der Quellpuffer ist fest auf 384 Bytes begrenzt.
Die Ausgabe wird an die tatsächliche ATT-MTU angepasst. Wenn der kodierte Block
inklusive 2-Byte-Längenfeld nicht kleiner als seine Originalbytes ist, wird ein
Rohrahmen verwendet. Bei MTU 23 wird die Kompression ganz übersprungen. Ein
Seek an einen beliebigen Byteoffset sendet zunächst nötige Ausrichtungsbytes roh.
Nicht verbrauchte vorgelesene Bytes werden nicht gehasht; die Dateiposition wird
auf das nächste tatsächlich verwendete Originalbyte gesetzt. Der Ganzdatei-Hash
wird somit unverändert über die Originaldatei berechnet, auch bei Wiederholungen.

Die Fortschritts-/Geschwindigkeitsanzeige zählt rekonstruierte BIN-Bytes. CSV
enthält `transfer_compression: "record_delta_v1"`, falls mindestens ein komprimierter
Block verwendet wurde, sonst `"none"`. Bestehende SD-Dateien bleiben unverändert.

## Aufzeichnungsmetadaten ab 0.10.0

`ListInfo`/`NextInfo` liefert im Payload: Dateiname (12 ASCII-Bytes), Statusbyte
(0 = keine META-Erweiterung, 1 = gültig, 2 = Header/Metadaten nicht lesbar) und
bei Status 1 die META-Erweiterung in ihrer tatsächlichen Länge. Maximal 481
Payload-Bytes, weiterhin innerhalb der 512-Byte-GATT-Antwort. Datei-/Metadatenfehler
lassen den Eintrag in der Liste; es werden keine aktuellen Formularwerte eingesetzt.
Die bisherigen List-/Next-Antworten bleiben unverändert (12 Bytes Dateiname).

Das Grundformat bleibt BEKG v1. Header-Flagbit 2 (`flags & 4`) zeigt die Erweiterung
an. Sie beginnt an Headeroffset 44 (früher reserviert), Länge maximal 468 Bytes:

| Relativer Offset | Typ | Inhalt |
| --- | --- | --- |
| 0 | 4 Bytes | ASCII `META` |
| 4 | u16 | Erweiterungsversion 1 |
| 6 | u16 | UTF-8-Bytelänge Versuchsperson, maximal 80 |
| 8 | u16 | UTF-8-Bytelänge Kommentar, maximal 376 |
| 10 | u16 | Reserviert, 0 |
| 12 | variabel | Erst Versuchsperson, dann Kommentar, keine Nullterminatoren |

Restliche reservierte Bytes bleiben null. Beide Texte sind optional. Gültiges
UTF-8 wird geprüft; NUL/Steuerzeichen werden abgewiesen, außer LF/CR/Tab im
Kommentar. Kein Abschneiden von Zeichen oder UTF-8-Sequenzen. Alte Dateien ohne
Flag werden als ohne Texte gelesen; alte Reader können die Erweiterung ignorieren.

Der Browser lädt den Block via Begin/Chunk/Seal hoch und wartet jede Bestätigung
ab. Die Uploadphase hält den exklusiven `files`-Zustand, aktiviert aber die SD
noch nicht. Nach erfolgreichem Seal startet nur das interne NUS-Kommando
`record <token>` die Messung. Es ist nur über BLE zulässig und konsumiert den
Token genau einmal. Der LogWriter kopiert den bestätigten Block in den Header
und synchronisiert ihn vor Erfassungsbeginn auf SD. Der normale Befehl `start`
bleibt für bestehende USB-/NUS-Nutzer ohne Metadaten bestehen.

Close, Disconnect, ungültige Übertragung oder 15 Sekunden ohne Auftrag verwerfen
den RAM-Block und Token. Ein Start mit fehlendem/abgelaufenem/falschem Token wird
abgewiesen. Nach fehlender Startbestätigung darf nicht automatisch erneut gestartet
werden: die Aufnahme könnte bereits laufen, wenn nur die Antwort verloren ging.
Bereits gespeicherte Header werden nie nachträglich verändert.

## Browserexport

Maximal 32 MiB BIN pro Datei (deckt die derzeit maximalen Messungen ab). Vollständige
BIN wird im Browserpuffer zusammengesetzt, anschließend wird CSV erzeugt. Metadaten
stehen als `# schlüssel: JSON-Wert` vor der Tabelle, orientiert am bestehenden
`EKG_Parser`. Enthalten sind alle benannten Header-/Footerfelder, unter anderem
Gerätezeit (falls gültig), Firmware, Rate, Gain, Eingang, RLD und Verluststatistik.
Die aktuelle Gerätekonfiguration wird **nicht** als Dateimetadatum verwendet.

Datenspalten: `sample_number,timestamp_us,timestamp_s,adc_counts`. `timestamp_s`
berücksichtigt u32-Timestampüberläufe wie der bestehende Parser; es ist keine
automatisch aus der Gerätezeit berechnete Kalenderzeit. Rohwerte bleiben ungefiltert
und unkalibriert. Nicht gespeicherte Informationen (z. B. Filterband) werden nicht
nachträglich erfunden. Fehlender Footer oder inkonsistente Dateilänge verhindern
den CSV-Export, verändern jedoch nie die SD-Datei.

Der Dienst verwendet wie die bestehende Konsole offene BLE-Berechtigungen, keine
zusätzliche Authentifizierung. Für sensible Messdaten ist vor einem Einsatz
außerhalb des kontrollierten Prüfaufbaus ein separates Zugriffsschutzkonzept nötig.

## Hosttest des tatsächlichen Servicecodes

Aus `Software` (C++17 nur für die Host-Stubs; Firmware nutzt die Board-Toolchain):

```bash
g++ -std=c++17 -Wall -Wextra -Werror -I Bike_ekg_nrf/tests/sd_stubs Bike_ekg_nrf/tests/sd_file_service_test.cpp Bike_ekg_nrf/SdFileBleService.cpp -o /tmp/sd-file-service-test
/tmp/sd-file-service-test
```

Zusätzlich Metadatenvalidierung und den echten LogWriter (Dateisystem/Clock als
Host-Stubs) testen:

```bash
g++ -std=c++11 -Wall -Wextra -Werror Bike_ekg_nrf/tests/recording_metadata_test.cpp -o /tmp/recording-metadata-test
/tmp/recording-metadata-test
g++ -std=c++17 -Wall -Wextra -Werror -I Bike_ekg_nrf/tests/sd_stubs Bike_ekg_nrf/tests/log_writer_metadata_test.cpp Bike_ekg_nrf/LogWriter.cpp -o /tmp/log-writer-metadata-test
/tmp/log-writer-metadata-test
```

Streaming einschließlich MTU 23/247, Sendefenster, Wiederholung mit geänderten
Paketgrenzen und Ganzdatei-Hash sowie SHA-256-Referenzvektoren:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I Bike_ekg_nrf/tests/sd_stubs Bike_ekg_nrf/tests/sd_stream_test.cpp Bike_ekg_nrf/SdFileBleService.cpp -o /tmp/sd-stream-test
/tmp/sd-stream-test
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined Bike_ekg_nrf/tests/sha256_test.cpp -o /tmp/sha256-test
/tmp/sha256-test
```

`sd_stream_test.cpp` prüft auch gemischte Roh-/Delta-Streams, nicht komprimierbare
Daten, alte Startoperation, MTU 23/247, Seek mitten im Datensatz, Rekonstruktion
und den Originaldatei-Hash. Codec-Referenzvektoren zusätzlich testen:

```bash
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined Bike_ekg_nrf/tests/record_compression_test.cpp -o /tmp/record-compression-test
/tmp/record-compression-test
```
