#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace dd
{
class GeometricLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    enum ColourIds
    {
        foregroundColourId = 0x2200100,
        backgroundColourId,
        mutedColourId
    };

    GeometricLookAndFeel();
    void setInverted (bool shouldBeInverted);
    void setUiScale (float newScale) noexcept;
    [[nodiscard]] bool isInverted() const noexcept { return inverted; }
    [[nodiscard]] float getUiScale() const noexcept { return uiScale; }

    void drawRotarySlider (juce::Graphics&,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPosition,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider&) override;

    void drawComboBox (juce::Graphics&,
                       int width,
                       int height,
                       bool isButtonDown,
                       int buttonX,
                       int buttonY,
                       int buttonW,
                       int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawButtonBackground (juce::Graphics&,
                               juce::Button&,
                               const juce::Colour&,
                               bool,
                               bool) override;
    void drawButtonText (juce::Graphics&,
                         juce::TextButton&,
                         bool,
                         bool) override;

private:
    void applyPalette();
    bool inverted = false;
    float uiScale = 1.0f;
};

class BrandButton final : public juce::TextButton
{
public:
    BrandButton();
    void paintButton (juce::Graphics&, bool, bool) override;
};

class ParameterControl final : public juce::Component
{
public:
    explicit ParameterControl (juce::String title);

    void setTitle (const juce::String&);
    void setUiScale (float newScale);
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::Label titleLabel;
    float uiScale = 1.0f;
};

class TriangleButton final : public juce::Button
{
public:
    explicit TriangleButton (bool pointsRight);
    void paintButton (juce::Graphics&, bool, bool) override;

private:
    bool right = false;
};

class SmartGainButton final : public juce::TextButton
{
public:
    SmartGainButton();
    void setLoadingState (float progress, bool isLoading);
    void paintButton (juce::Graphics&, bool, bool) override;

private:
    float loadingProgress = 0.0f;
    bool loading = false;
};

class VerticalTextButton final : public juce::TextButton
{
public:
    VerticalTextButton();
    void paintButton (juce::Graphics&, bool, bool) override;
};

class VerticalTextSlider final : public juce::Slider
{
public:
    VerticalTextSlider();
    void paint (juce::Graphics&) override;
};

class ResponseDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    explicit ResponseDisplay (DefaultDistortionAudioProcessor&);
    ~ResponseDisplay() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    DefaultDistortionAudioProcessor& processor;
    DistortionEngine::Visualization visualization;
    Parameters visualizedParameters;
    double visualizedSampleRate = 0.0;
    bool visualizationValid = false;
};

class DefaultDistortionAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit DefaultDistortionAudioProcessorEditor (
        DefaultDistortionAudioProcessor&);
    ~DefaultDistortionAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;
    void timerCallback() override;
    void configureKnob (ParameterControl&);
    void showModeMenu();
    void selectMode (int mode);
    void stepMode (int delta);
    void cycleAutoGain();
    void updateAutoGainButton (int mode);
    void updateCharacterControl (int mode);
    void togglePalette();

    DefaultDistortionAudioProcessor& ownerProcessor;
    GeometricLookAndFeel lookAndFeel;

    BrandButton brandLabel;
    juce::TextButton modeButton;
    TriangleButton previousModeButton { false };
    TriangleButton nextModeButton { true };
    SmartGainButton autoGainButton;
    VerticalTextButton asymStereoButton;
    VerticalTextSlider waveSlider;

    ParameterControl drive { "DRIVE" };
    ParameterControl character { "CURVE" };
    ParameterControl asym { "ASYM" };
    ParameterControl tone { "TONE" };
    ParameterControl stages { "STAGES" };
    ParameterControl mix { "MIX" };
    ParameterControl output { "OUTPUT" };
    ParameterControl quality { "OVERSAMPLING" };
    ResponseDisplay responseDisplay;

    std::unique_ptr<SliderAttachment> driveAttachment;
    std::unique_ptr<SliderAttachment> waveAttachment;
    std::unique_ptr<SliderAttachment> asymAttachment;
    std::unique_ptr<ButtonAttachment> asymStereoAttachment;
    std::unique_ptr<SliderAttachment> toneAttachment;
    std::unique_ptr<SliderAttachment> stagesAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<SliderAttachment> qualityAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    std::unique_ptr<juce::ParameterAttachment> autoGainAttachment;
    std::unique_ptr<juce::ParameterAttachment> characterAttachment;

    int displayedMode = -1;
    int displayedAutoGainMode = -1;
    bool updatingCharacter = false;
    juce::Random brandRandom;
    double nextBrandGlitchTimeMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        DefaultDistortionAudioProcessorEditor)
};
} // namespace dd
