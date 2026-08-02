#include "PluginEditor.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace dd
{
namespace
{
const auto lightPalette = juce::Colour (0xfff6f6f2);
const auto darkPalette = juce::Colour (0xff050505);

juce::Colour foregroundOf (const juce::Component& component)
{
    return component.findColour (
        GeometricLookAndFeel::foregroundColourId);
}

juce::Colour backgroundOf (const juce::Component& component)
{
    return component.findColour (
        GeometricLookAndFeel::backgroundColourId);
}

juce::Colour mutedOf (const juce::Component& component)
{
    return component.findColour (
        GeometricLookAndFeel::mutedColourId);
}

float scaleOf (const juce::Component& component) noexcept
{
    if (const auto* look = dynamic_cast<const GeometricLookAndFeel*> (
            &component.getLookAndFeel()))
        return look->getUiScale();
    return 1.0f;
}

juce::Font monoFont (float height, bool bold = false)
{
    juce::FontOptions options { juce::Font::getDefaultMonospacedFontName(), height,
                               bold ? juce::Font::bold : juce::Font::plain };
    return juce::Font { options };
}

bool differs (float first, float second) noexcept
{
    return std::bit_cast<std::uint32_t> (first)
        != std::bit_cast<std::uint32_t> (second);
}

juce::String spacedVerticalText (const juce::String& name)
{
    juce::String result;
    for (const auto character : name)
    {
        if (character == ' ')
        {
            result += "  ";
            continue;
        }
        if (result.isNotEmpty() && ! result.endsWith ("  "))
            result += " ";
        result += character;
    }
    return result;
}

class ModeMenuItem final : public juce::PopupMenu::CustomComponent
{
public:
    ModeMenuItem (int modeIndex,
                  double sampleRate,
                  bool isCurrent)
        : juce::PopupMenu::CustomComponent (true),
          mode (modeIndex),
          displayPosition (
              DistortionEngine::getDisplayPositionForMode (modeIndex)),
          current (isCurrent)
    {
        setName (
            juce::String (displayPosition + 1).paddedLeft ('0', 2)
            + " "
            + DistortionEngine::getModeNames()[
                static_cast<size_t> (mode)]);

        Parameters parameters;
        parameters.mode = mode;
        parameters.driveDb = 18.0f;
        parameters.character =
            DistortionEngine::isCharacterBipolar (mode)
                ? 0.35f
                : juce::jmax (
                    0.58f,
                    DistortionEngine::getDefaultCharacter (mode));
        parameters.stages = 1;
        parameters.autoGainMode = 0;
        DistortionEngine::makeVisualization (
            parameters, sampleRate, visualization);
    }

    void getIdealSize (int& idealWidth, int& idealHeight) override
    {
        const auto scale = scaleOf (*this);
        idealWidth = juce::roundToInt (238.0f * scale);
        idealHeight = juce::roundToInt (27.0f * scale);
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto highlighted = isItemHighlighted();
        const auto normalForeground = foregroundOf (*this);
        const auto normalBackground = backgroundOf (*this);
        const auto foreground =
            highlighted ? normalBackground : normalForeground;
        const auto background =
            highlighted ? normalForeground : normalBackground;
        graphics.fillAll (background);

        const auto scale = scaleOf (*this);
        auto bounds = getLocalBounds().toFloat().reduced (
            5.0f * scale, 2.0f * scale);
        if (current)
            graphics.fillRect (
                bounds.removeFromLeft (3.0f * scale));
        else
            bounds.removeFromLeft (3.0f * scale);
        bounds.removeFromLeft (4.0f * scale);

        auto number = bounds.removeFromLeft (29.0f * scale);
        auto icon = bounds.removeFromRight (55.0f * scale).reduced (
            2.0f * scale);
        auto name = bounds.reduced (3.0f * scale, 0.0f);

        graphics.setColour (foreground);
        graphics.setFont (monoFont (11.0f * scale, true));
        graphics.drawText (
            juce::String (displayPosition + 1).paddedLeft ('0', 2),
            number,
            juce::Justification::centredLeft);
        graphics.setFont (monoFont (9.5f * scale, true));
        graphics.drawFittedText (
            DistortionEngine::getModeNames()[
                static_cast<size_t> (mode)].toUpperCase(),
            name.toNearestInt(),
            juce::Justification::centredLeft,
            1,
            0.72f);

        graphics.drawRect (icon, 1.0f * scale);
        icon = icon.reduced (2.0f * scale);
        auto makePath = [&icon] (
            const std::array<
                float,
                DistortionEngine::Visualization::pointCount>& values)
        {
            juce::Path path;
            for (int point = 0;
                 point < DistortionEngine::Visualization::pointCount;
                 ++point)
            {
                const auto position = static_cast<float> (point)
                    / static_cast<float> (
                        DistortionEngine::Visualization::pointCount - 1);
                const auto value = values[static_cast<size_t> (point)];
                const auto x =
                    icon.getX() + position * icon.getWidth();
                const auto y = icon.getCentreY()
                    - juce::jlimit (-1.0f, 1.0f, value / 1.25f)
                        * icon.getHeight() * 0.46f;
                if (point == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }
            return path;
        };

        graphics.setColour (foreground.withAlpha (0.32f));
        graphics.strokePath (
            makePath (visualization.input),
            juce::PathStrokeType (0.7f * scale));
        graphics.setColour (foreground);
        graphics.strokePath (
            makePath (visualization.output),
            juce::PathStrokeType (
                1.1f * scale,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
    }

private:
    int mode = 0;
    int displayPosition = 0;
    bool current = false;
    DistortionEngine::Visualization visualization;
};
} // namespace

GeometricLookAndFeel::GeometricLookAndFeel()
{
    applyPalette();
}

void GeometricLookAndFeel::setInverted (bool shouldBeInverted)
{
    if (inverted == shouldBeInverted)
        return;
    inverted = shouldBeInverted;
    applyPalette();
}

void GeometricLookAndFeel::setUiScale (float newScale) noexcept
{
    uiScale = juce::jlimit (0.5f, 2.0f, newScale);
}

void GeometricLookAndFeel::applyPalette()
{
    const auto foreground = inverted ? darkPalette : lightPalette;
    const auto background = inverted ? lightPalette : darkPalette;
    const auto muted = foreground.interpolatedWith (background, 0.28f);

    setColour (foregroundColourId, foreground);
    setColour (backgroundColourId, background);
    setColour (mutedColourId, muted);
    setColour (juce::Label::textColourId, foreground);
    setColour (juce::Slider::textBoxTextColourId, foreground);
    setColour (juce::Slider::textBoxBackgroundColourId, background);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, background);
    setColour (juce::ComboBox::textColourId, foreground);
    setColour (juce::ComboBox::outlineColourId, foreground);
    setColour (juce::ComboBox::arrowColourId, foreground);
    setColour (juce::PopupMenu::backgroundColourId, background);
    setColour (juce::PopupMenu::textColourId, foreground);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, foreground);
    setColour (juce::PopupMenu::highlightedTextColourId, background);
}

void GeometricLookAndFeel::drawRotarySlider (
    juce::Graphics& graphics,
    int x,
    int y,
    int width,
    int height,
    float sliderPosition,
    float,
    float,
    juce::Slider&)
{
    const auto scale = uiScale;
    auto bounds = juce::Rectangle<float> (
        static_cast<float> (x),
        static_cast<float> (y),
        static_cast<float> (width),
        static_cast<float> (height)).reduced (8.0f * scale);

    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto square = bounds.withSizeKeepingCentre (side, side);
    const auto foreground = findColour (foregroundColourId);
    const auto background = findColour (backgroundColourId);
    graphics.setColour (background);
    graphics.fillRect (square);
    graphics.setColour (foreground);
    graphics.drawRect (square, 2.0f * scale);

    auto inner = square.reduced (9.0f * scale);
    graphics.setColour (
        background.interpolatedWith (foreground, 0.12f));
    graphics.fillRect (inner);

    const auto progress = juce::jlimit (0.0f, 1.0f, sliderPosition);
    auto progressArea = inner.reduced (5.0f * scale);
    const auto filledHeight = progressArea.getHeight() * progress;
    graphics.setColour (foreground);
    graphics.fillRect (progressArea.withTop (
        progressArea.getBottom() - filledHeight));

    const auto grid = inner.reduced (3.0f * scale);
    graphics.setColour (background.withAlpha (0.22f));
    for (int i = 1; i < 4; ++i)
    {
        const auto px = grid.getX() + grid.getWidth() * static_cast<float> (i) / 4.0f;
        const auto py = grid.getY() + grid.getHeight() * static_cast<float> (i) / 4.0f;
        graphics.drawVerticalLine (
            juce::roundToInt (px), grid.getY(), grid.getBottom());
        graphics.drawHorizontalLine (
            juce::roundToInt (py), grid.getX(), grid.getRight());
    }

    const auto markerSize = juce::jmax (
        6.0f * scale, side * 0.08f);
    const auto markerX = inner.getX()
        + progress * juce::jmax (0.0f, inner.getWidth() - markerSize);
    graphics.setColour (
        background.interpolatedWith (foreground, 0.72f));
    graphics.fillRect (
        markerX,
        inner.getY() + 3.0f * scale,
        markerSize,
        markerSize);
}

void GeometricLookAndFeel::drawComboBox (
    juce::Graphics& graphics,
    int width,
    int height,
    bool,
    int,
    int,
    int,
    int,
    juce::ComboBox&)
{
    const auto scale = uiScale;
    auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (
        juce::jmax (1, juce::roundToInt (scale)));
    const auto foreground = findColour (foregroundColourId);
    const auto background = findColour (backgroundColourId);
    graphics.setColour (background);
    graphics.fillRect (bounds);
    graphics.setColour (foreground);
    graphics.drawRect (
        bounds, juce::jmax (1, juce::roundToInt (2.0f * scale)));

    const auto marker = juce::jmax (
        juce::roundToInt (8.0f * scale), height / 5);
    graphics.fillRect (
        width - marker - juce::roundToInt (10.0f * scale),
        (height - marker) / 2,
        marker,
        marker);
}

void GeometricLookAndFeel::positionComboBoxText (
    juce::ComboBox& box,
    juce::Label& label)
{
    const auto left = juce::roundToInt (10.0f * uiScale);
    const auto top = juce::jmax (1, juce::roundToInt (uiScale));
    label.setBounds (
        left,
        top,
        box.getWidth() - juce::roundToInt (42.0f * uiScale),
        box.getHeight() - 2 * top);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font GeometricLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return monoFont (15.0f * uiScale, true);
}

juce::Font GeometricLookAndFeel::getLabelFont (juce::Label&)
{
    return monoFont (13.0f * uiScale);
}

juce::Font GeometricLookAndFeel::getPopupMenuFont()
{
    return monoFont (12.0f * uiScale);
}

void GeometricLookAndFeel::drawButtonBackground (
    juce::Graphics& graphics,
    juce::Button& button,
    const juce::Colour&,
    bool isHighlighted,
    bool isDown)
{
    const auto active = button.getToggleState();
    const auto foreground = findColour (foregroundColourId);
    const auto background = findColour (backgroundColourId);
    graphics.setColour (active || isDown ? foreground : background);
    graphics.fillRect (button.getLocalBounds());
    graphics.setColour (active || isDown ? background : foreground);
    graphics.drawRect (
        button.getLocalBounds(),
        juce::jmax (
            1,
            juce::roundToInt (
                static_cast<float> (isHighlighted ? 3 : 2)
                * uiScale)));
}

void GeometricLookAndFeel::drawButtonText (
    juce::Graphics& graphics,
    juce::TextButton& button,
    bool,
    bool isDown)
{
    graphics.setColour (
        button.getToggleState() || isDown
            ? findColour (backgroundColourId)
            : findColour (foregroundColourId));
    graphics.setFont (monoFont (16.0f * uiScale, true));
    graphics.drawFittedText (
        button.getButtonText(),
        button.getLocalBounds().reduced (
            juce::roundToInt (10.0f * uiScale),
            juce::roundToInt (2.0f * uiScale)),
        juce::Justification::centred,
        1);
}

BrandButton::BrandButton()
    : juce::TextButton ("default_distortion")
{
    setWantsKeyboardFocus (false);
}

void BrandButton::paintButton (
    juce::Graphics& graphics,
    bool isHighlighted,
    bool isDown)
{
    auto colour = foregroundOf (*this);
    if (isDown)
        colour = mutedOf (*this);
    else if (isHighlighted)
        colour = colour.brighter (0.08f);
    graphics.setColour (colour);
    const auto scale = scaleOf (*this);
    graphics.setFont (monoFont (16.0f * scale, true));
    graphics.drawFittedText (
        getButtonText(),
        getLocalBounds().reduced (
            juce::roundToInt (10.0f * scale),
            juce::roundToInt (2.0f * scale)),
        juce::Justification::centred,
        1);
}

ParameterControl::ParameterControl (juce::String title)
{
    titleLabel.setText (std::move (title), juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    addAndMakeVisible (slider);
    setUiScale (1.0f);
}

void ParameterControl::setTitle (const juce::String& title)
{
    titleLabel.setText (title, juce::dontSendNotification);
}

void ParameterControl::setUiScale (float newScale)
{
    uiScale = juce::jlimit (0.5f, 2.0f, newScale);
    titleLabel.setFont (monoFont (13.0f * uiScale, true));
    slider.setTextBoxStyle (
        juce::Slider::TextBoxBelow,
        false,
        juce::roundToInt (86.0f * uiScale),
        juce::roundToInt (19.0f * uiScale));
    resized();
    repaint();
}

void ParameterControl::resized()
{
    auto bounds = getLocalBounds().reduced (
        juce::roundToInt (3.0f * uiScale));
    titleLabel.setBounds (bounds.removeFromTop (
        juce::roundToInt (18.0f * uiScale)));
    slider.setBounds (bounds);
}

void ParameterControl::paint (juce::Graphics& graphics)
{
    graphics.fillAll (backgroundOf (*this));
}

TriangleButton::TriangleButton (bool pointsRight)
    : juce::Button (pointsRight ? "Next algorithm" : "Previous algorithm"),
      right (pointsRight)
{
    setWantsKeyboardFocus (false);
}

void TriangleButton::paintButton (
    juce::Graphics& graphics,
    bool isHighlighted,
    bool isDown)
{
    const auto scale = scaleOf (*this);
    auto bounds = getLocalBounds().toFloat().reduced (
        8.0f * scale, 10.0f * scale);
    juce::Path triangle;
    if (right)
    {
        triangle.startNewSubPath (bounds.getX(), bounds.getY());
        triangle.lineTo (bounds.getRight(), bounds.getCentreY());
        triangle.lineTo (bounds.getX(), bounds.getBottom());
    }
    else
    {
        triangle.startNewSubPath (bounds.getRight(), bounds.getY());
        triangle.lineTo (bounds.getX(), bounds.getCentreY());
        triangle.lineTo (bounds.getRight(), bounds.getBottom());
    }
    triangle.closeSubPath();
    const auto foreground = foregroundOf (*this);
    graphics.setColour (
        isDown
            ? mutedOf (*this)
            : (isHighlighted ? foreground.brighter (0.08f) : foreground));
    graphics.fillPath (triangle);
}

SmartGainButton::SmartGainButton()
    : juce::TextButton ("AUTO GAIN")
{
}

VerticalTextButton::VerticalTextButton()
    : juce::TextButton ("S T E R E O")
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);
}

void VerticalTextButton::paintButton (
    juce::Graphics& graphics,
    bool isHighlighted,
    bool isDown)
{
    const auto active = getToggleState() || isDown;
    const auto scale = scaleOf (*this);
    const auto connectorWidth = juce::jmax (3.0f, 7.0f * scale);
    auto body = getLocalBounds().toFloat();
    body.removeFromLeft (connectorWidth);
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    const auto border = juce::jmax (1.0f, 2.0f * scale);

    graphics.setColour (foreground);
    graphics.fillRect (0.0f,
                       body.getCentreY() - 1.5f * scale,
                       body.getX() + border,
                       3.0f * scale);
    graphics.setColour (active ? foreground : background);
    graphics.fillRect (body);
    graphics.setColour (foreground);
    graphics.fillRect (body.removeFromTop (border));
    graphics.fillRect (body.removeFromBottom (border));
    graphics.fillRect (body.removeFromLeft (border));
    graphics.fillRect (body.removeFromRight (border));

    graphics.setColour (active ? background : foreground);
    if (isHighlighted && ! active)
        graphics.setColour (foreground.brighter (0.08f));
    graphics.setFont (monoFont (10.0f * scale, true));

    juce::Graphics::ScopedSaveState saved (graphics);
    graphics.addTransform (
        juce::AffineTransform::rotation (
            -juce::MathConstants<float>::halfPi,
            body.getCentreX(), body.getCentreY()));
    const auto rotatedBounds = juce::Rectangle<float> (
        body.getCentreX() - body.getHeight() * 0.5f,
        body.getCentreY() - body.getWidth() * 0.5f,
        body.getHeight(), body.getWidth());
    graphics.drawFittedText (
        "S T E R E O",
        rotatedBounds.reduced (
            5.0f * scale, 2.0f * scale).toNearestInt(),
        juce::Justification::centred,
        1);
}

VerticalTextSlider::VerticalTextSlider (
    juce::String parameterName,
    juce::String verticalText,
    double defaultValue)
    : text (std::move (verticalText))
{
    setSliderStyle (juce::Slider::LinearVertical);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRange (0.0, 1.0, 0.001);
    setMouseDragSensitivity (140);
    setDescriptor (
        std::move (parameterName), std::move (verticalText), defaultValue);
}

void VerticalTextSlider::setDescriptor (
    juce::String parameterName,
    juce::String verticalText,
    double defaultValue)
{
    setName (parameterName);
    setTitle (parameterName);
    text = std::move (verticalText);
    setDoubleClickReturnValue (true, defaultValue);
    repaint();
}

void VerticalTextSlider::paint (juce::Graphics& graphics)
{
    const auto scale = scaleOf (*this);
    const auto connectorWidth = juce::jmax (3.0f, 7.0f * scale);
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromLeft (connectorWidth);
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    const auto border = juce::jmax (1.0f, 2.0f * scale);

    graphics.setColour (foreground);
    graphics.fillRect (0.0f,
                       bounds.getCentreY() - 1.5f * scale,
                       bounds.getX() + border,
                       3.0f * scale);
    graphics.setColour (background);
    graphics.fillRect (bounds);
    graphics.setColour (foreground);
    auto framed = bounds;
    graphics.fillRect (framed.removeFromTop (border));
    graphics.fillRect (framed.removeFromBottom (border));
    graphics.fillRect (framed.removeFromLeft (border));
    graphics.fillRect (framed.removeFromRight (border));

    const auto progress = static_cast<float> (
        getNormalisableRange().convertTo0to1 (getValue()));
    const auto interior = bounds.reduced (border);
    const auto fill = interior.withTop (
        interior.getBottom() - progress * interior.getHeight());
    graphics.setColour (foreground);
    graphics.fillRect (fill);

    graphics.setFont (monoFont (10.0f * scale, true));
    const auto drawText = [&] (juce::Colour colour,
                               juce::Rectangle<int> clip)
    {
        juce::Graphics::ScopedSaveState clipped (graphics);
        graphics.reduceClipRegion (clip);
        graphics.setColour (colour);
        juce::Graphics::ScopedSaveState rotated (graphics);
        graphics.addTransform (
            juce::AffineTransform::rotation (
                -juce::MathConstants<float>::halfPi,
                bounds.getCentreX(), bounds.getCentreY()));
        const auto rotatedBounds = juce::Rectangle<float> (
            bounds.getCentreX() - bounds.getHeight() * 0.5f,
            bounds.getCentreY() - bounds.getWidth() * 0.5f,
            bounds.getHeight(), bounds.getWidth());
        graphics.drawFittedText (
            text,
            rotatedBounds.reduced (
                5.0f * scale, 2.0f * scale).toNearestInt(),
            juce::Justification::centred,
            1);
    };
    drawText (foreground, interior.toNearestInt());
    drawText (background, fill.toNearestInt());
}

void SmartGainButton::setLoadingState (float progress, bool isLoading)
{
    loadingProgress = juce::jlimit (0.0f, 1.0f, progress);
    loading = isLoading;
    repaint();
}

void SmartGainButton::paintButton (
    juce::Graphics& graphics,
    bool isHighlighted,
    bool isDown)
{
    getLookAndFeel().drawButtonBackground (
        graphics, *this, findColour (buttonColourId),
        isHighlighted, isDown);
    getLookAndFeel().drawButtonText (
        graphics, *this, isHighlighted, isDown);

    if (! loading)
        return;

    const auto scale = scaleOf (*this);
    auto track = getLocalBounds().toFloat().reduced (5.0f * scale);
    track = track.removeFromBottom (5.0f * scale);
    const auto background = backgroundOf (*this);
    graphics.setColour (background.withAlpha (0.22f));
    graphics.fillRect (track);
    graphics.setColour (background);
    graphics.fillRect (track.withWidth (
        track.getWidth() * loadingProgress));

    const auto scan = static_cast<float> (std::fmod (
        juce::Time::getMillisecondCounterHiRes() * 0.0014, 1.0));
    const auto scannerWidth = juce::jmax (
        3.0f * scale, track.getWidth() * 0.045f);
    graphics.fillRect (
        track.getX() + scan * juce::jmax (
            0.0f, track.getWidth() - scannerWidth),
        track.getY(),
        scannerWidth,
        track.getHeight());
}

ResponseDisplay::ResponseDisplay (DefaultDistortionAudioProcessor& owner)
    : processor (owner)
{
    startTimerHz (30);
}

ResponseDisplay::~ResponseDisplay()
{
    stopTimer();
}

void TrimSlider::paint (juce::Graphics& graphics)
{
    const auto scale = scaleOf (*this);
    auto track = getLocalBounds().toFloat();
    track.removeFromRight (
        static_cast<float> (getTextBoxWidth()) + 7.0f * scale);
    track = track.reduced (7.0f * scale, 0.0f);

    const auto toolbarColour = foregroundOf (*this);
    const auto darkToolbar = toolbarColour.getPerceivedBrightness() < 0.5f;
    const auto trackColour = darkToolbar
        ? juce::Colour (0xffaaaaaa)
        : juce::Colour (0xff777777);
    const auto centreY = track.getCentreY();
    graphics.setColour (trackColour);
    graphics.fillRect (juce::Rectangle<float> {
        track.getX(), centreY - 1.0f * scale,
        track.getWidth(), 2.0f * scale });

    const auto range = getMaximum() - getMinimum();
    const auto amount = range > 0.0
        ? static_cast<float> ((getValue() - getMinimum()) / range)
        : 0.5f;
    const auto markerSize = 9.0f * scale;
    const auto markerX = track.getX()
        + juce::jlimit (0.0f, 1.0f, amount) * track.getWidth();
    graphics.setColour (trackColour.brighter (darkToolbar ? 0.08f : 0.0f));
    graphics.fillRect (juce::Rectangle<float> {
        markerX - 0.5f * markerSize,
        centreY - 0.5f * markerSize,
        markerSize,
        markerSize });
}

void TrimSlider::lookAndFeelChanged()
{
    juce::Slider::lookAndFeelChanged();
    setColour (juce::Slider::textBoxTextColourId, backgroundOf (*this));
    setColour (
        juce::Slider::textBoxBackgroundColourId,
        juce::Colours::transparentBlack);
    setColour (
        juce::Slider::textBoxOutlineColourId,
        juce::Colours::transparentBlack);
}

MultibandPanel::MultibandPanel (DefaultDistortionAudioProcessor& owner)
    : processor (owner)
{
    inputSpectrum.fill (-60.0f);
    outputSpectrum.fill (-60.0f);
    for (auto* button : {
             &linkButton, &bandCountButton, &phaseButton,
             &soloButton, &bypassButton })
        addAndMakeVisible (*button);

    trimSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    trimSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 24);
    trimSlider.setRange (-12.0, 12.0, 0.01);
    trimSlider.setDoubleClickReturnValue (true, 0.0);
    trimSlider.textFromValueFunction = [] (double value)
    {
        const auto clean = std::abs (value) < 0.005 ? 0.0 : value;
        return juce::String (clean, 1) + " dB";
    };
    addAndMakeVisible (trimSlider);

    linkButton.onClick = [this]
    {
        processor.setMultibandLinkedFromUi (
            ! processor.getCurrentMultibandParameters().linked);
    };
    bandCountButton.onClick = [this]
    {
        showBandCountMenu();
    };
    phaseButton.onClick = [this]
    {
        const auto phase = processor.getCurrentMultibandParameters().phaseMode;
        setParameter (ParamIDs::multibandPhase, phase == 0 ? 1.0f : 0.0f);
    };
    soloButton.onClick = [this]
    {
        const auto selected = processor.getSelectedBand();
        processor.setSoloBand (
            processor.getSoloBand() == selected ? -1 : selected);
    };
    bypassButton.onClick = [this]
    {
        const auto selected = processor.getSelectedBand();
        const auto multiband = processor.getCurrentMultibandParameters();
        setParameter (
            ParamIDs::band (selected, "Bypass"),
            multiband.bands[static_cast<size_t> (selected)].bypass ? 0.0f : 1.0f);
    };
    trimSlider.onValueChange = [this]
    {
        setParameter (
            ParamIDs::band (processor.getSelectedBand(), "Trim"),
            static_cast<float> (trimSlider.getValue()));
    };

    setMouseCursor (juce::MouseCursor::NormalCursor);
    startTimerHz (30);
}

