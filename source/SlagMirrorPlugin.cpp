#include "SlagMirrorPlugin.h"

#include "ProductState.h"

#if ! SLAGMIRROR_HEADLESS_TEST
#include "ParameterGridEditor.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace slagmirror::plugin
{
namespace
{
constexpr std::array<char, 4> stateMagic {{ 'S', 'L', 'M', '1' }};
constexpr int stateVersion = 1;
constexpr std::size_t presetParameterCount = 6;

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
constexpr bool standaloneAuditionCompiled = true;
#else
constexpr bool standaloneAuditionCompiled = false;
#endif

constexpr std::array<std::array<float, presetParameterCount>, 4> presetValues {{
    {{ 0.62f, 0.58f, 0.42f, 0.28f, 0.72f, 0.82f }},
    {{ 0.86f, 0.34f, 0.68f, 0.45f, 0.84f, 0.74f }},
    {{ 0.48f, 0.77f, 0.92f, 0.18f, 0.66f, 0.88f }},
    {{ 0.95f, 0.52f, 0.76f, 0.64f, 0.92f, 0.62f }}
}};

yup::AudioParameter::Ptr makeParameter (const char* id,
                                        const char* name,
                                        int hostID,
                                        float defaultValue,
                                        float smoothingMs)
{
    return yup::AudioParameterBuilder()
        .withID (id)
        .withName (name)
        .withHostID (static_cast<yup::uint32> (hostID))
        .withRange (0.0f, 1.0f)
        .withDefault (defaultValue)
        .withSmoothing (smoothingMs)
        .withModulatable (true)
        .withUnit (yup::AudioParameter::ParameterUnit::Percent)
        .build();
}

yup::AudioParameter::Ptr makeOutputParameter (int hostID, float defaultValue)
{
    return yup::AudioParameterBuilder()
        .withID ("output")
        .withName ("Output")
        .withHostID (static_cast<yup::uint32> (hostID))
        .withRange (0.0f, 2.0f)
        .withDefault (defaultValue)
        .withSmoothing (30.0f)
        .withModulatable (true)
        .build();
}

float noiseSample (std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    if (state == 0u)
        state = 0x6d2b79f5u;
    return static_cast<float> (static_cast<double> (state) / 2147483648.0 - 1.0);
}

float wrapPhase (float phase) noexcept
{
    return phase >= 1.0f ? phase - std::floor (phase) : phase;
}
} // namespace

SlagMirrorPlugin::SlagMirrorPlugin()
    : yup::AudioProcessor ("SlagMirror",
                           yup::AudioBusLayout ({
                                                    yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Input, 2),
                                                },
                                                {
                                                    yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Output, 2),
                                                }))
{
    parameters[mirror] = makeParameter ("mirror", "Mirror", mirror, presetValues[0][mirror], 18.0f);
    parameters[tilt] = makeParameter ("tilt", "Tilt", tilt, presetValues[0][tilt], 35.0f);
    parameters[scatter] = makeParameter ("scatter", "Scatter", scatter, presetValues[0][scatter], 24.0f);
    parameters[feedback] = makeParameter ("feedback", "Feedback", feedback, presetValues[0][feedback], 40.0f);
    parameters[mix] = makeParameter ("mix", "Mix", mix, presetValues[0][mix], 20.0f);
    parameters[output] = makeOutputParameter (output, presetValues[0][output]);

    for (const auto& parameter : parameters)
        addParameter (parameter);
}

void SlagMirrorPlugin::prepareToPlay (const yup::AudioSpec& spec)
{
    sampleRate = spec.sampleRate > 1.0 ? spec.sampleRate : 44100.0;
    engine.prepare (sampleRate);

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i] = yup::AudioParameterHandle (*parameters[i], sampleRate);
        smoothedValues[i] = parameters[i]->getValue();
    }

    engine.reset();
    applyEngineParameters();
    resetRuntimeState();
}

void SlagMirrorPlugin::releaseResources()
{
}

