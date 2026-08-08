#pragma once

#include "slagmirror/SlagMirrorEngine.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace slagmirror::plugin
{

class SlagMirrorPlugin final : public yup::AudioProcessor
{
public:
    SlagMirrorPlugin();

    void prepareToPlay (const yup::AudioSpec& spec) override;
    void releaseResources() override;
    void processBlock (yup::AudioProcessContext<float>& context) override;
    void flush() override;

    bool acceptsMidi() const noexcept override;
    bool producesMidi() const noexcept override;

    int getCurrentPreset() const noexcept override;
    void setCurrentPreset (int index) noexcept override;
    int getNumPresets() const override;
    yup::String getPresetName (int index) const override;
    void setPresetName (int index, yup::StringRef newName) override;

    yup::Result loadStateFromMemory (const yup::MemoryBlock& data) override;
    yup::Result saveStateIntoMemory (yup::MemoryBlock& data) override;

    bool hasEditor() const override;
    yup::AudioProcessorEditor* createEditor() override;

    void setStandaloneAuditionEnabled (bool shouldBeEnabled) noexcept;
    void setStandaloneAuditionType (int type) noexcept;
    [[nodiscard]] bool isStandaloneAuditionEnabled() const noexcept;
    [[nodiscard]] int getStandaloneAuditionType() const noexcept;
    [[nodiscard]] bool isStandaloneAuditionAvailable() const noexcept;
    [[nodiscard]] float getInputPeakLevel() const noexcept;
    [[nodiscard]] float getOutputPeakLevel() const noexcept;

private:
    enum ParameterIndex
    {
        mirror,
        tilt,
        scatter,
        feedback,
        mix,
        output,
        parameterCount
    };

    static constexpr int parameterUpdateCadenceSamples = 16;

    void advanceParameterHandles (int samplePosition) noexcept;
    void applyEngineParameters() noexcept;
    void resetRuntimeState() noexcept;
    [[nodiscard]] slagmirror::StereoFrame nextAuditionFrame() noexcept;

    std::array<yup::AudioParameter::Ptr, parameterCount> parameters;
    std::array<yup::AudioParameterHandle, parameterCount> parameterHandles;
    std::array<float, parameterCount> smoothedValues {};
    slagmirror::SlagMirrorEngine engine;

    int controlUpdateCountdown = 0;
    double sampleRate = 44100.0;
    float auditionPhaseA = 0.0f;
    float auditionPhaseB = 0.0f;
    std::uint32_t auditionNoise = 0x6d2b79f5u;
    std::atomic<int> standaloneAuditionEnabled { 0 };
    std::atomic<int> standaloneAuditionType { 0 };
    std::atomic<int> inputPeakMilli { 0 };
    std::atomic<int> outputPeakMilli { 0 };
    std::atomic<int> currentPreset { 0 };
    std::array<yup::String, 4> presetNames {
        "Molten Fold",
        "Glass Furnace",
        "Reverse Plate",
        "Chrome Spill"
    };
};

} // namespace slagmirror::plugin
