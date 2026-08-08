#include "ParameterGridEditor.h"

#include "SlagMirrorPlugin.h"

#include <algorithm>
#include <functional>

namespace slagmirror::plugin
{
namespace
{
class AuditionButton final : public yup::TextButton
{
public:
    using yup::TextButton::TextButton;

    std::function<void (bool)> onHeldChanged;

    void mouseDown (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseDown (event);
        if (onHeldChanged)
            onHeldChanged (true);
    }

    void mouseUp (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseUp (event);
        if (onHeldChanged)
            onHeldChanged (false);
    }

    void mouseExit (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseExit (event);
        if (onHeldChanged && isButtonDown())
            onHeldChanged (false);
    }
};

class CycleTypeButton final : public yup::TextButton
{
public:
    using yup::TextButton::TextButton;

    std::function<void()> onCycle;

    void mouseUp (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseUp (event);
        if (onCycle)
            onCycle();
    }
};

class LevelMeter final : public yup::Component
{
public:
    explicit LevelMeter (std::uint32_t newColor)
        : color (newColor)
    {
    }

    void setLevel (float newLevel)
    {
        level = std::clamp (newLevel, 0.0f, 1.0f);
        repaint();
    }

    void paint (yup::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds();
        graphics.setFillColor (0xff101216u);
        graphics.fillRect (bounds.to<float>());

        graphics.setFillColor (0xff262a31u);
        graphics.fillRect (bounds.withTrimmedLeft (bounds.getWidth() * level).to<float>());

        graphics.setFillColor (color);
        graphics.fillRect (0.0f, 0.0f, bounds.getWidth() * level, bounds.getHeight());
    }

private:
    std::uint32_t color = 0xffff6f2du;
    float level = 0.0f;
};

const char* auditionTypeName (int type) noexcept
{
    switch (type)
    {
        case 1: return "Plate";
        case 2: return "Ash";
        default: return "Sine";
    }
}
} // namespace

ParameterGridEditor::ParameterGridEditor (yup::AudioProcessor& processor,
                                          yup::StringRef newTitle,
                                          yup::StringRef newWarning,
                                          std::uint32_t newAccentColor)
    : title (newTitle)
    , warning (newWarning)
    , accentColor (newAccentColor)
{
    slagmirrorProcessor = dynamic_cast<SlagMirrorPlugin*> (&processor);

    const auto processorParameters = processor.getParameters();
    parameters.assign (processorParameters.begin(), processorParameters.end());

    titleLabel = std::make_unique<yup::Label>();
    titleLabel->setText (title, yup::dontSendNotification);
    titleLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*titleLabel);

