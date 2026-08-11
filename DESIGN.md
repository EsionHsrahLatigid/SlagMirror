# SlagMirror Design

SlagMirror v0.1.0 is an independent YUP stereo effect. Its sound identity is unstable reflective coloration: sample-domain mirror folding, cross-channel reflection, short all-pass scatter, tilt-filter proxy shaping, controlled feedback, mix, and output safety.

## Product Shape

- Formats: Standalone and VST3 on macOS and Windows; AUv2 additionally on macOS.
- Stable IDs: app `jp.ehl.slagmirror`, plugin `jp.ehl.slagmirror`, AU subtype `SlMr`.
- Host parameters: `Mirror`, `Tilt`, `Scatter`, `Feedback`, `Mix`, `Output`.
- State format: parameter ID/value pairs with magic `SLM1` and version `1`.
- Hosted wrappers: stereo input to effect to stereo output; no internal generator.
- Standalone wrapper: optional audition source only when `YUP_AUDIO_PLUGIN_ENABLE_STANDALONE` is present; no project-local macro can enable it.

## DSP Checklist

- Sample-domain mirror fold is odd-symmetric: `fold(-x) == -fold(x)`.
- `Mirror` increases folding around a moving threshold without FFT or block analysis.
- `Scatter` controls cross-channel reflection and two first-order all-pass cells.
- `Tilt` uses one-pole low/high proxy filtering as a spectral tilt approximation.
- Parameter updates refresh coefficients without clearing filter or DC-blocker state.
- `Feedback` is sanitized to [0, 1] and converted to a pre-safety gain below 1.0.
- `Mix` blends dry and wet in the audio sample path.
- `Output` supports 0 to 2 gain before the final safety stage.
- Final samples are finite and hard bounded by the 0.98 ceiling.
- Denormal-scale state is zeroed in feedback/filter memories.
- Silence after reset remains silence in hosted processing.
- Realtime callbacks allocate no memory and perform no locks, logging, file I/O, or UI calls.

## Standalone Runtime Checklist

- Audition enable/type are runtime atomics, not host parameters.
- Audition enable/type are not saved or loaded by product state.
- Hosted builds fail closed: audition setters cannot enable generation.
- Standalone builds use the same processor/effect path after inserting audition input.
- Input and output meters are display-only scaled atomics.
- Editor focus loss and destruction disable held audition state.

## Visual Checklist

- Visual direction: molten mirror plates, fractured symmetry, high-contrast readable controls.
- Implementation: native YUP labels, rotary sliders, text buttons, simple meter components, and drawn rect geometry.
- No image assets, SVG assets, shaders, or extra dependencies.
- Parameter controls remain in a stable six-column grid.
- Audition and meter strip sits above the grid and does not overlap parameter labels.

## Test Checklist

- Engine tests prove mirror-fold symmetry and parameter response.
- Engine tests prove scatter-driven stereo crossfeed.
- Engine tests prove tilt changes high-frequency proxy energy.
- Engine tests prove feedback gain remains below 1.0 and changes response.
- Engine tests prove repeated parameter-cadence updates do not clear filter/DC state or create periodic discontinuities.
- Engine tests prove silence preservation, deterministic output, finite output, and peak bound.
- Bridge tests prove hosted effect classification, hosted silence, meters, deterministic processing, state round trip, and audition fail-closed behavior.
- A hosted negative bridge target defines the removed project escape macro without YUP standalone and proves audition is still unavailable.
- Standalone bridge tests compile with `YUP_AUDIO_PLUGIN_ENABLE_STANDALONE` and prove audition RMS/output activity.

## CI and Release Checklist

- GitHub Actions use immutable commit-SHA action pins.
- `CI Summary` is the stable required check.
- Debug jobs build and run engine, hosted bridge, hosted negative bridge, and standalone bridge tests.
- Release jobs build the effect bundle targets and package Standalone/VST3 plus AU on macOS.
- Artifacts are exact, checksummed, and retained for 14 days.
- Tag release promotes artifacts from exactly one successful `CI` push run on `main` for the tag SHA.
- Release verifies CMake version, artifact names, SHA-256 manifests, ZIP integrity, and final asset set.
