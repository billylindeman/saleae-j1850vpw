# CLAUDE.md — guide for future Claude sessions on this repo

This is a Saleae Logic 2 protocol analyzer for SAE J1850 VPW. It builds against
the Saleae AnalyzerSDK (digital-only plugin framework) and produces a shared
library that Logic 2 loads at startup.

## What this repo is for (and isn't)

- **Is**: a single Saleae Logic 2 analyzer plugin.
- **Is**: cross-compilable from Linux/WSL to a Windows DLL via clang-cl + xwin.
- **Isn't**: a tool for converting captures between formats. We tried writing
  `.sr ↔ .sal` converters earlier in the project and they didn't pan out — see
  the "Saleae .sal format" section below for why. Don't try again unless you
  have authoritative Saleae documentation.
- **Isn't**: an analog-signal analyzer. The Saleae Analyzer SDK is digital-only
  (`AnalyzerChannelData::GetBitState()` is the only sample-access API). If a
  user only captured analog, they have to **recapture** with the digital
  channel enabled on the same probe (the threshold is set at capture time in
  Logic 2's capture settings, not as a post-processing step on an existing
  analog channel — there is no such post-process feature).

## Repository layout

```
saleae-j1850vpw/
├── CMakeLists.txt                          # top-level build
├── cmake/
│   ├── ExternalAnalyzerSDK.cmake           # locates sibling AnalyzerSDK/ or fetches via git
│   └── WindowsCrossToolchain.cmake         # clang-cl + xwin cross-compile to Windows
├── src/                                    # analyzer source — 4 classes + entry
│   ├── J1850VpwAnalyzer.{h,cpp}                    # Analyzer2 subclass, state machine
│   ├── J1850VpwAnalyzerSettings.{h,cpp}            # Channel/polarity/CRC toggle UI
│   ├── J1850VpwAnalyzerResults.{h,cpp}             # bubble text + CSV export
│   └── J1850VpwSimulationDataGenerator.{h,cpp}     # synthetic test waveforms
└── tests/
    ├── CMakeLists.txt
    ├── FrameV2Stubs.cpp                    # plugs gaps in upstream testlib (see "Test harness")
    ├── captures/P01_bench_by_itself.bin    # converted real capture
    ├── convert_sr.py                       # capture .sr → test-input edge bin (numpy-free)
    ├── test_real_capture.cpp               # replays bench capture, asserts CRCs pass
    └── test_synthetic.cpp                  # 6 interval-based unit tests
```

`../AnalyzerSDK/` (sibling to this dir) is expected; the cmake helper prefers
it over a FetchContent download. `../SampleAnalyzer/` was the template we
based the structure on.

## Build / test commands

```sh
# Linux .so (default; tests included)
cmake -S . -B build
cmake --build build -j
(cd build && ctest --output-on-failure)
# → build/Analyzers/libJ1850VpwAnalyzer.so

# Windows DLL via xwin cross-compile (tests off — testlib uses host libc)
cmake -S . -B build-win \
      -DCMAKE_TOOLCHAIN_FILE=cmake/WindowsCrossToolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DJ1850VPW_BUILD_TESTS=OFF
cmake --build build-win -j
# → build-win/Analyzers/J1850VpwAnalyzer.dll
```

The Windows toolchain requires `clang-cl`, `lld-link`, `llvm-rc`, `llvm-lib`
in PATH, and an xwin splat at `$XWIN_ROOT` (defaults to `~/.xwin/cache`).
Setup instructions live in README.md.

## Critical invariants — do not regress

### 1. Bit-encoding rule — SAE J1850 spec, not the common online references

In `J1850VpwAnalyzer.cpp::WorkerThread`, the bit-classification rule is:

```cpp
// active  short = 1     active  long = 0
// passive short = 0     passive long = 1
```

This is the SAE J1850 spec rule. Several open-source J1850 VPW decoder
references found online have these inverted. When we initially built the
analyzer with the wrong rule, every CRC failed against a real bench capture
and decoded bytes came out bitwise-inverted from the transmitted values.
With the spec rule, all CRCs in `P01_bench_by_itself.bin` verify cleanly.
The simulation generator and synthetic tests use the same spec rule.

If a code review against another implementation says you "have the polarity
backwards," verify against an actual capture's CRCs before changing this.

### 2. CSUM byte is deferred to EOF

`emitByte()` only emits Frame records immediately for byte indices 0-3
(PRIO/DEST/SRC/MODE) — the four mandatory header bytes. Bytes 4+ are
buffered in `mBytes`. At EOF (`emitEof()`), the last buffered byte is
relabeled as CSUM, the rest become DATA frames, and CRC is computed over
header+data.

Why: if you emit each byte's Frame on receipt, the final byte would be
labeled DATA, then a *second* CSUM Frame would be emitted at EOF for the
same byte. The original implementation did this and the synthetic tests
caught the duplicate frames. Do not move byte emission back to live-stream
mode unless you also delete the per-byte Frame for the trailing byte at EOF.

### 3. The single-piece `.sr` write in `convert_sr.py`

`tests/convert_sr.py` writes `logic-1-1` as one zip member, not split into
10 MB chunks. Multi-piece writes need zero-padded indices
(`logic-1-01` … `logic-1-49`) because pulseview reads zip entries in
lexical order, not numerical. With a single piece this is a non-issue.

## Test harness quirks

