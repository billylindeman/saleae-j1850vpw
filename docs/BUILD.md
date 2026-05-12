# Build, install, and test

The build is plain CMake against the [Saleae AnalyzerSDK](https://github.com/saleae/AnalyzerSDK).
`cmake/ExternalAnalyzerSDK.cmake` will prefer a sibling `../AnalyzerSDK/`
checkout if present; otherwise it fetches the SDK via `FetchContent`.

```
parent/
├── AnalyzerSDK/         # optional — preferred when present
├── SampleAnalyzer/      # not needed at build time, kept for reference
└── saleae-j1850vpw/     # this repo
```

## Linux

```sh
cmake -S . -B build
cmake --build build -j
```

Output: `build/Analyzers/libJ1850VpwAnalyzer.so`.

Install: copy the `.so` to Logic 2's custom-analyzers directory (in Logic 2,
**Edit → Preferences → Custom Low Level Analyzers**, click "Open Containing
Folder"). Restart Logic 2.

## Windows (native build)

Tested with Visual Studio 2019/2022 + CMake. Same commands as Linux but with
the MSVC generator:

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Output: `build\Analyzers\Release\J1850VpwAnalyzer.dll`.

## Windows DLL cross-compiled from Linux/WSL

`cmake/WindowsCrossToolchain.cmake` builds a Windows DLL using `clang-cl`
+ `lld-link` against MSVC headers/libs fetched by
[xwin](https://github.com/Jake-Shadle/xwin). One-time host setup:

```sh
# 1. LLVM tools — provides lld-link, llvm-rc, llvm-lib
sudo apt-get install lld llvm clang

# 2. clang-cl is just clang invoked with MSVC driver mode (selected by the
#    binary name). Symlink it somewhere on PATH:
mkdir -p ~/.local/bin
ln -sf /usr/bin/clang-14 ~/.local/bin/clang-cl
export PATH="$HOME/.local/bin:$PATH"

# 3. Install xwin and splat the MSVC SDK
cargo install --locked xwin
xwin --accept-license --arch x86_64 --variant desktop splat \
     --include-debug-libs --output ~/.xwin/cache
```

That's ~1 GB of MSVC headers + libs in `~/.xwin/cache` (point `XWIN_ROOT` env
var elsewhere if you want).

Then configure + build:

```sh
cmake -S . -B build-win \
      -DCMAKE_TOOLCHAIN_FILE=cmake/WindowsCrossToolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DJ1850VPW_BUILD_TESTS=OFF
cmake --build build-win -j
```

Output: `build-win/Analyzers/J1850VpwAnalyzer.dll`. Verify with
`file build-win/Analyzers/*.dll` — should report `PE32+ executable (DLL) (GUI)
x86-64, for MS Windows`.

The DLL links against the release CRT (`MSVCP140.dll`, `VCRUNTIME140.dll`,
UCRT), all of which ship with Saleae Logic 2. No additional runtime install
needed on the user's machine.

### Notes on the cross-build

- The MSVC STL pulled by xwin asserts Clang 19+ via a `static_assert`.
  The toolchain bypasses this with `/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH`;
  Clang 14 works fine for the C++11 features we use.
- Tests are disabled by default for the Windows cross-build
  (`-DJ1850VPW_BUILD_TESTS=OFF`) because the SDK's `testlib` uses host-side
  libc behavior that doesn't survive the cross-build cleanly.

## Tests

Unit tests use the SDK's `testlib/` test harness (expects sibling
`../AnalyzerSDK/`). They build by default; pass `-DJ1850VPW_BUILD_TESTS=OFF`
to skip.

```sh
cmake -S . -B build
cmake --build build -j
(cd build && ctest --output-on-failure)
```

Two suites:

- **`test_synthetic`** — interval-based unit tests covering SOF 1×/4×
  detection, bit classification, byte assembly, EOF/IFS, polarity inversion,
  and CRC pass/fail.
- **`test_real_capture`** — replays the converted bench capture
  `tests/captures/P01_bench_by_itself.bin` through the analyzer and asserts
  ≥1 SOF, ≥1 CSUM-OK, no CSUM-bad frames.

The `.bin` format is a custom edge-pack used only by the test harness; it's
not a general-purpose capture format. See `tests/convert_sr.py` for the
layout if you want to regenerate the test capture from a different source.
