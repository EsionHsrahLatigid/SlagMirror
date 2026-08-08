#pragma once

#include "slagmirror/SlagMirrorDspPrimitives.h"

namespace slagmirror
{

/** Realtime-safe parameter set for the SlagMirror stereo reflection effect.

    All values are sanitized by setParameters():
    mirror [0, 1], tilt [0, 1], scatter [0, 1], feedback [0, 1],
    mix [0, 1], outputGain [0, 2].
*/
struct SlagMirrorParameters
{
    float mirror = 0.62f;
    float tilt = 0.58f;
    float scatter = 0.42f;
    float feedback = 0.28f;
    float mix = 0.72f;
    float outputGain = 0.82f;
};

/** Stereo sample-domain mirror folder with reflective all-pass scatter.

    The engine is an audio effect only: silence remains silence after reset, it
    allocates no memory in process calls, and every output sample is finite and
    clipped below the hard 0.98 ceiling.
*/
class SlagMirrorEngine
{
public:
    SlagMirrorEngine();

    /** Sets the sample rate and rebuilds tilt filters; invalid rates fall back to 44.1 kHz. */
    void prepare (double sampleRate) noexcept;

    /** Clears feedback, filters, and scatter memories. */
    void reset() noexcept;

    /** Applies sanitized parameters and updates filter coefficients. */
    void setParameters (const SlagMirrorParameters& parameters) noexcept;

    /** Renders one stereo effect frame from one stereo input frame. */
    [[nodiscard]] StereoFrame processSample (float inputLeft, float inputRight) noexcept;

    /** Processes stereo buffers in place when both pointers are valid. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** Exposes the pre-safety feedback coefficient for regression tests. */
    [[nodiscard]] float getFeedbackGainForTests() const noexcept;

    /** Stateless mirror-fold identity used by tests and documentation. */
    [[nodiscard]] static float mirrorFoldSample (float input, float mirror) noexcept;

private:
    struct ClampedParameters
    {
        float mirror = 0.62f;
        float tilt = 0.58f;
        float scatter = 0.42f;
        float feedback = 0.28f;
        float mix = 0.72f;
        float outputGain = 0.82f;
    };

    void updateFilters() noexcept;
    [[nodiscard]] float applyTilt (float input, OnePoleLowPass& lowPass) const noexcept;
    [[nodiscard]] StereoFrame sanitizeFrame (float left, float right) const noexcept;

    ClampedParameters params;
    double sampleRate = 44100.0;
    float feedbackGain = 0.0f;
    float previousLeft = 0.0f;
    float previousRight = 0.0f;

    OnePoleLowPass tiltLeft;
    OnePoleLowPass tiltRight;
    AllPassScatter scatterLeftA;
    AllPassScatter scatterRightA;
    AllPassScatter scatterLeftB;
    AllPassScatter scatterRightB;
    DcBlocker dcLeft;
    DcBlocker dcRight;
};

} // namespace slagmirror
