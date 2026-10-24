# Interne BLE-Signalvorschau, Version 1

Separater Dienst neben NUS und dem unveränderten Displaydienst. NUS bleibt
verbunden; Einzelmesswerte werden nicht als Text ausgegeben. Alle Mehrbytefelder
sind Little Endian.

| Element | UUID |
| --- | --- |
| Service | `7f520001-1212-efde-1523-785feabcd123` |
| Daten, Notify | `7f520002-1212-efde-1523-785feabcd123` |
| Status, Read/Notify | `7f520003-1212-efde-1523-785feabcd123` |

## Ablauf

Status und Daten abonnieren, Status lesen, dann intern über NUS `preview start`
senden. Nur Idle mit vorhandenem Datenabonnenten ist erlaubt. Es wird mit den
aktuellen ADS-Einstellungen erfasst. Ab Firmware 0.8.1 aktiviert `filter on`
denselben MCU-Bandpass wie für USB-Streaming, mit dem gespeicherten Filterband.
Die Filterung erfolgt vor dem Einreihen der BLE-Werte; bei Quelllücken wird der
Filterzustand zurückgesetzt. `filter off` überträgt unveränderte Rohwerte.
Filtereinstellungen sind nur in Idle änderbar und gelten ab dem nächsten Start.
SD-Aufzeichnungen bleiben ungefiltert. Antwort:
`preview: started`. Eine neue Session setzt alle Vorschauzähler zurück.

`preview stop` beendet die Erfassung, verwirft ausstehende Sendewerte und liefert
Endstatus sowie `preview: stopped` und `state: idle`. Anschließend Status lesen,
um den Endstand auch bei ausgebliebener Statusnotification zu erhalten.
SD-`start` stoppt eine aktive Vorschau ebenfalls vor dem Aufzeichnungsstart.
Verbindungsabbruch oder Verlust des Datenabonnements beendet die Vorschau.
Streaming, Display und Vorschau sind gegenseitig ausschließende Betriebsmodi.
Die internen Vorschaukommandos erscheinen nicht in `help`.

## Datenframe

| Offset | Typ | Inhalt |
| --- | --- | --- |
| 0 | u8 | `0xD1` |
| 1 | u8 | Version `1` |
| 2 | u16 | Session |
| 4 | u16 | Frame-Sequenz, beginnend bei 0 |
| 6 | u32 | Erste Sample-Nummer |
| 10 | u8 | Anzahl N, 1–58 |
| 11 | u8 | Reserviert, 0 |
| 12 | N × i32 | Vorzeichenbehaftete ADC-Werte, Filtermodus laut Status |

Framelänge: `12 + 4*N`, maximal 244 Bytes und höchstens ATT-MTU minus 3.
Auch ATT-MTU 23 wird unterstützt (zwei Werte pro Frame). Nur aufeinanderfolgende
Sample-Nummern teilen einen Frame; Quelllücken beginnen einen neuen Frame.
Teilframes werden spätestens beim nächsten Update nach 40 ms abgeschlossen.
Die Queue hält acht Frames und verwirft bei Überlauf den ältesten. Sequenznummern
werden beim Erzeugen vergeben, sodass verworfene Frames als Sequenzlücke erkennbar
bleiben. Sample-, Sequenz- und Sessionnummern laufen modulo ihrer Feldbreite über.

## Statusframe (20 Bytes)

| Offset | Typ | Inhalt |
| --- | --- | --- |
| 0 | u8 | `0xC1` |
| 1 | u8 | Version `1` |
| 2 | u16 | Session |
| 4 | u16 | Abtastrate in SPS, vor erster Session 0 |
| 6 | u8 | Bit 0: aktiv |
| 7 | u8 | Format `1`: rohe i32-ADC-Werte; `2`: MCU-bandpassgefilterte i32-ADC-Werte (ab 0.8.1) |
| 8 | u32 | Exklusives Ende des Quell-Samplenummernraums |
| 12 | u32 | Aggregierte Erfassungsverluste aus ADS/Ringpuffer |
| 16 | u32 | In der Vorschauqueue verworfene Samples |

Status wird beim Start, beim Stopp und während der Vorschau etwa einmal pro
Sekunde aktualisiert. Er bleibt lesbar. Das exklusive Quellende ergibt sich aus
empfangenen Samples abzüglich Einschwingwerten plus Lesefehlern und inferierten
verpassten DRDY-Ereignissen. Erfassungsverluste überlappen mit Samplelücken;
Read-Error- und DRDY-Zähler dürfen nicht nochmals dazuaddiert werden.

Der Browser zählt Samplelücken einschließlich eines fehlenden Anfangs und zeichnet
keine Verbindung über sie. Während der Übertragung sind Werte noch unterwegs;
erst bei beendetem Empfang liefert Quellende minus empfangene Werte die gesamte
Fehlmenge einschließlich eines fehlenden Endes. Späte Notifications können diesen
Endstand noch korrigieren. Paketlücken sind ein zusätzlicher Diagnosezähler, keine
weitere Sampleverlustzahl.

Die Queue ist begrenzt, Bluefruits `notify()` kann jedoch intern auf TX-Credits
warten. Es besteht keine harte Echtzeit- oder Verlustfreiheitsgarantie.
Durchsatz und Verlustverhalten müssen mit echter Hardware geprüft werden.

Hosttest der Queue (aus `Software`):

```bash
g++ -std=c++11 -Wall -Wextra -Werror Bike_ekg_nrf/tests/preview_buffer_test.cpp -o /tmp/preview-buffer-test
/tmp/preview-buffer-test
```