void SlagMirrorPlugin::processBlock (yup::AudioProcessContext<float>& context)
{
    auto& audio = context.audio;
    const auto numSamples = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
        parameterHandles[i].prepareBlock (context.params, parameters[i]->getIndexInContainer());

    auto* left = numChannels > 0 ? audio.getWritePointer (0) : nullptr;
    auto* right = numChannels > 1 ? audio.getWritePointer (1) : nullptr;
    float blockInputPeak = 0.0f;
    float blockOutputPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        advanceParameterHandles (sample);
        if (controlUpdateCountdown <= 0)
        {
            applyEngineParameters();
            controlUpdateCountdown = parameterUpdateCadenceSamples;
        }
        --controlUpdateCountdown;

        float inputLeft = left != nullptr ? left[sample] : 0.0f;
        float inputRight = right != nullptr ? right[sample] : inputLeft;

        if constexpr (standaloneAuditionCompiled)
        {
            if (standaloneAuditionEnabled.load (std::memory_order_relaxed) != 0)
            {
                const auto audition = nextAuditionFrame();
                inputLeft = audition.left;
                inputRight = audition.right;
            }
        }

        blockInputPeak = std::max (blockInputPeak, std::max (std::fabs (inputLeft), std::fabs (inputRight)));
        const auto frame = engine.processSample (inputLeft, inputRight);
        blockOutputPeak = std::max (blockOutputPeak, std::max (std::fabs (frame.left), std::fabs (frame.right)));

        if (left != nullptr)
            left[sample] = frame.left;
        if (right != nullptr)
            right[sample] = frame.right;

        for (int channel = 2; channel < numChannels; ++channel)
            audio.getWritePointer (channel)[sample] = 0.0f;
    }

    inputPeakMilli.store (static_cast<int> (std::clamp (blockInputPeak, 0.0f, 1.0f) * 1000.0f + 0.5f),
                          std::memory_order_relaxed);
    outputPeakMilli.store (static_cast<int> (std::clamp (blockOutputPeak, 0.0f, 1.0f) * 1000.0f + 0.5f),
                           std::memory_order_relaxed);
    context.midi.clear();
}

void SlagMirrorPlugin::flush()
{
    engine.reset();
    resetRuntimeState();
}

bool SlagMirrorPlugin::acceptsMidi() const noexcept
{
    return false;
}

bool SlagMirrorPlugin::producesMidi() const noexcept
{
    return false;
}

int SlagMirrorPlugin::getCurrentPreset() const noexcept
{
    return currentPreset.load (std::memory_order_relaxed);
}

void SlagMirrorPlugin::setCurrentPreset (int index) noexcept
{
    if (! yup::isPositiveAndBelow (index, static_cast<int> (presetValues.size())))
        return;

    currentPreset.store (index, std::memory_order_relaxed);
    for (std::size_t i = 0; i < parameters.size(); ++i)
        parameters[i]->setValue (presetValues[static_cast<std::size_t> (index)][i]);
}

int SlagMirrorPlugin::getNumPresets() const
{
    return static_cast<int> (presetNames.size());
}

yup::String SlagMirrorPlugin::getPresetName (int index) const
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        return presetNames[static_cast<std::size_t> (index)];
    return "Invalid Preset";
}

void SlagMirrorPlugin::setPresetName (int index, yup::StringRef newName)
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        presetNames[static_cast<std::size_t> (index)] = newName;
}

yup::Result SlagMirrorPlugin::loadStateFromMemory (const yup::MemoryBlock& data)
{
    auto presetIndex = currentPreset.load (std::memory_order_relaxed);
    const auto result = loadProductState (*this, data, stateMagic, stateVersion, getNumPresets(), presetIndex);
    if (result.wasOk())
        currentPreset.store (presetIndex, std::memory_order_relaxed);
    return result;
}

yup::Result SlagMirrorPlugin::saveStateIntoMemory (yup::MemoryBlock& data)
{
    return saveProductState (*this, data, stateMagic, stateVersion, currentPreset.load (std::memory_order_relaxed));
}

bool SlagMirrorPlugin::hasEditor() const
{
#if SLAGMIRROR_HEADLESS_TEST
    return false;
#else
    return true;
#endif
}

yup::AudioProcessorEditor* SlagMirrorPlugin::createEditor()
{
#if SLAGMIRROR_HEADLESS_TEST
    return nullptr;
#else
    return new ParameterGridEditor (*this,
                                    "SlagMirror",
                                    "Molten mirror plates, fractured symmetry, sample-domain reflection.",
                                    0xfff2f2f0u);
#endif
}

