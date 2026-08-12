#include "ParameterGridEditor.h"

#include <ehl/yup_plugin_ui/EhlPluginTheme.h>
#include "SlagMirrorPlugin.h"

#include <algorithm>
#include <functional>

namespace slagmirror::plugin
{
class ParameterGridEditor::AuditionButton final : public yup::TextButton
{
public:
    using yup::TextButton::TextButton;

    std::function<void (bool)> onHeldChanged;

    void setSelected (bool shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }

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

    void paintButton (yup::Graphics& graphics) override
    {
        paintLocalCommandButton (graphics, selected || isButtonDown());
    }

private:
    void paintLocalCommandButton (yup::Graphics& graphics, bool active)
    {
        const auto bounds = getLocalBounds().to<float>();
        const auto over = isButtonOver();

        graphics.setFillColor (active ? ehl::ui::paper : (over ? ehl::ui::mid : ehl::ui::low));
        graphics.fillRect (bounds);
        graphics.setStrokeColor (hasKeyboardFocus() ? ehl::ui::paper : ehl::ui::mid);
        graphics.setStrokeWidth (hasKeyboardFocus() ? 2.0f : 1.0f);
        graphics.strokeRect (bounds.reduced (1.0f));

        graphics.setFillColor (active || over ? ehl::ui::ink : ehl::ui::paper);
        graphics.fillFittedText (getStyledText(), getTextBounds());
    }

    bool selected = false;
};

class ParameterGridEditor::CycleTypeButton final : public yup::TextButton
{
public:
    using yup::TextButton::TextButton;

    std::function<void()> onCycle;

    void setSelected (bool shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }

    void mouseUp (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseUp (event);
        if (onCycle)
            onCycle();
    }

    void paintButton (yup::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().to<float>();
        const auto active = selected || isButtonDown();
        const auto over = isButtonOver();

        graphics.setFillColor (active ? ehl::ui::paper : (over ? ehl::ui::mid : ehl::ui::low));
        graphics.fillRect (bounds);
        graphics.setStrokeColor (hasKeyboardFocus() ? ehl::ui::paper : ehl::ui::mid);
        graphics.setStrokeWidth (hasKeyboardFocus() ? 2.0f : 1.0f);
        graphics.strokeRect (bounds.reduced (1.0f));

        graphics.setFillColor (active || over ? ehl::ui::ink : ehl::ui::paper);
        graphics.fillFittedText (getStyledText(), getTextBounds());
    }

private:
    bool selected = false;
};

namespace
{
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
{
    (void) newAccentColor;

    slagmirrorProcessor = dynamic_cast<SlagMirrorPlugin*> (&processor);

    const auto processorParameters = processor.getParameters();
    parameters.assign (processorParameters.begin(), processorParameters.end());

    titleLabel = std::make_unique<yup::Label>();
    titleLabel->setText (title, yup::dontSendNotification);
    titleLabel->setJustification (yup::Justification::centerLeft);
    ehl::ui::styleLabel (*titleLabel, ehl::ui::TextRole::primary);
    addAndMakeVisible (*titleLabel);

    warningLabel = std::make_unique<yup::Label>();
    warningLabel->setText (warning, yup::dontSendNotification);
    warningLabel->setJustification (yup::Justification::centerLeft);
    ehl::ui::styleLabel (*warningLabel, ehl::ui::TextRole::secondary);
    addAndMakeVisible (*warningLabel);

    labels.reserve (parameters.size());
    sliders.reserve (parameters.size());
    valueLabels.reserve (parameters.size());

    for (const auto& parameter : parameters)
    {
        auto label = std::make_unique<yup::Label>();
        label->setText (parameter->getName(), yup::dontSendNotification);
        label->setJustification (yup::Justification::center);
        ehl::ui::styleLabel (*label, ehl::ui::TextRole::secondary);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        auto slider = std::make_unique<ehl::ui::PixelSlider> (yup::Slider::RotaryVerticalDrag);
        slider->setRange (parameter->getMinimumValue(),
                          parameter->getMaximumValue(),
                          parameter->isStepped() ? 1.0 : 0.0);
        slider->setDefaultValue (parameter->getDefaultValue());
        slider->setValue (parameter->getValue(), yup::dontSendNotification);
        slider->setTextBoxStyle (yup::Slider::NoTextBox);
        slider->setPopupDisplayEnabled (false);
        slider->setMouseCursor (yup::MouseCursor::Hand);
        slider->setClickingGrabFocus (false);
        slider->onDragStart = [this, parameter] (const yup::MouseEvent&)
        {
            takeKeyboardFocus();
            parameter->beginChangeGesture();
        };
        slider->onValueChanged = [parameter] (double value)
        {
            parameter->setValueNotifyingHost (static_cast<float> (value));
        };
        slider->onDragEnd = [this, parameter] (const yup::MouseEvent&)
        {
            takeKeyboardFocus();
            parameter->endChangeGesture();
        };
        addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));

