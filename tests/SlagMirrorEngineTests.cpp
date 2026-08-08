#include "slagmirror/SlagMirrorEngine.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using slagmirror::SlagMirrorEngine;
using slagmirror::SlagMirrorParameters;

namespace
{
constexpr int sampleCount = 4096;

std::vector<float> makeRamp()
{
    std::vector<float> samples;
    samples.reserve (sampleCount);
    for (int i = 0; i < sampleCount; ++i)
        samples.push_back (std::sin (static_cast<float> (i) * 0.017f) * 0.65f
                           + std::sin (static_cast<float> (i) * 0.071f) * 0.20f);
    return samples;
}

std::array<std::vector<float>, 2> render (SlagMirrorParameters params)
{
    auto left = makeRamp();
    auto right = makeRamp();
    for (std::size_t i = 0; i < right.size(); ++i)
        right[i] = right[i] * -0.42f + std::sin (static_cast<float> (i) * 0.031f) * 0.38f;

    SlagMirrorEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (params);
    engine.process (left.data(), right.data(), static_cast<int> (left.size()));
    return { left, right };
}

float energy (const std::vector<float>& samples)
{
    float result = 0.0f;
    for (const auto sample : samples)
        result += sample * sample;
    return result;
}

float differenceEnergy (const std::vector<float>& a, const std::vector<float>& b)
{
    assert (a.size() == b.size());
    float result = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const auto difference = a[i] - b[i];
        result += difference * difference;
    }
    return result;
}

float highProxyEnergy (const std::vector<float>& samples)
{
    float low = 0.0f;
    float result = 0.0f;
    constexpr float coefficient = 0.82f;
    for (const auto sample : samples)
    {
        low = (1.0f - coefficient) * sample + coefficient * low;
        const auto high = sample - low;
        result += high * high;
    }
    return result;
}

void testMirrorFoldSymmetryAndResponse()
{
    for (float value = -2.0f; value <= 2.0f; value += 0.031f)
    {
        const auto positive = SlagMirrorEngine::mirrorFoldSample (value, 0.91f);
        const auto negative = SlagMirrorEngine::mirrorFoldSample (-value, 0.91f);
        assert (std::fabs (positive + negative) <= 1.0e-6f);
    }

    const auto dry = SlagMirrorEngine::mirrorFoldSample (1.35f, 0.0f);
    const auto folded = SlagMirrorEngine::mirrorFoldSample (1.35f, 1.0f);
    assert (std::fabs (dry - 1.35f) <= 1.0e-6f);
    assert (folded < 0.45f);
}

void testScatterCreatesStereoCrossfeed()
{
    SlagMirrorParameters narrow;
    narrow.mirror = 0.5f;
    narrow.scatter = 0.0f;
    narrow.feedback = 0.0f;
    narrow.mix = 1.0f;

    SlagMirrorParameters scattered = narrow;
    scattered.scatter = 1.0f;

    auto inputLeft = makeRamp();
    std::vector<float> silentRight (inputLeft.size(), 0.0f);
    auto baseLeft = inputLeft;
    auto baseRight = silentRight;
    auto scatterLeft = inputLeft;
    auto scatterRight = silentRight;

    SlagMirrorEngine base;
    base.prepare (48000.0);
    base.setParameters (narrow);
    base.process (baseLeft.data(), baseRight.data(), static_cast<int> (baseLeft.size()));

    SlagMirrorEngine cross;
    cross.prepare (48000.0);
    cross.setParameters (scattered);
    cross.process (scatterLeft.data(), scatterRight.data(), static_cast<int> (scatterLeft.size()));

    assert (energy (scatterRight) > energy (baseRight) * 1.8f + 1.0e-5f);
    assert (differenceEnergy (baseLeft, scatterLeft) > 0.01f);
}

void testTiltParameterChangesSpectrum()
{
    SlagMirrorParameters dark;
    dark.tilt = 0.0f;
    dark.scatter = 0.35f;
    dark.feedback = 0.0f;

    SlagMirrorParameters bright = dark;
    bright.tilt = 1.0f;

    const auto darkRender = render (dark);
    const auto brightRender = render (bright);
    assert (highProxyEnergy (brightRender[0]) > highProxyEnergy (darkRender[0]) * 1.25f);
}

void testFeedbackBoundAndResponse()
{
    SlagMirrorParameters low;
    low.feedback = 0.0f;
    low.mix = 1.0f;

    SlagMirrorParameters high = low;
    high.feedback = 1.0f;

    SlagMirrorEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (high);
    assert (engine.getFeedbackGainForTests() < 1.0f);

    const auto lowRender = render (low);
    const auto highRender = render (high);
    assert (differenceEnergy (lowRender[0], highRender[0]) > 0.01f);
}