MultibandPanel::~MultibandPanel()
{
    stopTimer();
    processor.setSoloBand (-1);
}

void MultibandPanel::setParameter (const juce::String& id, float plainValue)
{
    if (auto* parameter = processor.parameters.getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (plainValue));
        parameter->endChangeGesture();
    }
}

juce::Rectangle<float> MultibandPanel::analyzerBounds() const
{
    auto bounds = getLocalBounds().toFloat().reduced (10.0f * scaleOf (*this));
    bounds.removeFromBottom (42.0f * scaleOf (*this));
    return bounds;
}

float MultibandPanel::frequencyToX (float frequency) const
{
    const auto bounds = analyzerBounds();
    const auto maximum = static_cast<float> (
        juce::jmin (20000.0, 0.45 * juce::jmax (1.0, processor.getSampleRate())));
    const auto normalised = std::log (juce::jlimit (20.0f, maximum, frequency) / 20.0f)
        / std::log (maximum / 20.0f);
    return bounds.getX() + normalised * bounds.getWidth();
}

float MultibandPanel::xToFrequency (float x) const
{
    const auto bounds = analyzerBounds();
    const auto maximum = static_cast<float> (
        juce::jmin (20000.0, 0.45 * juce::jmax (1.0, processor.getSampleRate())));
    const auto normalised = juce::jlimit (
        0.0f, 1.0f, (x - bounds.getX()) / bounds.getWidth());
    return 20.0f * std::pow (maximum / 20.0f, normalised);
}