        auto valueLabel = std::make_unique<yup::Label>();
        valueLabel->setText (parameter->toString(), yup::dontSendNotification);
        valueLabel->setJustification (yup::Justification::center);
        ehl::ui::styleLabel (*valueLabel, ehl::ui::TextRole::primary);
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
            auditionButton->onHeldChanged = [this] (bool shouldBeOn)
            {
                auditionHeld = shouldBeOn;
                publishAudition();
            };
            addAndMakeVisible (*auditionButton);

            auditionTypeButton = std::make_unique<CycleTypeButton>();
            auditionTypeButton->setMouseCursor (yup::MouseCursor::Hand);
            auditionTypeButton->setClickingGrabFocus (false);
            auditionTypeButton->onCycle = [this]
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
        ehl::ui::styleLabel (*meterLabel, ehl::ui::TextRole::secondary);
        addAndMakeVisible (*meterLabel);

        inputMeter = std::make_unique<ehl::ui::StripMeter> (ehl::ui::mid);
        outputMeter = std::make_unique<ehl::ui::StripMeter> (ehl::ui::paper);
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
    return ehl::ui::preferredSize;
}

void ParameterGridEditor::paint (yup::Graphics& graphics)
{
    ehl::ui::paintEditorBackground (graphics, getWidth(), getHeight());
}

void ParameterGridEditor::resized()
{
    constexpr int columns = 7;
    constexpr float margin = 16.0f;
    constexpr float top = 128.0f;
    constexpr float gap = 8.0f;
    constexpr float labelHeight = 24.0f;
    constexpr float valueHeight = 24.0f;
    constexpr float controlSize = 72.0f;

    const auto bounds = getLocalBounds();
    const auto cellWidth = (bounds.getWidth() - 2.0f * margin - gap * (columns - 1)) / columns;
    const auto rows = std::max (1, static_cast<int> ((sliders.size() + columns - 1) / columns));
    const auto availableHeight = bounds.getHeight() - top - margin;
    const auto cellHeight = (availableHeight - gap * (rows - 1)) / rows;

    titleLabel->setBounds (20.0f, 8.0f, bounds.getWidth() - 40.0f, 28.0f);
    warningLabel->setBounds (20.0f, 36.0f, bounds.getWidth() - 40.0f, 20.0f);

    constexpr float buttonWidth = 104.0f;
    constexpr float controlHeight = 28.0f;
    auto meterX = margin;
    if (auditionButton != nullptr && auditionTypeButton != nullptr)
    {
        auditionButton->setBounds (margin, 72.0f, buttonWidth, controlHeight);
        auditionTypeButton->setBounds (margin + buttonWidth + gap, 72.0f, 80.0f, controlHeight);
        meterX = margin + 2.0f * buttonWidth + 2.0f * gap;
    }

    if (meterLabel != nullptr && inputMeter != nullptr && outputMeter != nullptr)
    {
        const auto meterWidth = std::max (140.0f, bounds.getWidth() - meterX - margin);
        meterLabel->setBounds (meterX, 68.0f, 88.0f, 16.0f);
        inputMeter->setBounds (meterX + 88.0f, 76.0f, std::max (40.0f, meterWidth - 88.0f), 10.0f);
        outputMeter->setBounds (meterX + 88.0f, 92.0f, std::max (40.0f, meterWidth - 88.0f), 10.0f);
    }

    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + column * (cellWidth + gap);
        const auto y = top + row * (cellHeight + gap);
        const auto inset = rows > 1 ? 4.0f : 12.0f;
        const auto labelY = y + inset;
        const auto valueY = y + cellHeight - valueHeight - inset;
        const auto controlTop = labelY + labelHeight;
        const auto controlBottom = valueY;
        const auto fittedControlSize = std::min ({ controlSize,
                                                   cellWidth - 8.0f,
                                                   std::max (20.0f, controlBottom - controlTop) });
        const auto controlX = x + 0.5f * (cellWidth - fittedControlSize);
        const auto controlY = controlTop + 0.5f * (controlBottom - controlTop - fittedControlSize);

        labels[i]->setBounds (x + 2.0f, labelY, cellWidth - 4.0f, labelHeight);
        sliders[i]->setBounds (controlX, controlY, fittedControlSize, fittedControlSize);
        valueLabels[i]->setBounds (x + 2.0f, valueY, cellWidth - 4.0f, valueHeight);
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
        inputMeter->setLevel (displayedInputPeak);
        outputMeter->setLevel (displayedOutputPeak);
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
    {
        auditionButton->setButtonText (slagmirrorProcessor->isStandaloneAuditionEnabled() ? "Audition On" : "Audition");
        auditionButton->setSelected (slagmirrorProcessor->isStandaloneAuditionEnabled());
    }

    if (auditionTypeButton != nullptr)
    {
        auditionTypeButton->setButtonText (auditionTypeName (slagmirrorProcessor->getStandaloneAuditionType()));
        auditionTypeButton->setSelected (slagmirrorProcessor->getStandaloneAuditionType() != 0);
    }
}

} // namespace slagmirror::plugin
