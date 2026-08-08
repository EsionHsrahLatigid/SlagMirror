#include "SlagMirrorPlugin.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
constexpr int numChannels = 2;
constexpr int blockSamples = 4096;

class PluginHarness
{
public:
    PluginHarness()
        : audio (numChannels, blockSamples)
        , context { audio, midi, automation, nullptr, {}, {} }
    {
        plugin.prepareToPlay (yup::AudioSpec (48000.0f, blockSamples, numChannels));
    }

    void clearAudio()
    {
        audio.clear();
    }

    void fillStereoImpulse()
    {
        audio.clear();
        audio.getWritePointer (0)[0] = 0.7f;
        audio.getWritePointer (1)[0] = -0.25f;
    }

    void fillStereoTone()
    {
        auto* left = audio.getWritePointer (0);
        auto* right = audio.getWritePointer (1);
        for (int i = 0; i < blockSamples; ++i)
        {
            left[i] = std::sin (static_cast<float> (i) * 0.021f) * 0.45f;
            right[i] = std::sin (static_cast<float> (i) * 0.037f) * -0.35f;
        }
    }

    float process()
    {
        plugin.processBlock (context);
        return peak();
    }

    std::array<float, blockSamples> processLeft()
    {
        plugin.processBlock (context);
        std::array<float, blockSamples> result {};
        const auto* samples = audio.getReadPointer (0);
        for (int sample = 0; sample < blockSamples; ++sample)
            result[static_cast<std::size_t> (sample)] = samples[sample];
        return result;
    }

    float peak() const
    {
        float result = 0.0f;
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            const auto* samples = audio.getReadPointer (channel);
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                result = std::max (result, std::fabs (samples[sample]));
        }
        return result;
    }

    float rms() const
    {
        double sumSquares = 0.0;
        int count = 0;
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            const auto* samples = audio.getReadPointer (channel);
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            {
                sumSquares += static_cast<double> (samples[sample]) * static_cast<double> (samples[sample]);
                ++count;
            }
        }
        return count > 0 ? static_cast<float> (std::sqrt (sumSquares / static_cast<double> (count))) : 0.0f;
    }

    slagmirror::plugin::SlagMirrorPlugin plugin;

private:
    yup::AudioBuffer<float> audio;
    yup::MidiBuffer midi;
    yup::ParameterChangeBuffer automation;
    yup::AudioProcessContext<float> context;
};

void testHostedPathIsEffectAndSilencePreserving()
{
    PluginHarness harness;
    assert (! harness.plugin.acceptsMidi());
    assert (! harness.plugin.producesMidi());
#if ! defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    assert (! harness.plugin.isStandaloneAuditionAvailable());
#endif

    harness.clearAudio();
    const auto silencePeak = harness.process();
    assert (silencePeak <= 1.0e-7f);
    assert (harness.plugin.getInputPeakLevel() <= 1.0e-7f);
    assert (harness.plugin.getOutputPeakLevel() <= 1.0e-7f);

    harness.fillStereoTone();
    const auto audioPeak = harness.process();
    assert (audioPeak > 1.0e-4f);
    assert (audioPeak <= 0.9801f);
    assert (harness.plugin.getInputPeakLevel() > 0.1f);
    assert (harness.plugin.getOutputPeakLevel() > 0.1f);
}

void testHostedAuditionFailsClosed()
{
    PluginHarness harness;
#if ! defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    harness.plugin.setStandaloneAuditionEnabled (true);
    harness.plugin.setStandaloneAuditionType (2);
    assert (! harness.plugin.isStandaloneAuditionEnabled());
    assert (harness.plugin.getStandaloneAuditionType() == 0);

    harness.clearAudio();
    assert (harness.process() <= 1.0e-7f);
#else
    assert (harness.plugin.isStandaloneAuditionAvailable());
#endif
}

void testOldEscapeMacroDoesNotExposeStandaloneAudition()
{
#if defined(SLAGMIRROR_ENABLE_STANDALONE_AUDITION) && ! defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    PluginHarness harness;
    assert (! harness.plugin.isStandaloneAuditionAvailable());
    harness.plugin.setStandaloneAuditionEnabled (true);
    harness.plugin.setStandaloneAuditionType (2);
    assert (! harness.plugin.isStandaloneAuditionEnabled());
    assert (harness.plugin.getStandaloneAuditionType() == 0);

    harness.clearAudio();
    assert (harness.process() <= 1.0e-7f);
#endif
}