    warningLabel = std::make_unique<yup::Label>();
    warningLabel->setText (warning, yup::dontSendNotification);
    warningLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*warningLabel);

    labels.reserve (parameters.size());
    sliders.reserve (parameters.size());
    valueLabels.reserve (parameters.size());

    for (const auto& parameter : parameters)
    {
        auto label = std::make_unique<yup::Label>();
        label->setText (parameter->getName(), yup::dontSendNotification);
        label->setJustification (yup::Justification::center);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        auto slider = std::make_unique<yup::Slider> (yup::Slider::RotaryVerticalDrag);
        slider->setRange (parameter->getMinimumValue(),
                          parameter->getMaximumValue(),
                          parameter->isStepped() ? 1.0 : 0.0);
        slider->setDefaultValue (parameter->getDefaultValue());
        slider->setValue (parameter->getValue(), yup::dontSendNotification);
        slider->setTextBoxStyle (yup::Slider::NoTextBox);
        slider->setPopupDisplayEnabled (false);
        slider->setMouseCursor (yup::MouseCursor::Hand);
        slider->setClickingGrabFocus (false);
        slider->onDragStart = [parameter] (const yup::MouseEvent&) { parameter->beginChangeGesture(); };
        slider->onValueChanged = [parameter] (double value)
        {
            parameter->setValueNotifyingHost (static_cast<float> (value));
        };
        slider->onDragEnd = [parameter] (const yup::MouseEvent&) { parameter->endChangeGesture(); };
        addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));

        auto valueLabel = std::make_unique<yup::Label>();
        valueLabel->setText (parameter->toString(), yup::dontSendNotification);
        valueLabel->setJustification (yup::Justification::center);
        addAndMakeVisible (*valueLabel);
        valueLabels.push_back (std::move (valueLabel));
    }

    if (slagmirrorProcessor != nullptr)
    {
        if (slagmirrorProcessor->isStandaloneAuditionAvailable())
        {
            auditionButton = std::make_unique<AuditionButton>();
            auditionButton->setMouseCursor (yup::MouseCursor::Hand);
            auditionButton->setClickingGrabFocus (false);
            static_cast<AuditionButton*> (auditionButton.get())->onHeldChanged = [this] (bool shouldBeOn)
            {
                auditionHeld = shouldBeOn;
                publishAudition();
            };
            addAndMakeVisible (*auditionButton);

            auditionTypeButton = std::make_unique<CycleTypeButton>();
            auditionTypeButton->setMouseCursor (yup::MouseCursor::Hand);
            auditionTypeButton->setClickingGrabFocus (false);
            static_cast<CycleTypeButton*> (auditionTypeButton.get())->onCycle = [this]
            {
                if (slagmirrorProcessor != nullptr)
                {
                    slagmirrorProcessor->setStandaloneAuditionType (slagmirrorProcessor->getStandaloneAuditionType() + 1);
                    refreshAuditionButtons();
                }
            };
            addAndMakeVisible (*auditionTypeButton);
        }

        meterLabel = std::make_unique<yup::Label>();
        meterLabel->setText ("Input / Output", yup::dontSendNotification);
        meterLabel->setJustification (yup::Justification::centerLeft);
        addAndMakeVisible (*meterLabel);

        inputMeter = std::make_unique<LevelMeter> (0xff74d8b4u);
        outputMeter = std::make_unique<LevelMeter> (accentColor);
        addAndMakeVisible (*inputMeter);
        addAndMakeVisible (*outputMeter);

        setWantsKeyboardFocus (true);
        refreshAuditionButtons();
    }

    setSize (getPreferredSize().to<float>());
    startTimerHz (30);
}

ParameterGridEditor::~ParameterGridEditor()
{
    auditionHeld = false;
    spaceAuditionHeld = false;
    publishAudition();
}

bool ParameterGridEditor::isResizable() const
{
    return true;
}

bool ParameterGridEditor::shouldPreserveAspectRatio() const
{
    return true;
}

yup::Size<int> ParameterGridEditor::getPreferredSize() const
{
    return { 960, 540 };
}

void ParameterGridEditor::paint (yup::Graphics& graphics)
{
    graphics.setFillColor (0xff08090bu);
    graphics.fillAll();

    const auto width = static_cast<float> (getWidth());
    graphics.setFillColor (0xff1a1d22u);
    graphics.fillRect (0.0f, 72.0f, width, 18.0f);
    graphics.fillRect (0.0f, 300.0f, width, 10.0f);

    graphics.setFillColor (0xff2e3138u);
    graphics.fillRect (24.0f, 82.0f, width * 0.28f, 8.0f);
    graphics.fillRect (width * 0.42f, 72.0f, width * 0.18f, 18.0f);
    graphics.fillRect (width * 0.72f, 82.0f, width * 0.20f, 8.0f);

    graphics.setFillColor (accentColor);
    graphics.fillRect (0.0f, 0.0f, width, 5.0f);
    graphics.fillRect (width * 0.48f, 72.0f, 5.0f, 238.0f);
}