void SlagMirrorPlugin::setStandaloneAuditionEnabled (bool shouldBeEnabled) noexcept
{
    if constexpr (standaloneAuditionCompiled)
        standaloneAuditionEnabled.store (shouldBeEnabled ? 1 : 0, std::memory_order_relaxed);
    else
        standaloneAuditionEnabled.store (0, std::memory_order_relaxed);
}

void SlagMirrorPlugin::setStandaloneAuditionType (int type) noexcept
{
    if constexpr (standaloneAuditionCompiled)
        standaloneAuditionType.store (std::clamp (type, 0, 2), std::memory_order_relaxed);
    else
        standaloneAuditionType.store (0, std::memory_order_relaxed);
}

bool SlagMirrorPlugin::isStandaloneAuditionEnabled() const noexcept
{
    return standaloneAuditionEnabled.load (std::memory_order_relaxed) != 0;
}

int SlagMirrorPlugin::getStandaloneAuditionType() const noexcept
{
    return standaloneAuditionType.load (std::memory_order_relaxed);
}

bool SlagMirrorPlugin::isStandaloneAuditionAvailable() const noexcept
{
    return standaloneAuditionCompiled;
}

float SlagMirrorPlugin::getInputPeakLevel() const noexcept
{
    return static_cast<float> (inputPeakMilli.load (std::memory_order_relaxed)) * 0.001f;
}

float SlagMirrorPlugin::getOutputPeakLevel() const noexcept
{
    return static_cast<float> (outputPeakMilli.load (std::memory_order_relaxed)) * 0.001f;
}

void SlagMirrorPlugin::advanceParameterHandles (int samplePosition) noexcept
{
    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i].advanceToSample (samplePosition);
        smoothedValues[i] = parameterHandles[i].getNextValue();
    }
}

void SlagMirrorPlugin::applyEngineParameters() noexcept
{
    slagmirror::SlagMirrorParameters engineParameters;
    engineParameters.mirror = smoothedValues[mirror];
    engineParameters.tilt = smoothedValues[tilt];
    engineParameters.scatter = smoothedValues[scatter];
    engineParameters.feedback = smoothedValues[feedback];
    engineParameters.mix = smoothedValues[mix];
    engineParameters.outputGain = smoothedValues[output];
    engine.setParameters (engineParameters);
}

void SlagMirrorPlugin::resetRuntimeState() noexcept
{
    controlUpdateCountdown = 0;
    auditionPhaseA = 0.0f;
    auditionPhaseB = 0.0f;
    auditionNoise = 0x6d2b79f5u;
    inputPeakMilli.store (0, std::memory_order_relaxed);
    outputPeakMilli.store (0, std::memory_order_relaxed);
}

slagmirror::StereoFrame SlagMirrorPlugin::nextAuditionFrame() noexcept
{
    const auto type = standaloneAuditionType.load (std::memory_order_relaxed);
    const auto rate = static_cast<float> (sampleRate > 1.0 ? sampleRate : 44100.0);
    auditionPhaseA = wrapPhase (auditionPhaseA + (type == 1 ? 73.0f : 110.0f) / rate);
    auditionPhaseB = wrapPhase (auditionPhaseB + (type == 2 ? 293.0f : 181.0f) / rate);

    if (type == 1)
    {
        const auto stepped = auditionPhaseA < 0.5f ? 0.36f : -0.36f;
        return { stepped, -stepped * 0.82f };
    }

    if (type == 2)
    {
        const auto noisy = noiseSample (auditionNoise) * 0.25f;
        const auto tone = std::sin (2.0f * std::numbers::pi_v<float> * auditionPhaseB) * 0.18f;
        return { noisy + tone, noisy * -0.7f + tone * 0.4f };
    }

    const auto left = std::sin (2.0f * std::numbers::pi_v<float> * auditionPhaseA) * 0.32f;
    const auto right = std::sin (2.0f * std::numbers::pi_v<float> * auditionPhaseB) * 0.28f;
    return { left, right };
}

} // namespace slagmirror::plugin

extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new slagmirror::plugin::SlagMirrorPlugin();
}