void testParameterCadenceKeepsFilterState()
{
    SlagMirrorParameters params;
    params.mirror = 0.72f;
    params.tilt = 0.41f;
    params.scatter = 0.64f;
    params.feedback = 0.36f;
    params.mix = 1.0f;
    params.outputGain = 0.8f;

    SlagMirrorEngine reference;
    reference.prepare (48000.0);
    reference.setParameters (params);

    SlagMirrorEngine cadence;
    cadence.prepare (48000.0);
    cadence.setParameters (params);

    float maxDifference = 0.0f;
    float maxCadenceJump = 0.0f;
    auto previousCadenceFrame = slagmirror::StereoFrame {};

    for (int sample = 0; sample < 4096; ++sample)
    {
        if (sample > 0 && (sample % 16) == 0)
            cadence.setParameters (params);

        const auto inputLeft = 0.37f + std::sin (static_cast<float> (sample) * 0.003f) * 0.03f;
        const auto inputRight = -0.31f + std::cos (static_cast<float> (sample) * 0.004f) * 0.02f;
        const auto expected = reference.processSample (inputLeft, inputRight);
        const auto actual = cadence.processSample (inputLeft, inputRight);

        maxDifference = std::max (maxDifference, std::fabs (expected.left - actual.left));
        maxDifference = std::max (maxDifference, std::fabs (expected.right - actual.right));

        if (sample > 0 && (sample % 16) == 0)
        {
            maxCadenceJump = std::max (maxCadenceJump, std::fabs (actual.left - previousCadenceFrame.left));
            maxCadenceJump = std::max (maxCadenceJump, std::fabs (actual.right - previousCadenceFrame.right));
        }

        previousCadenceFrame = actual;
    }

    assert (maxDifference <= 1.0e-6f);
    assert (maxCadenceJump < 0.08f);
}

void testSilencePreservedAfterReset()
{
    SlagMirrorParameters params;
    params.feedback = 1.0f;
    params.mirror = 1.0f;
    params.scatter = 1.0f;
    params.mix = 1.0f;
    params.outputGain = 2.0f;

    SlagMirrorEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (params);
    engine.reset();

    for (int i = 0; i < 8192; ++i)
    {
        const auto frame = engine.processSample (0.0f, 0.0f);
        assert (std::fabs (frame.left) <= 1.0e-7f);
        assert (std::fabs (frame.right) <= 1.0e-7f);
    }
}

void testDeterministicAndBounded()
{
    SlagMirrorParameters params;
    params.mirror = 0.84f;
    params.tilt = 0.73f;
    params.scatter = 0.91f;
    params.feedback = 0.77f;
    params.mix = 0.88f;
    params.outputGain = 1.4f;

    const auto first = render (params);
    const auto second = render (params);
    assert (differenceEnergy (first[0], second[0]) <= 1.0e-12f);
    assert (differenceEnergy (first[1], second[1]) <= 1.0e-12f);

    for (const auto& channel : first)
    {
        for (const auto sample : channel)
        {
            assert (std::isfinite (sample));
            assert (sample >= -0.9801f && sample <= 0.9801f);
        }
    }
}

void testNonFiniteParametersFallbackSafely()
{
    SlagMirrorParameters params;
    params.mirror = std::numeric_limits<float>::quiet_NaN();
    params.tilt = std::numeric_limits<float>::infinity();
    params.scatter = -std::numeric_limits<float>::infinity();
    params.feedback = std::numeric_limits<float>::quiet_NaN();
    params.mix = std::numeric_limits<float>::infinity();
    params.outputGain = std::numeric_limits<float>::quiet_NaN();

    SlagMirrorEngine engine;
    engine.prepare (std::numeric_limits<double>::infinity());
    engine.setParameters (params);

    for (int i = 0; i < 4096; ++i)
    {
        const auto frame = engine.processSample (std::sin (static_cast<float> (i) * 0.11f), 0.0f);
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
        assert (frame.left >= -0.9801f && frame.left <= 0.9801f);
        assert (frame.right >= -0.9801f && frame.right <= 0.9801f);
    }
}
} // namespace

int main()
{
    testMirrorFoldSymmetryAndResponse();
    testScatterCreatesStereoCrossfeed();
    testTiltParameterChangesSpectrum();
    testFeedbackBoundAndResponse();
    testParameterCadenceKeepsFilterState();
    testSilencePreservedAfterReset();
    testDeterministicAndBounded();
    testNonFiniteParametersFallbackSafely();

    std::cout << "SlagMirrorEngineTests passed\n";
    return 0;
}