The SDK ships `AnalyzerSDK/testlib/` with `MockChannelData`, `MockResults`,
and a `TestInstance` plumbing class. Several upstream issues we work around
in `tests/CMakeLists.txt` and `tests/FrameV2Stubs.cpp`:

| Issue | Workaround |
|---|---|
| `SettingsStubs.cpp` uses `std::runtime_error` without `#include <stdexcept>` | `target_compile_options(... -include stdexcept)` for non-MSVC, `/FI stdexcept` for MSVC |
| `MockResultData::CurrentFrame()` returns `size() - 1`, `AnalyzerResults::GetNumFrames()` returns that off-by-one count, and `GetFrame(GetNumFrames()-1)` asserts | Access frames directly via `MockResultData::MockFromResults(res)->TotalFrameCount()` and `->GetFrame(i)` |
| testlib lacks stubs for `FrameV2::*`, `AnalyzerResults::AddFrameV2`, and `Analyzer::UseFrameV2` | `tests/FrameV2Stubs.cpp` provides no-op stubs |
| testlib's `CMakeLists.txt` hardcodes `${PROJECT_SOURCE_DIR}/AnalyzerSDK` for include dirs (wrong relative path for us) | We compile testlib sources directly into a local static lib rather than `add_subdirectory()` |

The test build does NOT link against `Saleae::AnalyzerSDK` — the testlib
ships its own stubs for everything except `FrameV2` (which we add).

`tests/convert_sr.py` produces a custom edge-pack binary
(`{u64 rate, u64 n_trans, u8 initial, u64 trans[n]}`) — *not* a generic
format. It's only used by `test_real_capture.cpp`.

## Settings and frame schema

`J1850VpwAnalyzerSettings`:
- `mInputChannel: Channel`
- `mActiveLevel: U32` — 0 = active-low (transceiver output, default), 1 = active-high (raw bus)
- `mVerifyChecksum: bool` — flips the error flag on CSUM frames when set

Legacy `Frame::mType` values (defined in `J1850VpwAnalyzerResults.h`):

```
0=SOF, 1=PRIO, 2=DEST, 3=SRC, 4=MODE, 5=DATA, 6=CSUM,
7=EOF, 8=IFS, 9=NORM (in-frame-response, unused), 10=ERROR
```

FrameV2 types emitted: `byte`, `sof`, `csum`, `error`, `message`.

## Saleae `.sal` format — explicitly out of scope

The `.sal` zip layout (from inspecting real Logic-Pro-2 captures):

- `meta.json` — JSON; includes UI state, `binData` channel→file map, and
  `legacyDeviceCalibration.fullScaleVoltageRanges` per channel
- `analog-N.bin` / `digital-N.bin` (hyphens, NOT underscores) — DEFLATE
  compressed inside the zip
- `trigger-store.bin` (~32 bytes)

After decompression, both `analog-N.bin` and `digital-N.bin` have a
`<SALEAE>`-magic header (~75 bytes for analog) and **then internally
chunked payload that we have not decoded**. For analog, naively reading
the decompressed bytes as flat int16 LE produces periodic garbage spikes
every ~218,493 samples — there are embedded markers inside the stream.
The digital binary body is also opaque (tried int64/float64/i32 with
multiple header offsets, none gave monotonic values).

**Do not write a Saleae `.sal` parser** unless authoritative docs exist or
the user provides a reference capture small enough to reverse-engineer
against known content. The earlier session lost hours to this. Logic 2's
own CSV export (digital: `time_s, state`) is the supported escape hatch.

## Logic Pro 2 ADC calibration (for any future analog work)

If you ever do need to read raw analog samples from Logic Pro 2 captures:

- ADC: **14-bit signed**, ±12 V full scale
- Sample stored as int16 (no shift)
- Scale: `v = sample × 24 / 16384` ≈ **1.465 mV/LSB**

The `legacyDeviceCalibration.fullScaleVoltageRanges` (~±10.4 V per channel)
in `meta.json` describes the *calibrated-accurate* sub-range, **not** the
int16-to-voltage mapping. Using it as the scale (off by ~5×) was the bug
that wasted a session.

Verified anchors on real captures:

| Observed | Sample | × 1.465 mV | Reality |
|---|---|---|---|
| Pre-engine quiet | 48 LSB | 70 mV | ADC offset / no signal |
| Engine-on bus idle | ~205 LSB | 300 mV | confirmed by user |
| Noise floor | ~683 LSB | 1.00 V | confirmed by user |
| Engine-start glitch | ~-887 LSB | -1.30 V | confirmed by user |
| J1850 active | ~4778 LSB | 7.00 V | confirmed by user |
| Relay-kick transient | 6826 LSB | 10.0 V | confirmed by user |

## Conventions / style

- Filenames: `J1850Vpw…` prefix on everything in `src/` and matching test
  files in `tests/` — keeps the file list grep-friendly.
- C-linkage entry points (`CreateAnalyzer`, `DestroyAnalyzer`,
  `GetAnalyzerName`) live at the bottom of `J1850VpwAnalyzer.cpp`. They
  must be linked into both the plugin module AND the test executables
  (the SDK testlib calls `CreateAnalyzer()` via `extern "C"`).
- C++11 (matches the SDK). No C++14+ features (digit separators caused
  a build error earlier in development).
- No emoji in code, settings strings, or analyzer-emitted text — Saleae
  Logic 2's bubble-text rendering doesn't always handle them well, and
  it'd look out of place next to Saleae's own analyzers.