int MultibandPanel::crossoverAt (juce::Point<float> position,
                                 bool badgeOnly) const
{
    const auto parameters = processor.getCurrentMultibandParameters();
    const auto bounds = analyzerBounds();
    for (int crossover = 0; crossover < parameters.bandCount - 1; ++crossover)
    {
        const auto x = frequencyToX (
            parameters.crossoverHz[static_cast<size_t> (crossover)]);
        const auto hit = badgeOnly
            ? juce::Rectangle<float> {
                x - 38.0f, bounds.getBottom() - 32.0f, 76.0f, 23.0f }
            : juce::Rectangle<float> {
                x - 8.0f, bounds.getY(), 16.0f, bounds.getHeight() };
        if (hit.contains (position))
            return crossover;
    }
    return -1;
}

int MultibandPanel::bandAt (float x) const
{
    const auto parameters = processor.getCurrentMultibandParameters();
    for (int crossover = 0; crossover < parameters.bandCount - 1; ++crossover)
        if (x < frequencyToX (
                parameters.crossoverHz[static_cast<size_t> (crossover)]))
            return crossover;
    return parameters.bandCount - 1;
}

void MultibandPanel::showBandCountMenu()
{
    juce::PopupMenu menu;
    const auto selected = processor.getCurrentMultibandParameters().bandCount;
    for (int count = 2; count <= 4; ++count)
        menu.addItem (
            count,
            juce::String (count) + " BANDS",
            true,
            count == selected);
    const auto safeThis = juce::Component::SafePointer<MultibandPanel> (this);
    menu.showMenuAsync (
        juce::PopupMenu::Options {}.withTargetComponent (&bandCountButton),
        [safeThis] (int result)
        {
            if (safeThis != nullptr && result >= 2 && result <= 4)
                safeThis->setParameter (
                    ParamIDs::multibandBandCount,
                    static_cast<float> (result - 2));
        });
}

