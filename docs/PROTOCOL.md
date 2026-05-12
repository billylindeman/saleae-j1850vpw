# SAE J1850 VPW protocol reference

This is a quick reference for the protocol details the analyzer implements,
sourced from SAE J1850 (1995 revision, §23.4). Useful when interpreting raw
captures or extending the analyzer.

## Physical layer

J1850 VPW is a single-wire, half-duplex multidrop bus. The bus has two
electrical states:

- **Passive** — recessive, line at ~0 V, default state when no node drives
- **Active** — dominant, line driven to ~7 V

The bus uses **passive arbitration**: any node may drive it active, and the
network resolves contention by bit-by-bit comparison during the priority byte.

Two speeds are defined:

- **1× mode** — 10.4 kbps nominal bit rate
- **4× mode** — 41.6 kbps nominal bit rate (diagnostic / programming)

The analyzer auto-detects per-frame from the SOF pulse width.

## Symbol timing

All in microseconds; divide by speed factor for 4× operation. The analyzer
uses these ranges (slightly widened on the SOF and short-pulse boundaries
for tolerance):

| Symbol | Min | Typical | Max |
|---|---|---|---|
| **SOF** (Start of Frame) | 164 | 200 | 245 |
| **Long** (=1 active / =0 passive) |  97 | 128 | 170 |
| **Short** (=0 active / =1 passive) |  24 |  64 |  97 |
| **EOF/IFS** (idle) | 240 | — | — |

EOF is the inter-frame separator after a complete message; longer idles
(beyond ~280 µs / speed) are treated as IFS in the analyzer's annotation.

## Bit encoding

Each bit on the wire is **one pulse** at either the active or passive level;
the bus toggles between levels on every bit. The bit value is determined by
**both** the level the pulse is at and how long it lasts:

| Pulse level | Pulse width | Bit value |
|---|---|---|
| Active  | Short | **1** |
| Active  | Long  | **0** |
| Passive | Short | **0** |
| Passive | Long  | **1** |

Bits are transmitted **MSB first** within each byte.

## Frame structure

A complete J1850 VPW message:

```
SOF │ PRIO │ DEST │ SRC │ MODE │ DATA[0..N] │ CRC │ EOF
```

| Field | Bytes | Notes |
|---|---|---|
| SOF  | 0 | 200 µs active pulse |
| PRIO | 1 | Priority / type byte. High 3 bits = priority (000 = highest, 111 = lowest); bit 4 controls in-frame response; bit 3 selects 1-byte vs 3-byte header; low 3 bits historically reserved/zero |
| DEST | 1 | Destination address. `0xFE` / `0xEA` are common functional/broadcast targets |
| SRC  | 1 | Source address — typically the transmitting ECU's known address (e.g. `0x10` for the engine/PCM on most GM platforms) |
| MODE | 1 | Service mode, e.g. `0x01` = current data PIDs (OBD-II), `0x09` = vehicle info, etc. |
| DATA | 0..N | Mode-specific payload. Length is inferred from frame timing (everything between MODE and CRC) |
| CRC  | 1 | Trailing CRC-8 over `PRIO..last DATA` |
| EOF  | 0 | ≥240 µs passive idle |

Minimum valid frame: 4 header bytes + CRC = 5 bytes total. Frames shorter
than 5 bytes are flagged but no CSUM frame is emitted.

## CRC-8

```
poly:    0x1D
init:    0xFF
refin:   no
refout:  no
xorout:  0xFF
```

Reference C++ implementation:

```cpp
U8 j1850_crc(const U8* data, int n) {
    U8 crc = 0xFF;
    for (int i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? U8((crc << 1) ^ 0x1D) : U8(crc << 1);
    }
    return U8(crc ^ 0xFF);
}
```

Standard check vector: CRC of ASCII `"123456789"` is **`0x4B`**.