void ParameterGridEditor::resized()
{
    constexpr int columns = 6;
    constexpr float margin = 20.0f;
    constexpr float top = 168.0f;
    constexpr float gap = 10.0f;
    constexpr float labelHeight = 24.0f;
    constexpr float valueHeight = 24.0f;
    constexpr float controlGap = 4.0f;

    const auto bounds = getLocalBounds();
    const auto cellWidth = (bounds.getWidth() - 2.0f * margin - gap * (columns - 1)) / columns;
    const auto rows = std::max (1, static_cast<int> ((sliders.size() + columns - 1) / columns));
    const auto availableHeight = bounds.getHeight() - top - margin;
    const auto cellHeight = (availableHeight - gap * (rows - 1)) / rows;

    titleLabel->setBounds (24.0f, 12.0f, bounds.getWidth() - 48.0f, 30.0f);
    warningLabel->setBounds (24.0f, 43.0f, bounds.getWidth() - 48.0f, 24.0f);

    constexpr float buttonWidth = 120.0f;
    constexpr float controlHeight = 32.0f;
    auto meterX = margin;
    if (auditionButton != nullptr && auditionTypeButton != nullptr)
    {
        auditionButton->setBounds (margin, 102.0f, buttonWidth, controlHeight);
        auditionTypeButton->setBounds (margin + buttonWidth + gap, 102.0f, buttonWidth, controlHeight);
        meterX = margin + 2.0f * buttonWidth + 2.0f * gap;
    }

    if (meterLabel != nullptr && inputMeter != nullptr && outputMeter != nullptr)
    {
        const auto meterWidth = std::max (140.0f, (bounds.getWidth() - margin - meterX - 88.0f) * 0.5f);
        meterLabel->setBounds (meterX, 102.0f, 88.0f, controlHeight);
        inputMeter->setBounds (meterX + 88.0f, 106.0f, meterWidth, 10.0f);
        outputMeter->setBounds (meterX + 88.0f, 122.0f, meterWidth, 10.0f);
    }

    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + column * (cellWidth + gap);
        const auto y = top + row * (cellHeight + gap);
        const auto controlHeightLocal = cellHeight - labelHeight - valueHeight - 2.0f * controlGap;
        const auto controlSize = std::max (20.0f, std::min (cellWidth - 8.0f, controlHeightLocal));
        const auto controlX = x + 0.5f * (cellWidth - controlSize);
        const auto controlY = y + labelHeight + controlGap;

        labels[i]->setBounds (x, y, cellWidth, labelHeight);
        sliders[i]->setBounds (controlX, controlY, controlSize, controlSize);
        valueLabels[i]->setBounds (x, y + cellHeight - valueHeight, cellWidth, valueHeight);
    }
}

void ParameterGridEditor::focusLost()
{
    yup::AudioProcessorEditor::focusLost();
    auditionHeld = false;
    spaceAuditionHeld = false;
    publishAudition();
}

void ParameterGridEditor::keyDown (const yup::KeyPress& key, const yup::Point<float>& position)
{
    yup::AudioProcessorEditor::keyDown (key, position);

    if (key.getKey() == yup::KeyPress::spaceKey && ! spaceAuditionHeld)
    {
        spaceAuditionHeld = true;
        publishAudition();
    }
}

void ParameterGridEditor::keyUp (const yup::KeyPress& key, const yup::Point<float>& position)
{
    yup::AudioProcessorEditor::keyUp (key, position);

    if (key.getKey() == yup::KeyPress::spaceKey)
    {
        spaceAuditionHeld = false;
        publishAudition();
    }
}

void ParameterGridEditor::timerCallback()
{
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        if (! sliders[i]->isCurrentlyBeingDragged())
            sliders[i]->setValue (parameters[i]->getValue(), yup::dontSendNotification);
        valueLabels[i]->setText (parameters[i]->toString(), yup::dontSendNotification);
    }

    if (slagmirrorProcessor != nullptr && inputMeter != nullptr && outputMeter != nullptr)
    {
        displayedInputPeak = std::max (slagmirrorProcessor->getInputPeakLevel(), displayedInputPeak * 0.82f);
        displayedOutputPeak = std::max (slagmirrorProcessor->getOutputPeakLevel(), displayedOutputPeak * 0.82f);
        static_cast<LevelMeter*> (inputMeter.get())->setLevel (displayedInputPeak);
        static_cast<LevelMeter*> (outputMeter.get())->setLevel (displayedOutputPeak);
    }

    refreshAuditionButtons();
}

void ParameterGridEditor::publishAudition()
{
    if (slagmirrorProcessor != nullptr)
        slagmirrorProcessor->setStandaloneAuditionEnabled (auditionHeld || spaceAuditionHeld);
    refreshAuditionButtons();
}

void ParameterGridEditor::refreshAuditionButtons()
{
    if (slagmirrorProcessor == nullptr)
        return;

    if (auditionButton != nullptr)
        auditionButton->setButtonText (slagmirrorProcessor->isStandaloneAuditionEnabled() ? "Audition On" : "Audition");

    if (auditionTypeButton != nullptr)
        auditionTypeButton->setButtonText (auditionTypeName (slagmirrorProcessor->getStandaloneAuditionType()));
}

} // namespace slagmirror::plugin