void MultibandPanel::showSlopeMenu (int crossover)
{
    juce::PopupMenu menu;
    constexpr std::array<int, 5> slopes { 6, 12, 24, 36, 48 };
    const auto selected = processor.getCurrentMultibandParameters()
        .crossoverSlope[static_cast<size_t> (crossover)];
    for (int index = 0; index < static_cast<int> (slopes.size()); ++index)
        menu.addItem (
            index + 1,
            juce::String (slopes[static_cast<size_t> (index)]) + " dB/oct",
            true,
            index == selected);
    const auto safeThis = juce::Component::SafePointer<MultibandPanel> (this);
    menu.showMenuAsync (
        juce::PopupMenu::Options {}
            .withTargetScreenArea (
                localAreaToGlobal (
                    juce::Rectangle<int> (
                        juce::roundToInt (frequencyToX (
                            processor.getCurrentMultibandParameters()
                                .crossoverHz[static_cast<size_t> (crossover)])) - 38,
                        juce::roundToInt (analyzerBounds().getBottom() - 32),
                        76, 23))),
        [safeThis, crossover] (int result)
        {
            if (safeThis != nullptr && result > 0)
                safeThis->setParameter (
                    ParamIDs::crossoverSlope (crossover),
                    static_cast<float> (result - 1));
        });
}

void MultibandPanel::timerCallback()
{
    const auto received = processor.pullAnalyzerSamples (
        incomingInput.data(), incomingOutput.data(),
        static_cast<int> (incomingInput.size()));
    for (int sample = 0; sample < received; ++sample)
    {
        inputHistory[static_cast<size_t> (historyPosition)] =
            incomingInput[static_cast<size_t> (sample)];
        outputHistory[static_cast<size_t> (historyPosition)] =
            incomingOutput[static_cast<size_t> (sample)];
        historyPosition = (historyPosition + 1) % fftSize;
    }
    if (received > 0)
        updateSpectrum();
    updateControls();
    repaint();
}

void MultibandPanel::updateSpectrum()
{
    inputFft.fill (0.0f);
    outputFft.fill (0.0f);
    for (int sample = 0; sample < fftSize; ++sample)
    {
        const auto source = (historyPosition + sample) % fftSize;
        inputFft[static_cast<size_t> (sample)] =
            inputHistory[static_cast<size_t> (source)];
        outputFft[static_cast<size_t> (sample)] =
            outputHistory[static_cast<size_t> (source)];
    }
    window.multiplyWithWindowingTable (inputFft.data(), fftSize);
    window.multiplyWithWindowingTable (outputFft.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform (inputFft.data());
    fft.performFrequencyOnlyForwardTransform (outputFft.data());
    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const auto normalise = 2.0f / static_cast<float> (fftSize);
        const auto inputDb = juce::Decibels::gainToDecibels (
            inputFft[static_cast<size_t> (bin)] * normalise, -60.0f);
        const auto outputDb = juce::Decibels::gainToDecibels (
            outputFft[static_cast<size_t> (bin)] * normalise, -60.0f);
        auto smooth = [] (float previous, float next)
        {
            const auto amount = next > previous ? 0.58f : 0.12f;
            return previous + amount * (next - previous);
        };
        inputSpectrum[static_cast<size_t> (bin)] = smooth (
            inputSpectrum[static_cast<size_t> (bin)], inputDb);
        outputSpectrum[static_cast<size_t> (bin)] = smooth (
            outputSpectrum[static_cast<size_t> (bin)], outputDb);
    }
}

void MultibandPanel::updateControls()
{
    const auto parameters = processor.getCurrentMultibandParameters();
    const auto selected = juce::jlimit (
        0, parameters.bandCount - 1, processor.getSelectedBand());
    if (selected != processor.getSelectedBand())
        processor.setSelectedBand (selected);
    linkButton.setToggleState (parameters.linked, juce::dontSendNotification);
    bandCountButton.setButtonText (
        juce::String (parameters.bandCount) + " BANDS");
    phaseButton.setButtonText (
        parameters.phaseMode == 0 ? "MIN PHASE" : "LINEAR PHASE");
    phaseButton.setToggleState (
        parameters.phaseMode == 1, juce::dontSendNotification);
    soloButton.setToggleState (
        processor.getSoloBand() == selected, juce::dontSendNotification);
    bypassButton.setToggleState (
        parameters.bands[static_cast<size_t> (selected)].bypass,
        juce::dontSendNotification);
    const auto trim = parameters.bands[static_cast<size_t> (selected)].trimDb;
    if (! trimSlider.isMouseButtonDown())
        trimSlider.setValue (trim, juce::dontSendNotification);
}

