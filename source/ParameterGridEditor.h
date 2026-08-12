#pragma once

#include <yup_audio_processors/yup_audio_processors.h>
#include <yup_gui/yup_gui.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace slagmirror::plugin
{

class SlagMirrorPlugin;

} // namespace slagmirror::plugin

namespace ehl::ui
{
class StripMeter;
}

namespace slagmirror::plugin
{

/** Native YUP parameter grid with standalone-only audition and stereo metering. */
class ParameterGridEditor final
    : public yup::AudioProcessorEditor
    , private yup::Timer
{
public:
    ParameterGridEditor (yup::AudioProcessor& processor,
                         yup::StringRef title,
                         yup::StringRef warning,
                         std::uint32_t accentColor);
    ~ParameterGridEditor() override;

    bool isResizable() const override;
    bool shouldPreserveAspectRatio() const override;
    yup::Size<int> getPreferredSize() const override;
    void paint (yup::Graphics& graphics) override;
    void resized() override;
    void focusLost() override;
    void keyDown (const yup::KeyPress& key, const yup::Point<float>& position) override;
    void keyUp (const yup::KeyPress& key, const yup::Point<float>& position) override;

private:
    class AuditionButton;
    class CycleTypeButton;

    void timerCallback() override;
    void publishAudition();
    void refreshAuditionButtons();

    yup::String title;
    yup::String warning;
    std::unique_ptr<yup::Label> titleLabel;
    std::unique_ptr<yup::Label> warningLabel;
    std::vector<yup::AudioParameter::Ptr> parameters;
    std::vector<std::unique_ptr<yup::Label>> labels;
    std::vector<std::unique_ptr<yup::Slider>> sliders;
    std::vector<std::unique_ptr<yup::Label>> valueLabels;
    std::unique_ptr<AuditionButton> auditionButton;
    std::unique_ptr<CycleTypeButton> auditionTypeButton;
    std::unique_ptr<yup::Label> meterLabel;
    std::unique_ptr<ehl::ui::StripMeter> inputMeter;
    std::unique_ptr<ehl::ui::StripMeter> outputMeter;
    SlagMirrorPlugin* slagmirrorProcessor = nullptr;
    float displayedInputPeak = 0.0f;
    float displayedOutputPeak = 0.0f;
    bool auditionHeld = false;
    bool spaceAuditionHeld = false;
};

} // namespace slagmirror::plugin
