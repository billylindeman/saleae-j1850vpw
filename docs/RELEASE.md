# Release process

The GitHub Actions workflow `.github/workflows/build.yml` produces release
binaries automatically when you push a `v*` tag.

## What the pipeline does

Three jobs in one workflow:

| Job | Runner | Output | Tests |
|---|---|---|---|
| `linux`   | `ubuntu-latest`   | `libJ1850VpwAnalyzer.so`  | yes (ctest) |
| `windows` | `windows-latest`  | `J1850VpwAnalyzer.dll`    | skipped |
| `release` | `ubuntu-latest`   | attaches both as release assets | — |

The `release` job only runs on tag pushes matching `v*` (so tags like
`v1.0.0`, `v0.2.0-rc1`, etc.). Tag names containing `-` are flagged as
prereleases automatically.

Each platform job:

1. Checks out this repo at `saleae-j1850vpw/`
2. Checks out `saleae/AnalyzerSDK` at the sibling `AnalyzerSDK/`
3. Runs `cmake --install build --prefix install` to put binaries in a
   predictable location regardless of generator (MSBuild on Windows is
   multi-config and otherwise puts files under `Release/`)
4. Uploads the binary as a workflow artifact

The release job downloads both artifacts and uploads them to a new release
named after the tag, with the asset filenames including the version, e.g.:

```
J1850VpwAnalyzer-v1.0.0-linux-x86_64.so
J1850VpwAnalyzer-v1.0.0-windows-x86_64.dll
```

## Cutting a release

```sh
git tag -a v1.0.0 -m "v1.0.0"
git push origin v1.0.0
```

GitHub Actions picks up the tag push, runs the builds, and creates a draft
release with the binaries attached. `generate_release_notes: true` pulls in
PR titles since the previous tag — review and publish the release once it
appears at `https://github.com/<owner>/<repo>/releases`.

## CI on PRs and master

Every PR and every push to `master`/`main` runs the `linux` and `windows`
build jobs (no release). The Linux job runs the test suite; failures block
the green check.

The `workflow_dispatch` trigger lets you also kick a build manually from the
Actions tab — useful for testing CI changes without a code push.

## Adding macOS

Not currently included; would be straightforward to add. A starter
`macos-latest` (Apple Silicon) job:

```yaml
macos-arm64:
  name: macOS arm64
  runs-on: macos-latest
  steps:
    - uses: actions/checkout@v4
      with: { path: saleae-j1850vpw }
    - uses: actions/checkout@v4
      with:
        repository: saleae/AnalyzerSDK
        path: AnalyzerSDK
    - run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
      working-directory: saleae-j1850vpw
    - run: cmake --build build -j
      working-directory: saleae-j1850vpw
    - run: cmake --install build --prefix install --config Release
      working-directory: saleae-j1850vpw
    - uses: actions/upload-artifact@v4
      with:
        name: J1850VpwAnalyzer-macos-arm64
        path: saleae-j1850vpw/install/Analyzers/libJ1850VpwAnalyzer.dylib
```

For x86_64 macOS, swap `macos-latest` for `macos-13` (Intel runners) and
`CMAKE_OSX_ARCHITECTURES=x86_64`. Don't try to build both arches in one
command (`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`) — the AnalyzerSDK config
rejects universal binaries.