void MultibandPanel::paint (juce::Graphics& graphics)
{
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    const auto muted = mutedOf (*this);
    graphics.fillAll (foreground);
    const auto bounds = analyzerBounds();
    graphics.setColour (background);
    graphics.fillRect (bounds);
    graphics.setColour (foreground);
    graphics.drawRect (bounds, 2.0f * scaleOf (*this));

    const auto parameters = processor.getCurrentMultibandParameters();
    const auto selected = processor.getSelectedBand();
    auto left = bounds.getX();
    for (int band = 0; band < parameters.bandCount; ++band)
    {
        const auto right = band < parameters.bandCount - 1
            ? frequencyToX (parameters.crossoverHz[static_cast<size_t> (band)])
            : bounds.getRight();
        if (band == selected)
        {
            graphics.setColour (foreground.withAlpha (0.22f));
            graphics.fillRect (juce::Rectangle<float> {
                left, bounds.getY(), right - left, bounds.getHeight() });
            graphics.setColour (foreground.withAlpha (0.72f));
            graphics.fillRect (juce::Rectangle<float> {
                left, bounds.getY(), right - left, 3.0f * scaleOf (*this) });
        }
        graphics.setColour (foreground.withAlpha (0.55f));
        graphics.setFont (monoFont (11.0f * scaleOf (*this), true));
        graphics.drawText (
            "B" + juce::String (band + 1),
            juce::Rectangle<float> { left + 6.0f, bounds.getY() + 5.0f,
                                     30.0f, 16.0f },
            juce::Justification::centredLeft);
        left = right;
    }

    graphics.setColour (foreground.withAlpha (0.13f));
    for (const auto db : { -60, -48, -36, -24, -12, 0 })
    {
        const auto y = bounds.getY()
            + (static_cast<float> (-db) / 60.0f) * bounds.getHeight();
        graphics.drawHorizontalLine (
            juce::roundToInt (y), bounds.getX(), bounds.getRight());
    }
    for (const auto frequency : {
             20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
             1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const auto x = frequencyToX (frequency);
        graphics.drawVerticalLine (
            juce::roundToInt (x), bounds.getY(), bounds.getBottom());
    }

    const auto makePath = [&] (const auto& spectrum)
    {
        juce::Path path;
        const auto rate = processor.getSampleRate() > 0.0
            ? processor.getSampleRate() : 48000.0;
        auto started = false;
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const auto frequency = static_cast<float> (bin * rate / fftSize);
            if (frequency < 20.0f || frequency > 20000.0f)
                continue;
            const auto x = frequencyToX (frequency);
            const auto db = juce::jlimit (
                -60.0f, 0.0f, spectrum[static_cast<size_t> (bin)]);
            const auto y = bounds.getY() + (-db / 60.0f) * bounds.getHeight();
            if (! started)
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
                path.lineTo (x, y);
        }
        return path;
    };
    graphics.setColour (muted.withAlpha (0.72f));
    graphics.strokePath (
        makePath (inputSpectrum),
        juce::PathStrokeType (1.25f * scaleOf (*this)));
    graphics.setColour (foreground);
    graphics.strokePath (
        makePath (outputSpectrum),
        juce::PathStrokeType (2.0f * scaleOf (*this)));

    constexpr std::array<int, 5> slopes { 6, 12, 24, 36, 48 };
    for (int crossover = 0; crossover < parameters.bandCount - 1; ++crossover)
    {
        const auto x = frequencyToX (
            parameters.crossoverHz[static_cast<size_t> (crossover)]);
        graphics.setColour (foreground);
        graphics.fillRect (x - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
        if (hoveredCrossover == crossover || draggedCrossover == crossover)
        {
            const auto frequencyBadge = juce::Rectangle<float> {
                x - 38.0f, bounds.getY() + 9.0f, 76.0f, 23.0f };
            const auto slopeBadge = juce::Rectangle<float> {
                x - 38.0f, bounds.getBottom() - 32.0f, 76.0f, 23.0f };
            graphics.setColour (foreground);
            graphics.fillRect (frequencyBadge);
            graphics.fillRect (slopeBadge);
            graphics.setColour (background);
            graphics.setFont (monoFont (10.0f * scaleOf (*this), true));
            const auto frequency = parameters.crossoverHz[
                static_cast<size_t> (crossover)];
            graphics.drawText (
                frequency >= 1000.0f
                    ? juce::String (frequency / 1000.0f, 2) + " kHz"
                    : juce::String (juce::roundToInt (frequency)) + " Hz",
                frequencyBadge,
                juce::Justification::centred);
            graphics.drawText (
                juce::String (slopes[static_cast<size_t> (
                    parameters.crossoverSlope[static_cast<size_t> (crossover)])])
                    + " dB/oct",
                slopeBadge,
                juce::Justification::centred);
        }
    }
}

void MultibandPanel::resized()
{
    auto toolbar = getLocalBounds().reduced (
        juce::roundToInt (10.0f * scaleOf (*this)));
    toolbar = toolbar.removeFromBottom (
        juce::roundToInt (34.0f * scaleOf (*this)));
    const auto gap = juce::roundToInt (5.0f * scaleOf (*this));
    linkButton.setBounds (toolbar.removeFromLeft (90).reduced (0, 1));
    toolbar.removeFromLeft (gap);
    bandCountButton.setBounds (toolbar.removeFromLeft (100).reduced (0, 1));
    toolbar.removeFromLeft (gap);
    phaseButton.setBounds (toolbar.removeFromLeft (120).reduced (0, 1));
    toolbar.removeFromLeft (gap);
    soloButton.setBounds (toolbar.removeFromLeft (75).reduced (0, 1));
    toolbar.removeFromLeft (gap);
    bypassButton.setBounds (toolbar.removeFromLeft (85).reduced (0, 1));
    toolbar.removeFromLeft (gap);
    trimSlider.setBounds (toolbar);
}

void MultibandPanel::mouseMove (const juce::MouseEvent& event)
{
    hoveredCrossover = crossoverAt (event.position, false);
    setMouseCursor (hoveredCrossover >= 0
        ? juce::MouseCursor::LeftRightResizeCursor
        : juce::MouseCursor::NormalCursor);
    repaint();
}

void MultibandPanel::mouseExit (const juce::MouseEvent&)
{
    if (draggedCrossover < 0)
        hoveredCrossover = -1;
    repaint();
}

void MultibandPanel::mouseDown (const juce::MouseEvent& event)
{
    const auto badge = crossoverAt (event.position, true);
    if (badge >= 0 && badge == hoveredCrossover)
    {
        showSlopeMenu (badge);
        return;
    }
    draggedCrossover = crossoverAt (event.position, false);
    if (draggedCrossover < 0 && analyzerBounds().contains (event.position))
        processor.setSelectedBand (bandAt (event.position.x));
}

void MultibandPanel::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedCrossover < 0)
        return;
    auto frequency = xToFrequency (event.position.x);
    const auto parameters = processor.getCurrentMultibandParameters();
    constexpr auto ratio = 1.2599210498948732f;
    const auto lower = draggedCrossover == 0
        ? 20.0f
        : parameters.crossoverHz[static_cast<size_t> (draggedCrossover - 1)]
            * ratio;
    const auto maximum = static_cast<float> (
        juce::jmin (20000.0, 0.45 * juce::jmax (1.0, processor.getSampleRate())));
    const auto upper = draggedCrossover >= parameters.bandCount - 2
        ? maximum
        : parameters.crossoverHz[static_cast<size_t> (draggedCrossover + 1)]
            / ratio;
    frequency = juce::jlimit (lower, upper, frequency);
    setParameter (ParamIDs::crossoverFrequency (draggedCrossover), frequency);
}

void MultibandPanel::mouseUp (const juce::MouseEvent&)
{
    draggedCrossover = -1;
}

void ResponseDisplay::timerCallback()
{
    repaint();
}

