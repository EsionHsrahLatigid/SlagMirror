#include "slagmirror/SlagMirrorEngine.h"

#include <algorithm>
#include <cmath>

namespace slagmirror
{

namespace
{
constexpr float ceiling = 0.98f;

float killDenormal (float value) noexcept
{
    if (! std::isfinite (value) || std::fabs (value) < 1.0e-20f)
        return 0.0f;
    return value;
}
} // namespace

SlagMirrorEngine::SlagMirrorEngine()
{
    prepare (44100.0);
    reset();
}

void SlagMirrorEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate > 1.0 ? newSampleRate : 44100.0;
    updateFilters();
}

void SlagMirrorEngine::reset() noexcept
{
    previousLeft = 0.0f;
    previousRight = 0.0f;
    tiltLeft.reset();
    tiltRight.reset();
    scatterLeftA.reset();
    scatterRightA.reset();
    scatterLeftB.reset();
    scatterRightB.reset();
    dcLeft.reset();
    dcRight.reset();
}

void SlagMirrorEngine::setParameters (const SlagMirrorParameters& parameters) noexcept
{
    params.mirror = clampFinite (parameters.mirror, 0.0f, 1.0f, SlagMirrorParameters {}.mirror);
    params.tilt = clampFinite (parameters.tilt, 0.0f, 1.0f, SlagMirrorParameters {}.tilt);
    params.scatter = clampFinite (parameters.scatter, 0.0f, 1.0f, SlagMirrorParameters {}.scatter);
    params.feedback = clampFinite (parameters.feedback, 0.0f, 1.0f, SlagMirrorParameters {}.feedback);
    params.mix = clampFinite (parameters.mix, 0.0f, 1.0f, SlagMirrorParameters {}.mix);
    params.outputGain = clampFinite (parameters.outputGain, 0.0f, 2.0f, SlagMirrorParameters {}.outputGain);

    feedbackGain = params.feedback * 0.86f;
    updateFilters();
}

StereoFrame SlagMirrorEngine::processSample (float inputLeft, float inputRight) noexcept
{
    const auto dryLeft = clampFinite (inputLeft, -4.0f, 4.0f, 0.0f);
    const auto dryRight = clampFinite (inputRight, -4.0f, 4.0f, 0.0f);

    const auto reflectedLeft = dryLeft + previousRight * feedbackGain;
    const auto reflectedRight = dryRight + previousLeft * feedbackGain;
    const auto foldedLeft = mirrorFoldSample (reflectedLeft, params.mirror);
    const auto foldedRight = mirrorFoldSample (reflectedRight, params.mirror);

    const auto scatterCoefficientA = 0.16f + params.scatter * 0.54f;
    const auto scatterCoefficientB = -0.10f - params.scatter * 0.42f;
    const auto firstLeft = scatterLeftA.process (foldedLeft + foldedRight * (0.10f + 0.32f * params.scatter),
                                                 scatterCoefficientA);
    const auto firstRight = scatterRightA.process (foldedRight - foldedLeft * (0.08f + 0.28f * params.scatter),
                                                   -scatterCoefficientA);
    const auto scatteredLeft = scatterLeftB.process (firstLeft + firstRight * params.scatter * 0.30f,
                                                     scatterCoefficientB);
    const auto scatteredRight = scatterRightB.process (firstRight - firstLeft * params.scatter * 0.26f,
                                                       -scatterCoefficientB);

    const auto tiltedLeft = applyTilt (scatteredLeft, tiltLeft);
    const auto tiltedRight = applyTilt (scatteredRight, tiltRight);
    const auto wetLeft = dcLeft.process (tiltedLeft);
    const auto wetRight = dcRight.process (tiltedRight);

    previousLeft = killDenormal (std::clamp (wetLeft, -0.98f, 0.98f));
    previousRight = killDenormal (std::clamp (wetRight, -0.98f, 0.98f));

    const auto mixedLeft = (dryLeft * (1.0f - params.mix) + wetLeft * params.mix) * params.outputGain;
    const auto mixedRight = (dryRight * (1.0f - params.mix) + wetRight * params.mix) * params.outputGain;
    return sanitizeFrame (mixedLeft, mixedRight);
}

void SlagMirrorEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto frame = processSample (left[i], right[i]);
        left[i] = frame.left;
        right[i] = frame.right;
    }
}

float SlagMirrorEngine::getFeedbackGainForTests() const noexcept
{
    return feedbackGain;
}

float SlagMirrorEngine::mirrorFoldSample (float input, float mirror) noexcept
{
    const auto safeInput = clampFinite (input, -4.0f, 4.0f, 0.0f);
    const auto amount = clampFinite (mirror, 0.0f, 1.0f, SlagMirrorParameters {}.mirror);
    const auto threshold = 1.0f - amount * 0.72f;
    const auto magnitude = std::fabs (safeInput);
    const auto sign = safeInput < 0.0f ? -1.0f : 1.0f;

    auto foldedMagnitude = magnitude;
    if (magnitude > threshold)
    {
        const auto period = threshold * 2.0f;
        const auto wrapped = std::fmod (magnitude - threshold, period);
        foldedMagnitude = threshold - std::fabs (wrapped - threshold);
    }

    const auto folded = sign * foldedMagnitude;
    return safeInput * (1.0f - amount) + folded * amount;
}

void SlagMirrorEngine::updateFilters() noexcept
{
    const auto cutoff = 520.0f + params.tilt * 5600.0f;
    tiltLeft.prepare (sampleRate, cutoff);
    tiltRight.prepare (sampleRate, cutoff * (1.0f + params.scatter * 0.05f));
    dcLeft.prepare (sampleRate, 4.0f);
    dcRight.prepare (sampleRate, 4.0f);
}

float SlagMirrorEngine::applyTilt (float input, OnePoleLowPass& lowPass) const noexcept
{
    const auto low = lowPass.process (input);
    const auto high = input - low;
    const auto bipolar = params.tilt * 2.0f - 1.0f;

    if (bipolar < 0.0f)
        return low * (-bipolar) + input * (1.0f + bipolar);

    return input * (1.0f - 0.35f * bipolar) + high * (0.85f * bipolar);
}

StereoFrame SlagMirrorEngine::sanitizeFrame (float left, float right) const noexcept
{
    const auto safeLeft = boundedDrive (left, 1.12f);
    const auto safeRight = boundedDrive (right, 1.12f);
    return { std::clamp (safeLeft, -ceiling, ceiling),
             std::clamp (safeRight, -ceiling, ceiling) };
}

} // namespace slagmirror
