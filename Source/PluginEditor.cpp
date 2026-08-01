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
    getLookAndFeel().drawButtonBackground (
        graphics, *this, findColour (buttonColourId),
        isHighlighted, isDown);

    const auto active = getToggleState() || isDown;
    graphics.setColour (
        active ? backgroundOf (*this) : foregroundOf (*this));
    const auto scale = scaleOf (*this);
    graphics.setFont (monoFont (10.0f * scale, true));

    juce::Graphics::ScopedSaveState saved (graphics);
    graphics.addTransform (
        juce::AffineTransform::rotation (
            -juce::MathConstants<float>::halfPi,
            static_cast<float> (getWidth()) * 0.5f,
            static_cast<float> (getHeight()) * 0.5f));
    const auto rotatedBounds = juce::Rectangle<float> (
        static_cast<float> (getWidth() - getHeight()) * 0.5f,
        static_cast<float> (getHeight() - getWidth()) * 0.5f,
        static_cast<float> (getHeight()),
        static_cast<float> (getWidth()));
    graphics.drawFittedText (
        "S T E R E O",
        rotatedBounds.reduced (
            5.0f * scale, 2.0f * scale).toNearestInt(),
        juce::Justification::centred,
        1);
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

    const auto parameters = processor.getCurrentParameters();
    const auto mode = juce::jlimit (0, DistortionEngine::modeCount - 1, parameters.mode);
    const auto displayPosition =
        DistortionEngine::getDisplayPositionForMode (mode);
    const auto& names = DistortionEngine::getModeNames();

    auto header = bounds.reduced (12.0f * scale).removeFromTop (
        38.0f * scale);
    graphics.setColour (background);
    graphics.setFont (monoFont (25.0f * scale, true));
    graphics.drawText (
        juce::String (displayPosition + 1).paddedLeft ('0', 2),
        header.removeFromLeft (48.0f * scale),
        juce::Justification::centredLeft);
    graphics.setFont (monoFont (12.0f * scale, true));
    graphics.drawFittedText (
        names[static_cast<size_t> (mode)].toUpperCase(),
        header.toNearestInt(),
        juce::Justification::centredLeft,
        2);

    const auto displaySampleRate =
        processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const auto visualizationChanged =
        ! visualizationValid
        || parameters.mode != visualizedParameters.mode
        || differs (parameters.driveDb, visualizedParameters.driveDb)
        || differs (parameters.character, visualizedParameters.character)
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
    graph.removeFromTop (42.0f * scale);
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
      responseDisplay (owner)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (860.0 / 354.0);
    setResizeLimits (720, 296, 1200, 494);
    setSize (860, 354);

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

    for (auto* control : {
             &drive, &character, &asym, &tone,
             &stages, &mix, &output, &quality })
    {
        configureKnob (*control);
        addAndMakeVisible (*control);
    }
    addAndMakeVisible (responseDisplay);

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
    driveAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::drive, drive.slider);
    asymAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::asym, asym.slider);
    asymStereoAttachment = std::make_unique<ButtonAttachment> (
        state, ParamIDs::asymStereo, asymStereoButton);
    toneAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::tone, tone.slider);
    stagesAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::stages, stages.slider);
    mixAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::mix, mix.slider);
    outputAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::output, output.slider);
    qualityAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::quality, quality.slider);
    if (auto* parameter = state.getParameter (ParamIDs::mode))
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

    if (auto* parameter = state.getParameter (ParamIDs::character))
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
    }

    updateCharacterControl (ownerProcessor.getCurrentParameters().mode);
    if (characterAttachment != nullptr)
        characterAttachment->sendInitialUpdate();
    timerCallback();
    startTimerHz (12);
}

DefaultDistortionAudioProcessorEditor::~DefaultDistortionAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DefaultDistortionAudioProcessorEditor::configureKnob (
    ParameterControl& control)
{
    control.slider.setMouseDragSensitivity (180);
}

void DefaultDistortionAudioProcessorEditor::showModeMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    const auto currentMode = ownerProcessor.getCurrentParameters().mode;
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
    if (auto* parameter = ownerProcessor.parameters.getParameter (ParamIDs::mode))
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
}

void DefaultDistortionAudioProcessorEditor::stepMode (int delta)
{
    const auto current = juce::jlimit (
        0, DistortionEngine::modeCount - 1,
        ownerProcessor.getCurrentParameters().mode);
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

    const auto mode = juce::jlimit (
        0,
        DistortionEngine::modeCount - 1,
        ownerProcessor.getCurrentParameters().mode);
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
    const auto scale = juce::jmin (
        static_cast<float> (getWidth()) / 860.0f,
        static_cast<float> (getHeight()) / 354.0f);
    const auto offsetX =
        0.5f * (static_cast<float> (getWidth()) - 860.0f * scale);
    const auto offsetY =
        0.5f * (static_cast<float> (getHeight()) - 354.0f * scale);
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
    const auto scale = juce::jmin (
        static_cast<float> (getWidth()) / 860.0f,
        static_cast<float> (getHeight()) / 354.0f);
    const auto offsetX = juce::roundToInt (
        0.5f * (static_cast<float> (getWidth()) - 860.0f * scale));
    const auto offsetY = juce::roundToInt (
        0.5f * (static_cast<float> (getHeight()) - 354.0f * scale));
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
    drive.setBounds (scaled (14, 78, controlWidth, controlHeight));
    character.setBounds (scaled (143, 78, controlWidth, controlHeight));
    asym.setBounds (scaled (285, 78, 100, controlHeight));
    asymStereoButton.setBounds (scaled (387, 107, 12, 69));
    tone.setBounds (scaled (401, 78, controlWidth, controlHeight));
    stages.setBounds (scaled (14, 212, controlWidth, controlHeight));
    output.setBounds (scaled (143, 212, controlWidth, controlHeight));
    quality.setBounds (scaled (272, 212, controlWidth, controlHeight));
    mix.setBounds (scaled (401, 212, controlWidth, controlHeight));

    responseDisplay.setBounds (scaled (544, 78, 302, 262));
    sendLookAndFeelChange();
}
} // namespace dd