void testHostedDeterministic()
{
    PluginHarness first;
    PluginHarness second;

    first.fillStereoTone();
    second.fillStereoTone();
    const auto a = first.processLeft();
    const auto b = second.processLeft();
    for (std::size_t i = 0; i < a.size(); ++i)
        assert (std::fabs (a[i] - b[i]) <= 1.0e-6f);
}

void testStateRoundTripDoesNotSerializeAudition()
{
    PluginHarness source;
    source.plugin.setCurrentPreset (2);
    auto parameters = source.plugin.getParameters();
    parameters[0]->setValue (0.91f);
    parameters[1]->setValue (0.22f);
    parameters[2]->setValue (0.73f);
    parameters[3]->setValue (0.44f);
    parameters[4]->setValue (0.65f);
    parameters[5]->setValue (1.31f);
    source.plugin.setStandaloneAuditionEnabled (true);
    source.plugin.setStandaloneAuditionType (2);

    yup::MemoryBlock state;
    assert (source.plugin.saveStateIntoMemory (state).wasOk());

    PluginHarness target;
    target.plugin.setStandaloneAuditionEnabled (false);
    target.plugin.setStandaloneAuditionType (0);
    assert (target.plugin.loadStateFromMemory (state).wasOk());

    const auto targetParameters = target.plugin.getParameters();
    assert (target.plugin.getCurrentPreset() == 2);
    assert (std::fabs (targetParameters[0]->getValue() - 0.91f) < 1.0e-6f);
    assert (std::fabs (targetParameters[1]->getValue() - 0.22f) < 1.0e-6f);
    assert (std::fabs (targetParameters[2]->getValue() - 0.73f) < 1.0e-6f);
    assert (std::fabs (targetParameters[3]->getValue() - 0.44f) < 1.0e-6f);
    assert (std::fabs (targetParameters[4]->getValue() - 0.65f) < 1.0e-6f);
    assert (std::fabs (targetParameters[5]->getValue() - 1.31f) < 1.0e-6f);
    assert (! target.plugin.isStandaloneAuditionEnabled());
    assert (target.plugin.getStandaloneAuditionType() == 0);
}

void testMetersFollowInputAndOutput()
{
    PluginHarness harness;
    harness.fillStereoImpulse();
    const auto peak = harness.process();
    assert (peak > 1.0e-4f);
    assert (harness.plugin.getInputPeakLevel() >= 0.69f);
    assert (harness.plugin.getOutputPeakLevel() > 1.0e-4f);
}

void testStandaloneAuditionBridge()
{
    PluginHarness harness;

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    assert (harness.plugin.isStandaloneAuditionAvailable());
    harness.plugin.setStandaloneAuditionType (2);
    harness.plugin.setStandaloneAuditionEnabled (true);
    assert (harness.plugin.isStandaloneAuditionEnabled());
    assert (harness.plugin.getStandaloneAuditionType() == 2);
    harness.clearAudio();
    const auto peak = harness.process();
    assert (peak > 1.0e-4f);
    assert (harness.rms() >= 1.0e-4f);
    assert (harness.plugin.getInputPeakLevel() > 1.0e-4f);
    assert (harness.plugin.getOutputPeakLevel() > 1.0e-4f);

    float sumSquares = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        harness.clearAudio();
        harness.process();
        const auto level = harness.plugin.getOutputPeakLevel();
        sumSquares += level * level;
    }
    assert (std::sqrt (sumSquares / 8.0f) >= 1.0e-4f);
#else
    assert (! harness.plugin.isStandaloneAuditionAvailable());
#endif
}
} // namespace

int main()
{
    testHostedPathIsEffectAndSilencePreserving();
    testHostedAuditionFailsClosed();
    testOldEscapeMacroDoesNotExposeStandaloneAudition();
    testHostedDeterministic();
    testStateRoundTripDoesNotSerializeAudition();
    testMetersFollowInputAndOutput();
    testStandaloneAuditionBridge();

    std::cout << "SlagMirrorPluginBridgeTests passed\n";
    return 0;
}