void ResponseDisplay::paint (juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat();
    const auto scale = scaleOf (*this);
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    graphics.setColour (foreground);
    graphics.fillRect (bounds);
    graphics.setColour (background);
    graphics.drawRect (bounds, 3.0f * scale);

    auto parameters = processor.getCurrentParameters();
    const auto multiband = processor.getCurrentMultibandParameters();
    if (multiband.enabled && ! multiband.linked)
        parameters = multiband.bands[static_cast<size_t> (
            juce::jlimit (0, multiband.bandCount - 1,
                          processor.getSelectedBand()))].saturation;
    const auto displaySampleRate =
        processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const auto visualizationChanged =
        ! visualizationValid
        || parameters.mode != visualizedParameters.mode
        || differs (parameters.driveDb, visualizedParameters.driveDb)
        || differs (parameters.character, visualizedParameters.character)
        || differs (parameters.secondary, visualizedParameters.secondary)
        || differs (parameters.asymmetry, visualizedParameters.asymmetry)
        || parameters.asymmetryStereo
            != visualizedParameters.asymmetryStereo
        || differs (parameters.tone, visualizedParameters.tone)
        || parameters.stages != visualizedParameters.stages
        || parameters.quality != visualizedParameters.quality
        || std::abs (displaySampleRate - visualizedSampleRate) > 0.5;
    if (visualizationChanged)
    {
        DistortionEngine::makeVisualization (
            parameters, displaySampleRate, visualization);
        visualizedParameters = parameters;
        visualizedSampleRate = displaySampleRate;
        visualizationValid = true;
    }

    auto graph = bounds.reduced (12.0f * scale);
    graph.removeFromBottom (31.0f * scale);
    graphics.setColour (background);
    graphics.fillRect (graph);
    graphics.setColour (foreground.withAlpha (0.2f));
    graphics.drawHorizontalLine (
        juce::roundToInt (graph.getCentreY()), graph.getX(), graph.getRight());
    graphics.drawVerticalLine (
        juce::roundToInt (graph.getCentreX()), graph.getY(), graph.getBottom());

    auto makePath = [&] (const std::array<float, DistortionEngine::Visualization::pointCount>& values)
    {
        juce::Path path;
        for (int point = 0;
             point < DistortionEngine::Visualization::pointCount;
             ++point)
        {
            const auto position = static_cast<float> (point)
                / static_cast<float> (
                    DistortionEngine::Visualization::pointCount - 1);
            const auto value = values[static_cast<size_t> (point)];
            const auto px = graph.getX() + position * graph.getWidth();
            const auto normalisedValue = visualization.spectralDomain
                ? 2.0f * value / 1.5f - 1.0f
                : value / 1.25f;
            const auto py = graph.getCentreY()
                - juce::jlimit (-1.0f, 1.0f, normalisedValue)
                    * graph.getHeight() * 0.46f;
            if (point == 0)
                path.startNewSubPath (px, py);
            else
                path.lineTo (px, py);
        }
        return path;
    };

    graphics.setColour (foreground.withAlpha (0.3f));
    graphics.strokePath (
        makePath (visualization.input),
        juce::PathStrokeType (1.0f * scale));
    graphics.setColour (foreground);
    graphics.strokePath (
        makePath (visualization.output),
        juce::PathStrokeType (
            2.0f * scale,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

    auto meters = bounds.reduced (12.0f * scale).removeFromBottom (
        21.0f * scale);
    const auto input = juce::jlimit (
        0.0f, 1.0f, processor.getInputPeak());
    const auto output = juce::jlimit (
        0.0f, 1.0f, processor.getOutputPeak());

    auto inputMeter = meters.removeFromTop (8.0f * scale);
    auto outputMeter = meters.removeFromBottom (8.0f * scale);
    graphics.setColour (background);
    graphics.drawRect (inputMeter);
    graphics.drawRect (outputMeter);
    graphics.fillRect (inputMeter.withWidth (inputMeter.getWidth() * input));
    graphics.fillRect (outputMeter.withWidth (outputMeter.getWidth() * output));
}

DefaultDistortionAudioProcessorEditor::DefaultDistortionAudioProcessorEditor (
    DefaultDistortionAudioProcessor& owner)
    : AudioProcessorEditor (&owner),
      ownerProcessor (owner),
      responseDisplay (owner),
      multibandPanel (owner)
{
    ownerProcessor.setAnalyzerEnabled (true);
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setResizable (true, true);
    const auto initiallyExpanded =
        ownerProcessor.getCurrentMultibandParameters().enabled;
    multibandVisible = initiallyExpanded;
    getConstrainer()->setFixedAspectRatio (
        860.0 / (initiallyExpanded ? 620.0 : 354.0));
    setResizeLimits (
        720, initiallyExpanded ? 519 : 296,
        1200, initiallyExpanded ? 865 : 494);
    setSize (860, initiallyExpanded ? 620 : 354);

    brandLabel.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    brandLabel.onClick = [this] { togglePalette(); };
    addAndMakeVisible (brandLabel);
    nextBrandGlitchTimeMs =
        juce::Time::getMillisecondCounterHiRes() + 4000.0;

    modeButton.onClick = [this] { showModeMenu(); };
    addAndMakeVisible (modeButton);
    previousModeButton.onClick = [this] { stepMode (-1); };
    nextModeButton.onClick = [this] { stepMode (1); };
    addAndMakeVisible (previousModeButton);
    addAndMakeVisible (nextModeButton);

    autoGainButton.setClickingTogglesState (false);
    autoGainButton.onClick = [this] { cycleAutoGain(); };
    addAndMakeVisible (autoGainButton);
    addAndMakeVisible (asymStereoButton);
    addAndMakeVisible (secondarySlider);

    for (auto* control : {
             &drive, &character, &asym, &tone,
             &stages, &mix, &output, &quality })
    {
        configureKnob (*control);
        addAndMakeVisible (*control);
    }
    addAndMakeVisible (responseDisplay);
    multibandButton.setClickingTogglesState (true);
    addAndMakeVisible (multibandButton);
    addAndMakeVisible (multibandPanel);
    multibandPanel.setVisible (initiallyExpanded);

    // ParameterControl paints an opaque background. Keep the linked vertical
    // controls above their neighbouring knobs so neither the connector nor
    // the left frame edge can be covered at larger editor scales.
    asymStereoButton.toFront (false);
    secondarySlider.toFront (false);

    drive.slider.setRange (0.0, 36.0, 0.01);
    asym.slider.setRange (-1.0, 1.0, 0.001);
    tone.slider.setRange (-1.0, 1.0, 0.001);
    stages.slider.setRange (1.0, 8.0, 1.0);
    mix.slider.setRange (0.0, 1.0, 0.001);
    output.slider.setRange (-24.0, 12.0, 0.01);
    quality.slider.setRange (0.0, 3.0, 1.0);

    drive.slider.textFromValueFunction = [] (double value)
    {
        const auto clean = std::abs (value) < 0.005 ? 0.0 : value;
        return juce::String (clean, 1) + " dB";
    };
    for (auto* slider : { &asym.slider, &tone.slider })
        slider->textFromValueFunction = [] (double value)
        {
            return juce::String (juce::roundToInt (value * 100.0)) + "%";
        };
    stages.slider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value)) + " stage";
    };
    mix.slider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value * 100.0)) + "%";
    };
    output.slider.textFromValueFunction = [] (double value)
    {
        const auto clean = std::abs (value) < 0.005 ? 0.0 : value;
        return juce::String (clean, 1) + " dB";
    };
    quality.slider.textFromValueFunction = [] (double value)
    {
        static const std::array<juce::String, 4> labels { "OFF", "2x", "4x", "8x" };
        return labels[static_cast<size_t> (
            juce::jlimit (0, 3, juce::roundToInt (value)))];
    };

    drive.slider.setDoubleClickReturnValue (true, 0.0);
    character.slider.setDoubleClickReturnValue (true, 0.0);
    asym.slider.setDoubleClickReturnValue (true, 0.0);
    tone.slider.setDoubleClickReturnValue (true, 0.0);
    stages.slider.setDoubleClickReturnValue (true, 1.0);
    mix.slider.setDoubleClickReturnValue (true, 1.0);
    output.slider.setDoubleClickReturnValue (true, 0.0);
    quality.slider.setDoubleClickReturnValue (true, 0.0);

    auto& state = ownerProcessor.parameters;
    outputAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::output, output.slider);
    qualityAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::quality, quality.slider);
    multibandAttachment = std::make_unique<ButtonAttachment> (
        state, ParamIDs::multibandEnabled, multibandButton);
    if (auto* parameter = state.getParameter (ParamIDs::autoGain))
    {
        autoGainAttachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float value)
            {
                updateAutoGainButton (juce::roundToInt (value));
            });
        autoGainAttachment->sendInitialUpdate();
    }
    character.slider.onDragStart = [this]
    {
        if (characterAttachment != nullptr)
            characterAttachment->beginGesture();
    };
    character.slider.onValueChange = [this]
    {
        if (! updatingCharacter && characterAttachment != nullptr)
            characterAttachment->setValueAsPartOfGesture (
                static_cast<float> (character.slider.getValue() / 100.0));
    };
    character.slider.onDragEnd = [this]
    {
        if (characterAttachment != nullptr)
            characterAttachment->endGesture();
    };
    rebindContextualControls();
    timerCallback();
    startTimerHz (12);
}

