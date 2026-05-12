# Usage

## Adding the analyzer in Logic 2

1. Open or capture a recording with the J1850 VPW signal on a digital channel.
2. **Analyzers → Add Analyzer → SAE J1850 VPW**.
3. Configure:
   - **VPW Data** — the input channel
   - **Active Level** — `Active Low` for transceiver outputs (typical),
     `Active High` for raw-bus probing
   - **Verify Checksum** — enable to flag CSUM mismatches with the standard
     error highlight

The analyzer reads a digital channel only. If you only have an analog
recording, recapture with the digital channel enabled on the same probe and
set the digital threshold around 3–4 V in the capture settings (Logic 2's
**Capture** panel → **Digital Threshold**). J1850 VPW idles at 0 V passive
and pulses to ~7 V active, so 3–4 V is a safe trip point for typical
transceiver outputs or raw-bus probes.

## Annotation bubbles

The analyzer emits one Frame per logical unit. The Logic 2 bubble text shows
multiple zoom levels (short / medium / long form).

| Frame type | Short | Medium | Long |
|---|---|---|---|
| `SOF`   | `S`   | `SOF`        | `SOF 1x` or `SOF 4x` |
| `PRIO`  | `P`   | `Prio`       | `Prio 0xNN` |
| `DEST`  | `D`   | `Dest`       | `Dest 0xNN` |
| `SRC`   | `Sr`  | `Src`        | `Src 0xNN` |
| `MODE`  | `M`   | `Mode`       | `Mode 0xNN` |
| `DATA`  | `NN`  | `Data 0xNN`  | — |
| `CSUM` (ok)  | `CS`  | `CSUM 0xNN`   | — |
| `CSUM` (bad) | `!CS` | `BAD CSUM 0xNN` | `BAD CSUM 0xNN (calc 0xMM)` |
| `EOF`   | `E`   | `EOF`        | — |
| `IFS`   | `I`   | `IFS`        | — |
| `ERROR` | `!`   | `ERR`        | `ERR <us> us` |

The active polarity setting only affects how the analyzer decodes the line; it
does not change the annotation labels.

## Frame schema (Frame V1)

Used by the bubble-text and CSV export paths. The `mType` codes:

| `mType` | Name | `mData1` | `mData2` | `mFlags` |
|---|---|---|---|---|
| 0  | SOF   | speed (1 or 4) | 0 | 0 |
| 1  | PRIO  | byte value | 0 (index)         | 0 |
| 2  | DEST  | byte value | 1 (index)         | 0 |
| 3  | SRC   | byte value | 2 (index)         | 0 |
| 4  | MODE  | byte value | 3 (index)         | 0 |
| 5  | DATA  | byte value | byte position     | 0 |
| 6  | CSUM  | observed CRC | computed CRC    | `DISPLAY_AS_ERROR_FLAG` on mismatch |
| 7  | EOF   | 0 | 0 | 0 |
| 8  | IFS   | 0 | 0 | 0 |
| 10 | ERROR | observed µs | 0 | `DISPLAY_AS_ERROR_FLAG` |

(Type 9, `NORM` / in-frame-response, is reserved for IFR support and not
currently emitted.)

## FrameV2 schema

For Logic 2's search, filter, and structured-export features. The analyzer
calls `UseFrameV2()` at construction.

### `sof`
```
{ speed: integer }       // 1 or 4
```
Span: SOF pulse start..end.

### `byte`
```
{ value: byte, index: integer }
```
Span: byte start..end. Index is the byte's position in the frame
(0 = priority, 1 = dest, 2 = src, 3 = mode, 4+ = data).

### `csum`
```
{ csum: byte, calc: byte, ok: bool }
```
Span: CSUM byte start..end.

### `message`
```
{
  speed:    integer,
  priority: byte,     // present iff frame has ≥ 1 byte
  dest:     byte,     // present iff frame has ≥ 2 bytes
  src:      byte,     // present iff frame has ≥ 3 bytes
  mode:     byte,     // present iff frame has ≥ 4 bytes
  data:     bytearray,// present iff frame has > 5 bytes (data bytes before csum)
  csum:     byte,     // present iff frame had a csum byte (frame ≥ 5 bytes)
  csum_ok:  bool
}
```
Span: from the start of the first byte to the EOF/IFS marker. One per
complete frame.

### `error`
```
{ width_us: double }
```
Emitted on unrecognized pulse widths.

## CSV export

`Analyzers → Export → CSV` (or use `mResults->GenerateExportFile` via the
SDK) produces:

```
Time [s],Type,Value,Note
0.0001234,SOF,1x,
0.0002345,Prio,0x68,
0.0003456,Dest,0x6A,
...
0.0009876,Csum,0x46,OK
0.0010000,IFS,,
```

`Note` is populated for CSUM rows (`OK` / `BAD (calc 0xNN)`) and ERROR rows.
