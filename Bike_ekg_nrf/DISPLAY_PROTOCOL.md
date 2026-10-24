# Display BLE Protocol

The `display start` command enters the exclusive display state. It requires
`rate 250`, stops NUS advertising, disconnects the current NUS central, and
advertises as `Bike_EKG_Display`.

The display is non-medical. It receives the lossless signed int32 output of the
nRF configured bandpass, not a calibrated ECG measurement.

RLD follows the `rld on|off` setting, persisted starting with firmware 0.12.4.
Factory/reset defaults enable RLD; migrating V1/V2 settings preserves RLD off. It is for
electrically isolated bench operation only; the display state does not provide
a patient-safety assessment.

## GATT

Service UUID:

```text
7f510001-1212-efde-1523-785feabcd123
```

Data characteristic UUID, Notify only:

```text
7f510002-1212-efde-1523-785feabcd123
```

Configuration characteristic UUID, Read and Notify:

```text
7f510003-1212-efde-1523-785feabcd123
```

The configuration frame has exactly 20 bytes. Data frames are variable-length
Notifications up to 243 bytes. Fields wider than one byte are little-endian.

## Configuration Frame

| Bytes | Value |
| --- | --- |
| 0 | `0xC0` configuration type |
| 1 | Protocol version, `2` |
| 2 | Session ID |
| 3 | State flags |
| 4-5 | Sample rate, `250` SPS |
| 6 | Maximum samples per data frame, `58` with MTU 247 |
| 7 | Format/filter ID, `2`: signed int32 with configured bandpass |
| 8-9 | Low cutoff in mHz |
| 10-11 | High cutoff in mHz |
| 12-13 | Maximum data-frame bytes, `243` |
| 14 | Bytes per signed sample, `4` |
| 15 | Data-frame header bytes, `11` |
| 16-17 | Reserved, `0` |
| 18-19 | Filter warmup, `500` samples |

State flags: bit 0 active, bit 1 valid, bit 2 filter warming, bit 6 non-medical
display. The ESP should clear its trace while the frame is invalid.

## Data Frame

| Bytes | Value |
| --- | --- |
| 0 | `0xD0` data type |
| 1 | Protocol version, `2` |
| 2 | Session ID |
| 3 | Data flags |
| 4-5 | Frame sequence number |
| 6-9 | ADS sample number of the first value |
| 10 | Number of samples, `1-58` |
| 11 onward | Signed int32 bandpass samples |

Data flags: bit 0 valid, bit 1 acquisition gap, bit 2 locally dropped BLE
frame, bit 4 bandpass applied. A sequence or sample-number