DefaultDistortionAudioProcessorEditor::~DefaultDistortionAudioProcessorEditor()
{
    stopTimer();
    ownerProcessor.setAnalyzerEnabled (false);
    setLookAndFeel (nullptr);
}

void DefaultDistortionAudioProcessorEditor::configureKnob (
    ParameterControl& control)
{
    control.slider.setMouseDragSensitivity (180);
}

void DefaultDistortionAudioProcessorEditor::rebindContextualControls()
{
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    const auto targetBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    if (targetBand == boundBand)
        return;
    boundBand = targetBand;

    driveAttachment.reset();
    secondaryAttachment.reset();
    asymAttachment.reset();
    asymStereoAttachment.reset();
    toneAttachment.reset();
    stagesAttachment.reset();
    mixAttachment.reset();
    modeAttachment.reset();
    characterAttachment.reset();

    const auto id = [targetBand] (const char* master, const char* bandSuffix)
    {
        return targetBand < 0
            ? juce::String (master)
            : ParamIDs::band (targetBand, bandSuffix);
    };
    auto& state = ownerProcessor.parameters;
    driveAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::drive, "Drive"), drive.slider);
    secondaryAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::secondary, "Secondary"), secondarySlider);
    asymAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::asym, "Asym"), asym.slider);
    asymStereoAttachment = std::make_unique<ButtonAttachment> (
        state, id (ParamIDs::asymStereo, "AsymStereo"), asymStereoButton);
    toneAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::tone, "Tone"), tone.slider);
    stagesAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::stages, "Stages"), stages.slider);
    mixAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::mix, "Mix"), mix.slider);

    const auto modeId = id (ParamIDs::mode, "Mode");
    if (auto* parameter = state.getParameter (modeId))
    {
        modeAttachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float value)
            {
                const auto mode = juce::jlimit (
                    0,
                    DistortionEngine::modeCount - 1,
                    juce::roundToInt (value));
                updateCharacterControl (mode);
                const auto& name =
                    DistortionEngine::getModeNames()[static_cast<size_t> (mode)];
                const auto displayPosition =
                    DistortionEngine::getDisplayPositionForMode (mode);
                modeButton.setButtonText (
                    juce::String (displayPosition + 1).paddedLeft ('0', 2)
                    + "  " + name.toUpperCase());
                if (characterAttachment != nullptr)
                    characterAttachment->sendInitialUpdate();
            });
        modeAttachment->sendInitialUpdate();
    }

    const auto characterId = id (ParamIDs::character, "Character");
    if (auto* parameter = state.getParameter (characterId))
    {
        characterAttachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float rawValue)
            {
                const auto shownValue = DistortionEngine::isCharacterBipolar (
                    juce::jmax (0, displayedMode))
                    ? rawValue
                    : juce::jmax (0.0f, rawValue);
                juce::ScopedValueSetter<bool> guard (updatingCharacter, true);
                character.slider.setValue (
                    100.0 * shownValue, juce::dontSendNotification);
            });
        characterAttachment->sendInitialUpdate();
    }
}

void DefaultDistortionAudioProcessorEditor::updateMultibandVisibility (
    bool enabled,
    bool resizeEditor)
{
    if (enabled == multibandVisible && ! resizeEditor)
        return;
    multibandVisible = enabled;
    multibandPanel.setVisible (enabled);
    const auto targetHeight = enabled ? 620.0 : 354.0;
    if (resizeEditor)
    {
        const auto currentWidth = getWidth();
        const auto scale = static_cast<double> (currentWidth) / 860.0;
        setResizeLimits (1, 1, 10000, 10000);
        getConstrainer()->setFixedAspectRatio (860.0 / targetHeight);
        setSize (
            currentWidth,
            juce::roundToInt (targetHeight * scale));
    }
    else
        getConstrainer()->setFixedAspectRatio (860.0 / targetHeight);
    setResizeLimits (
        720, enabled ? 519 : 296,
        1200, enabled ? 865 : 494);
    resized();
    repaint();
}

void DefaultDistortionAudioProcessorEditor::showModeMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    const auto contextBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    const auto currentMode = contextBand < 0
        ? ownerProcessor.getCurrentParameters().mode
        : multiband.bands[static_cast<size_t> (contextBand)].saturation.mode;
    const auto& names = DistortionEngine::getModeNames();
    const auto displaySampleRate =
        ownerProcessor.getSampleRate() > 0.0
            ? ownerProcessor.getSampleRate()
            : 48000.0;
    for (int position = 0;
         position < DistortionEngine::modeCount;
         ++position)
    {
        if (position == 10 || position == 20)
            menu.addColumnBreak();
        const auto mode =
            DistortionEngine::getModeForDisplayPosition (position);
        const auto title =
            juce::String (position + 1).paddedLeft ('0', 2)
            + "  " + names[static_cast<size_t> (mode)].toUpperCase();
        menu.addCustomItem (
            position + 1,
            std::make_unique<ModeMenuItem> (
                mode, displaySampleRate, mode == currentMode),
            nullptr,
            title);
    }

    const auto safeThis =
        juce::Component::SafePointer<DefaultDistortionAudioProcessorEditor> (this);
    menu.showMenuAsync (
        juce::PopupMenu::Options {}
            .withTargetComponent (modeButton)
            .withMinimumWidth (juce::roundToInt (
                750.0f * lookAndFeel.getUiScale()))
            .withStandardItemHeight (juce::roundToInt (
                27.0f * lookAndFeel.getUiScale())),
        [safeThis] (int result)
        {
            if (safeThis != nullptr && result > 0)
                safeThis->selectMode (
                    DistortionEngine::getModeForDisplayPosition (
                        result - 1));
        });
}

void DefaultDistortionAudioProcessorEditor::selectMode (int mode)
{
    const auto selected = juce::jlimit (
        0, DistortionEngine::modeCount - 1, mode);
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    const auto contextBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    const auto modeId = contextBand < 0
        ? juce::String (ParamIDs::mode)
        : ParamIDs::band (contextBand, "Mode");
    if (auto* parameter = ownerProcessor.parameters.getParameter (modeId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (
                static_cast<float> (selected)));
        parameter->endChangeGesture();
    }

    updateCharacterControl (selected);
    if (characterAttachment != nullptr)
        characterAttachment->setValueAsCompleteGesture (
            DistortionEngine::getDefaultCharacter (selected));
    const auto secondaryId = contextBand < 0
        ? juce::String (ParamIDs::secondary)
        : ParamIDs::band (contextBand, "Secondary");
    if (auto* parameter = ownerProcessor.parameters.getParameter (secondaryId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (
                DistortionEngine::getDefaultSecondary (selected)));
        parameter->endChangeGesture();
    }
}

void DefaultDistortionAudioProcessorEditor::stepMode (int delta)
{
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    const auto contextBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    const auto current = juce::jlimit (
        0, DistortionEngine::modeCount - 1,
        contextBand < 0
            ? ownerProcessor.getCurrentParameters().mode
            : multiband.bands[static_cast<size_t> (contextBand)].saturation.mode);
    const auto currentPosition =
        DistortionEngine::getDisplayPositionForMode (current);
    const auto nextPosition =
        (currentPosition + delta + DistortionEngine::modeCount)
        % DistortionEngine::modeCount;
    selectMode (
        DistortionEngine::getModeForDisplayPosition (nextPosition));
}

