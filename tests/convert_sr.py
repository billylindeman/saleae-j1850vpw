#!/usr/bin/env python3
"""Convert a `.sr` capture session zip to a compact edge-transition binary
the J1850 VPW analyzer's unit tests can load.

The input is a zip archive containing a `metadata` INI file (with at least
`samplerate=` and `unitsize=` fields) and one or more `<capturefile>-N` data
members holding raw packed-byte sample data, one byte per sample.

Binary layout (little-endian):
    u64 sample_rate_hz
    u64 num_transitions
    u8  initial_bit_state    (0 or 1, value of the selected probe at sample 0)
    u64 * num_transitions    (absolute sample numbers where the probe toggles)

Usage:
    python3 convert_sr.py <input.sr> <output.bin> [probe_bit]
"""

from __future__ import annotations

import struct
import sys
import zipfile
from configparser import ConfigParser


def parse_samplerate(s: str) -> int:
    parts = s.split()
    val = float(parts[0])
    unit = parts[1] if len(parts) > 1 else "Hz"
    mult = {"Hz": 1, "kHz": 1_000, "MHz": 1_000_000, "GHz": 1_000_000_000}[unit]
    return int(val * mult)


def convert(src_sr: str, dst_bin: str, probe_bit: int = 0) -> None:
    with zipfile.ZipFile(src_sr) as z:
        meta_text = z.read("metadata").decode()
        cfg = ConfigParser()
        cfg.read_string(meta_text)

        dev = next(s for s in cfg.sections() if s.startswith("device"))
        rate = parse_samplerate(cfg[dev]["samplerate"])
        capfile = cfg[dev]["capturefile"]
        unit = int(cfg[dev]["unitsize"])

        # Sort capturefile-1, -2, -3 ... numerically.
        def piece_idx(name: str) -> int:
            return int(name.rsplit("-", 1)[1])

        pieces = sorted(
            (n for n in z.namelist() if n.startswith(capfile + "-")),
            key=piece_idx,
        )

        transitions: list[int] = []
        sample = 0
        last_bit: int | None = None
        initial = 0
        mask = 1 << probe_bit

        for name in pieces:
            data = z.read(name)
            # Each sample is `unit` bytes (typically 1). Reading bit 0 only.
            view = memoryview(data)
            for i in range(0, len(view), unit):
                byte = view[i]
                bit = 1 if (byte & mask) else 0
                if last_bit is None:
                    last_bit = bit
                    initial = bit
                elif bit != last_bit:
                    transitions.append(sample)
                    last_bit = bit
                sample += 1

    with open(dst_bin, "wb") as f:
        f.write(struct.pack("<QQB", rate, len(transitions), initial))
        # Pack transitions in chunks for performance.
        for chunk_start in range(0, len(transitions), 65536):
            chunk = transitions[chunk_start : chunk_start + 65536]
            f.write(struct.pack(f"<{len(chunk)}Q", *chunk))

    print(
        f"converted {src_sr}\n"
        f"  -> {dst_bin}\n"
        f"  sample rate: {rate} Hz\n"
        f"  total samples: {sample}\n"
        f"  transitions:  {len(transitions)}\n"
        f"  initial bit:  {initial}",
        file=sys.stderr,
    )


def main(argv: list[str]) -> int:
    if len(argv) < 3 or len(argv) > 4:
        print(__doc__, file=sys.stderr)
        return 2
    src, dst = argv[1], argv[2]
    bit = int(argv[3]) if len(argv) == 4 else 0
    convert(src, dst, bit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
