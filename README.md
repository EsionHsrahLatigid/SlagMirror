# SlagMirror

SlagMirror is a YUP-based stereo audio effect and standalone app. It folds audio in the sample domain, reflects energy across channels through all-pass scatter, applies a cheap spectral-tilt proxy, then controls feedback, mix, and output without FFT dependencies.

## Identity

- Version: `0.1.0`
- App ID: `jp.ehl.slagmirror`
- Plugin ID: `jp.ehl.slagmirror`
- AU subtype: `SlMr`
- Vendor: `2bit`
- Type: stereo input to stereo output effect
- Host parameters: `Mirror`, `Tilt`, `Scatter`, `Feedback`, `Mix`, `Output`
- macOS formats: Standalone, VST3, AUv2
- Windows formats: Standalone, VST3

## Standalone Audition

Hosted plugin wrappers are strictly input to effect to output. They do not generate sound from silence.

The standalone wrapper compiles an audition bridge only when YUP defines `YUP_AUDIO_PLUGIN_ENABLE_STANDALONE`. The editor exposes runtime-only audition enable/type controls plus input/output meters. Audition state is not a host parameter and is not serialized.

## Build

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug
ctest --preset engine-debug
```

```sh
cmake --preset plugin-release
cmake --build --preset plugin-release
ctest --preset plugin-release
```

Release bundles are generated under `build/plugin-release` by YUP's plugin targets:

- `slagmirror_release_bundles`
- `slagmirror_standalone_plugin`
- `slagmirror_vst3_plugin`
- `slagmirror_au_plugin` on Apple platforms

## CI

`.github/workflows/ci.yml` is the required CI entrypoint for pushes to `main`, pull requests, and manual runs. A Linux classifier always runs. Changes limited to `README.md`, `DESIGN.md`, `LICENSE`, `docs/**`, or `.github/ISSUE_TEMPLATE/**` skip the heavy jobs; every other change runs Debug tests and Release bundle builds on macOS arm64 and Windows x64. Manual dispatches default to forcing both heavy jobs.

Successful heavy runs upload two immutable, 14-day artifacts:

- `SlagMirror-latest-macos-arm64`, containing `SlagMirror-latest-macos-arm64.zip` and `SHA256SUMS.txt`
- `SlagMirror-latest-windows-x64`, containing `SlagMirror-latest-windows-x64.zip` and `SHA256SUMS.txt`

`.github/workflows/release.yml` is the only `v*` tag workflow. It performs no compilation. The Ubuntu release job resolves lightweight or annotated tags to a commit, requires the tag version to match the CMake project version, requires one successful `CI` push run on `main` for that exact SHA, downloads exactly the two expected artifacts, verifies their SHA-256 manifests and ZIP integrity, then publishes versioned assets such as `SlagMirror-0.1.0-macos-arm64.zip` and `SlagMirror-0.1.0-windows-x64.zip`. Missing, expired, ambiguous, or mismatched provenance fails closed.

## Layout

- `include/slagmirror/` contains the realtime-safe DSP engine API and local DSP primitives.
- `source/` contains the engine implementation and YUP plugin/editor/state wrapper.
- `tests/` contains deterministic engine regression tests plus hosted, hosted-negative, and standalone bridge tests.
- `cmake/` contains the project-local macOS icon conversion workaround used by the standalone target.