void DefaultDistortionAudioProcessorEditor::cycleAutoGain()
{
    if (autoGainAttachment == nullptr)
        return;
    autoGainAttachment->setValueAsCompleteGesture (
        static_cast<float> ((displayedAutoGainMode + 1) % 3));
}

void DefaultDistortionAudioProcessorEditor::updateAutoGainButton (int mode)
{
    displayedAutoGainMode = juce::jlimit (0, 2, mode);
    static const std::array<juce::String, 3> labels {
        "AUTO GAIN: OFF", "AUTO GAIN", "SMART AUTO GAIN"
    };
    autoGainButton.setButtonText (
        labels[static_cast<size_t> (displayedAutoGainMode)]);
    autoGainButton.setToggleState (
        displayedAutoGainMode != 0, juce::dontSendNotification);
}

void DefaultDistortionAudioProcessorEditor::updateCharacterControl (int mode)
{
    displayedMode = juce::jlimit (0, DistortionEngine::modeCount - 1, mode);
    drive.slider.textFromValueFunction = [this] (double value)
    {
        return DistortionEngine::formatDriveValue (
            displayedMode,
            static_cast<float> (value),
            ownerProcessor.getSampleRate() > 0.0
                ? ownerProcessor.getSampleRate()
                : 48000.0);
    };
    character.setTitle (
        DistortionEngine::getCharacterNames()[static_cast<size_t> (displayedMode)]);
    character.slider.setRange (
        DistortionEngine::isCharacterBipolar (displayedMode) ? -100.0 : 0.0,
        100.0,
        DistortionEngine::isCharacterStepped (displayedMode) ? 50.0 : 0.1);
    character.slider.textFromValueFunction = [this] (double value)
    {
        return DistortionEngine::formatCharacterValue (
            displayedMode,
            static_cast<float> (value / 100.0),
            ownerProcessor.getSampleRate() > 0.0
                ? ownerProcessor.getSampleRate()
                : 48000.0);
    };
    drive.slider.updateText();
    character.slider.updateText();
    const auto secondaryName = DistortionEngine::getSecondaryName (displayedMode);
    secondarySlider.setDescriptor (
        secondaryName,
        spacedVerticalText (secondaryName),
        DistortionEngine::getDefaultSecondary (displayedMode));
    secondarySlider.setVisible (
        DistortionEngine::hasSecondaryControl (displayedMode));
    drive.repaint();
    character.repaint();
}

void DefaultDistortionAudioProcessorEditor::togglePalette()
{
    lookAndFeel.setInverted (! lookAndFeel.isInverted());
    sendLookAndFeelChange();
    repaint();
}

void DefaultDistortionAudioProcessorEditor::timerCallback()
{
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs >= nextBrandGlitchTimeMs)
    {
        static const juce::String original { "default_distortion" };
        auto animated = original;
        juce::Array<int> positions;
        for (int index = 0; index < original.length(); ++index)
            if (original[index] != '_')
                positions.add (index);
        for (int index = positions.size() - 1; index > 0; --index)
            positions.swap (
                index, brandRandom.nextInt (index + 1));

        const auto selectedCount =
            1 + brandRandom.nextInt (positions.size());
        for (int index = 0; index < selectedCount; ++index)
        {
            if (brandRandom.nextBool())
                animated = animated.replaceSection (
                    positions[index],
                    1,
                    juce::String::charToString (
                        static_cast<juce::juce_wchar> (
                            33 + brandRandom.nextInt (94))));
        }
        brandLabel.setButtonText (animated);
        nextBrandGlitchTimeMs = nowMs + 4000.0;
    }

    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    if (multiband.enabled != multibandVisible)
        updateMultibandVisibility (multiband.enabled, true);
    rebindContextualControls();
    const auto contextBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    const auto mode = juce::jlimit (
        0,
        DistortionEngine::modeCount - 1,
        contextBand < 0
            ? ownerProcessor.getCurrentParameters().mode
            : multiband.bands[static_cast<size_t> (contextBand)].saturation.mode);
    if (mode != displayedMode)
    {
        updateCharacterControl (mode);
        if (characterAttachment != nullptr)
            characterAttachment->sendInitialUpdate();
    }

    const auto& name = DistortionEngine::getModeNames()[static_cast<size_t> (mode)];
    const auto displayPosition =
        DistortionEngine::getDisplayPositionForMode (mode);
    modeButton.setButtonText (
        juce::String (displayPosition + 1).paddedLeft ('0', 2)
        + "  " + name.toUpperCase());

    autoGainButton.setLoadingState (
        ownerProcessor.getSmartAutoGainProgress(),
        displayedAutoGainMode == 2
            && ! ownerProcessor.isSmartAutoGainLocked());
}

void DefaultDistortionAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    const auto designHeight = multibandVisible ? 620.0f : 354.0f;
    const auto scale = juce::jmin (
        static_cast<float> (getWidth()) / 860.0f,
        static_cast<float> (getHeight()) / designHeight);
    const auto offsetX =
        0.5f * (static_cast<float> (getWidth()) - 860.0f * scale);
    const auto offsetY =
        0.5f * (static_cast<float> (getHeight()) - designHeight * scale);
    auto rect = [scale, offsetX, offsetY] (
                    float x, float y, float width, float height)
    {
        return juce::Rectangle<float> (
            offsetX + x * scale,
            offsetY + y * scale,
            width * scale,
            height * scale);
    };

    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    graphics.fillAll (foreground);
    graphics.setColour (background);

    // Dense hard-edged blocks retain the supplied cover's visual grammar.
    graphics.fillRect (rect (0, 0, 860, 64));
    graphics.fillRect (rect (10, 74, 520, 270));
    graphics.fillRect (rect (540, 74, 310, 270));
    graphics.setColour (foreground);
    graphics.fillRect (rect (258, 0, 26, 24));
    graphics.fillRect (rect (258, 40, 26, 24));
}

void DefaultDistortionAudioProcessorEditor::resized()
{
    const auto designHeight = multibandVisible ? 620.0f : 354.0f;
    const auto scale = juce::jmin (
        static_cast<float> (getWidth()) / 860.0f,
        static_cast<float> (getHeight()) / designHeight);
    const auto offsetX = juce::roundToInt (
        0.5f * (static_cast<float> (getWidth()) - 860.0f * scale));
    const auto offsetY = juce::roundToInt (
        0.5f * (static_cast<float> (getHeight()) - designHeight * scale));
    lookAndFeel.setUiScale (scale);
    for (auto* control : {
             &drive, &character, &asym, &tone,
             &stages, &mix, &output, &quality })
        control->setUiScale (scale);

    auto scaled = [scale, offsetX, offsetY] (
                      int x, int y, int width, int height)
    {
        return juce::Rectangle<int> (
            offsetX + juce::roundToInt (static_cast<float> (x) * scale),
            offsetY + juce::roundToInt (static_cast<float> (y) * scale),
            juce::roundToInt (static_cast<float> (width) * scale),
            juce::roundToInt (static_cast<float> (height) * scale));
    };

    brandLabel.setBounds (scaled (0, 0, 258, 64));
    previousModeButton.setBounds (scaled (294, 12, 32, 40));
    modeButton.setBounds (scaled (330, 12, 314, 40));
    nextModeButton.setBounds (scaled (648, 12, 32, 40));
    autoGainButton.setBounds (scaled (692, 12, 156, 40));

    constexpr int controlWidth = 126;
    constexpr int controlHeight = 128;
    drive.setBounds (scaled (27, 78, 100, controlHeight));
    secondarySlider.setBounds (scaled (112, 107, 31, 69));
    character.setBounds (scaled (143, 78, controlWidth, controlHeight));
    asym.setBounds (scaled (285, 78, 100, controlHeight));
    asymStereoButton.setBounds (scaled (370, 107, 32, 69));
    tone.setBounds (scaled (414, 78, 100, controlHeight));
    stages.setBounds (scaled (14, 212, controlWidth, controlHeight));
    output.setBounds (scaled (143, 212, controlWidth, controlHeight));
    quality.setBounds (scaled (272, 212, controlWidth, controlHeight));
    mix.setBounds (scaled (401, 212, controlWidth, controlHeight));

    responseDisplay.setBounds (scaled (544, 78, 302, 218));
    multibandButton.setBounds (scaled (544, 304, 302, 36));
    multibandPanel.setBounds (scaled (0, 354, 860, 266));
    sendLookAndFeelChange();
}
} // namespace dd
