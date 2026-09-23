#pragma once

#include "PluginProcessor.h"
#include "UI/DefaultFamilyTheme.h"
#include "UpdateChecker.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <array>

namespace dd
{
class PrototypeSimpleMenuWindow;
class PrototypeModeMenuWindow;

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
    void setThemeColours (juce::Colour lightBackground,
                          juce::Colour lightForeground,
                          juce::Colour darkBackground,
                          juce::Colour darkForeground);
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

    void drawLinearSlider (juce::Graphics&,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPos,
                           float minSliderPos,
                           float maxSliderPos,
                           juce::Slider::SliderStyle,
                           juce::Slider&) override;

    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawTextEditorOutline (juce::Graphics&,
                                int width,
                                int height,
                                juce::TextEditor&) override;

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
    int getPopupMenuBorderSize() override { return 0; }
    int getMenuWindowFlags() override { return 0; }
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
    bool inverted = true;
    float uiScale = 1.0f;
    juce::Colour lightBackgroundColour { 0xfff6f6f6 };
    juce::Colour lightForegroundColour { 0xff050505 };
    juce::Colour darkBackgroundColour { 0xff050505 };
    juce::Colour darkForegroundColour { 0xfff6f6f6 };
};

class ResettableSlider : public juce::Slider
{
public:
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
};

class VerticalDragSlider final : public ResettableSlider
{
public:
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    double dragStartProportion = 0.0;
    bool verticalDragActive = false;
};

class DistortionSettingsOverlay final : public juce::Component,
                                        private juce::ChangeListener
{
public:
    struct Statistics
    {
        float crestDeltaDb = 0.0f;
        float levelDeltaDb = 0.0f;
        float tiltDeltaDbPerOctave = 0.0f;
        float smartProgress = 0.0f;
        float smartGainDb = 0.0f;
        bool spectrumValid = false;
        bool timeValid = false;
        bool smartEnabled = false;
        bool smartLocked = false;
    };

    explicit DistortionSettingsOverlay (
        juce::AudioProcessorValueTreeState&);
    ~DistortionSettingsOverlay() override;
    std::function<void (const default_family::ThemeState&)> onStateChange;
    void setState (default_family::ThemeState);
    [[nodiscard]] const default_family::ThemeState& getState() const noexcept
    {
        return state;
    }
    void setStatistics (Statistics);
    void dismissColourEditor();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    enum class Cell
    {
        none,
        theme,
        lightBackground,
        lightForeground,
        darkBackground,
        darkForeground
    };
    [[nodiscard]] Cell cellAt (juce::Point<int>) const noexcept;
    void showColourEditor (Cell);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    default_family::ThemeState state;
    Statistics statistics;
    Cell editedColour = Cell::none;
    std::unique_ptr<juce::ColourSelector> colourSelector;
    VerticalDragSlider transientStrength;
    VerticalDragSlider transientBalance;
    VerticalDragSlider transientHold;
    VerticalDragSlider transientSmooth;
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> transientStrengthAttachment;
    std::unique_ptr<SliderAttachment> transientBalanceAttachment;
    std::unique_ptr<SliderAttachment> transientHoldAttachment;
    std::unique_ptr<SliderAttachment> transientSmoothAttachment;
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
    void setCompactLayout (bool shouldBeCompact);
    void applyPaletteColours();
    void resized() override;
    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override;

    VerticalDragSlider slider;

private:
    juce::Label titleLabel;
    float uiScale = 1.0f;
    bool compactLayout = false;
};

class TriangleButton final : public juce::Button
{
public:
    explicit TriangleButton (bool pointsRight);
    void paintButton (juce::Graphics&, bool, bool) override;

private:
    bool right = false;
};

class AlgorithmButton final : public juce::TextButton
{
public:
    AlgorithmButton();
    void setMode (int displayPosition, juce::String name);
    void paintButton (juce::Graphics&, bool, bool) override;

private:
    int number = 1;
    juce::String modeName { "SOFT CLIP" };
};

class HeaderActionButton : public juce::TextButton
{
public:
    HeaderActionButton (juce::String label, juce::String value);
    void setValueText (juce::String value);
    void paintButton (juce::Graphics&, bool, bool) override;

private:
    juce::String headerLabel;
    juce::String valueText;
};

class SmartGainButton final : public HeaderActionButton
{
public:
    SmartGainButton();
    void setLoadingState (float progress, bool isLoading);
    void paintButton (juce::Graphics&, bool, bool) override;

private:
    float loadingProgress = 0.0f;
    bool loading = false;
};

class StripButton final : public juce::TextButton
{
public:
    explicit StripButton (juce::String text);
    void setPreviewToggleOnPress (bool shouldPreview) noexcept
    {
        previewToggleOnPress = shouldPreview;
    }
    [[nodiscard]] bool getDisplayedToggleState() const noexcept
    {
        return previewToggleOnPress && isDown()
            ? ! getToggleState()
            : getToggleState();
    }
    void paintButton (juce::Graphics&, bool, bool) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool previewToggleOnPress = false;
};

class RtaBandButton final : public juce::TextButton
{
public:
    explicit RtaBandButton (juce::String text);
    void paintButton (juce::Graphics&, bool, bool) override;
};

class InputHpRouteButton final : public juce::TextButton
{
public:
    InputHpRouteButton();
    void paintButton (juce::Graphics&, bool, bool) override;
};

class VerticalTextButton final : public juce::TextButton
{
public:
    VerticalTextButton();
    void paintButton (juce::Graphics&, bool, bool) override;
};

class VerticalTextSlider final : public ResettableSlider
{
public:
    VerticalTextSlider (juce::String parameterName,
                        juce::String verticalText,
                        double defaultValue);
    void setDescriptor (juce::String parameterName,
                        juce::String verticalText,
                        double defaultValue);
    void paint (juce::Graphics&) override;

private:
    juce::String text;
};

class ResponseDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    explicit ResponseDisplay (DefaultDistortionAudioProcessor&);
    ~ResponseDisplay() override;
    void setRefreshActive (bool);
    [[nodiscard]] bool isRefreshActive() const noexcept
    {
        return isTimerRunning();
    }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    bool updateVisualization();

    DefaultDistortionAudioProcessor& processor;
    DistortionEngine::Visualization visualization;
    Parameters visualizedParameters;
    double visualizedSampleRate = 0.0;
    bool visualizationValid = false;
};

class LevelMeterPanel final : public juce::Component,
                              private juce::Timer
{
public:
    explicit LevelMeterPanel (DefaultDistortionAudioProcessor&);
    ~LevelMeterPanel() override;
    void setRefreshActive (bool);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    DefaultDistortionAudioProcessor& processor;
};

class BandTrimControl final : public ResettableSlider
{
public:
    BandTrimControl();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void lookAndFeelChanged() override;

private:
    void updateValueText();
    juce::Label valueLabel;
    bool updatingText = false;
};

class MultibandPanel final : public juce::Component,
                             private juce::Timer
{
public:
    struct SpectrumStatistics
    {
        float tiltDeltaDbPerOctave = 0.0f;
        bool spectrumValid = false;
    };

    explicit MultibandPanel (DefaultDistortionAudioProcessor&);
    ~MultibandPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    [[nodiscard]] SpectrumStatistics getSpectrumStatistics() const noexcept
    {
        return spectrumStatistics;
    }
    void setAnalyzerActive (bool);

private:
    static constexpr int fftSize = SpectrumFIFO::fftSize;
    static constexpr int spectrumPublishHop = SpectrumFIFO::publishHop;
    static constexpr float analyzerFloorDb = -80.0f;
    static constexpr float analyzerCeilingDb = 0.0f;
    static constexpr float analyzerAveragingSeconds = 0.065f;
    static constexpr float analyzerDecayDb = 1.5f;
    static constexpr float analyzerTiltDbPerOctave = 4.5f;
    void timerCallback() override;
    void updateSpectrum();
    void updateControls();
    void setParameter (const juce::String&, float plainValue);
    [[nodiscard]] juce::Rectangle<float> analyzerBounds() const;
    [[nodiscard]] float frequencyToX (float frequency) const;
    [[nodiscard]] float xToFrequency (float x) const;
    [[nodiscard]] juce::Rectangle<float> slopeBadgeBounds (int crossover) const;
    [[nodiscard]] juce::Rectangle<float> frequencyTooltipBounds (
        int crossover) const;
    [[nodiscard]] int crossoverAt (juce::Point<float>, bool badgeOnly) const;
    [[nodiscard]] int crossoverForResetAt (juce::Point<float>) const;
    [[nodiscard]] int bandAt (float x) const;
    [[nodiscard]] juce::Rectangle<float> bandBounds (int band) const;
    [[nodiscard]] float trimToY (float trimDb) const;
    [[nodiscard]] float yToTrim (float y) const;
    [[nodiscard]] int trimAt (juce::Point<float>) const;
    void resetTrim (int band);
    void resetCrossover (int crossover);
    void writeBandParameters (int band, const BandParameters&);
    void insertCrossover (int band, float frequency);
    void removeCrossover (int crossover);
    void bindTrimControl (int band);
    void beginTrimDrag (int band, float y);
    void updateTrimDrag (float y);
    void endTrimDrag();
    void showBandCountMenu();
    void showSlopeMenu (int crossover);
    void layoutBandButtons (const MultibandParameters&);

    DefaultDistortionAudioProcessor& processor;
    std::array<float, fftSize / 2> inputSpectrum {};
    std::array<float, fftSize / 2> outputSpectrum {};
    std::array<float, fftSize / 2> incomingInput {};
    std::array<float, fftSize / 2> incomingOutput {};
    SpectrumStatistics spectrumStatistics;
    int hoveredCrossover = -1;
    int draggedCrossover = -1;
    int hoveredTrimBand = -1;
    float ghostCrossoverX = -1.0f;
    int draggedTrimBand = -1;
    int trimBoundBand = -1;
    int laidOutBandCount = -1;
    bool analyzerActive = false;
    juce::RangedAudioParameter* draggedTrimParameter = nullptr;

    juce::TextButton linkButton { "LINK" };
    juce::TextButton bandCountButton { "2 BANDS" };
    juce::TextButton phaseButton { "MIN PHASE" };
    juce::TextButton soloButton { "SOLO" };
    juce::TextButton bypassButton { "BYPASS" };
    std::array<RtaBandButton, MultibandParameters::maximumBands> soloButtons {
        RtaBandButton { "S" }, RtaBandButton { "S" },
        RtaBandButton { "S" }, RtaBandButton { "S" }
    };
    std::array<RtaBandButton, MultibandParameters::maximumBands> bypassButtons {
        RtaBandButton { "B" }, RtaBandButton { "B" },
        RtaBandButton { "B" }, RtaBandButton { "B" }
    };
    BandTrimControl trimControl;
    std::unique_ptr<PrototypeSimpleMenuWindow> simpleMenu;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        trimAttachment;
};

class UpdateAvailableOverlay final : public juce::Component
{
public:
    UpdateAvailableOverlay();
    void setLatestVersion (const juce::String&);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onOpenWebsite;
    std::function<void()> onDismiss;

private:
    [[nodiscard]] juce::Rectangle<int> panelBounds() const;

    juce::String latestVersion;
    juce::TextButton openWebsiteButton { "OPEN DEFAULT-AUDIO" };
    juce::TextButton laterButton { "LATER" };
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
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;
    void timerCallback() override;
    void configureKnob (ParameterControl&);
    void showModeMenu();
    void showQualityMenu();
    void showPhaseMenu();
    void selectMode (int mode);
    void stepMode (int delta);
    void cycleAutoGain();
    void cycleRoute();
    void updateAutoGainButton (int mode);
    void updateCharacterControl (int mode);
    void rebindContextualControls();
    void beginBandGroupDrag (const juce::String& parameterSuffix,
                             float displayedValue);
    void updateBandGroupDrag (float displayedValue);
    void endBandGroupDrag();
    void updateMultibandVisibility (bool enabled, bool resizeEditor);
    void toggleSettingsOverlay();
    void hideSettingsOverlay();
    void showUpdateAvailable (const juce::String& latestVersion);
    void dismissUpdateAvailable();
    void applyThemeState (const default_family::ThemeState&, bool persist);
    void updateAnalyzerLifecycle();

    DefaultDistortionAudioProcessor& ownerProcessor;
    GeometricLookAndFeel lookAndFeel;

    BrandButton brandLabel;
    AlgorithmButton modeButton;
    TriangleButton previousModeButton { false };
    TriangleButton nextModeButton { true };
    SmartGainButton autoGainButton;
    HeaderActionButton qualityButton { "OS", "OFF" };
    HeaderActionButton pluginPowerButton { "POWER", "ON" };
    StripButton multibandButton { "MULTIBAND  ON" };
    HeaderActionButton routeButton { "ROUTE", "M/S" };
    StripButton linkStripButton { "LINK" };
    StripButton phaseStripButton { "PHASE  MINIMUM" };
    VerticalTextButton asymStereoButton;
    InputHpRouteButton inputHpDetectorButton;

    ParameterControl drive { "DRIVE" };
    ParameterControl character { "CURVE" };
    ParameterControl secondary { "SECONDARY" };
    ParameterControl asym { "ASYM" };
    ParameterControl tone { "TONE" };
    ParameterControl stages { "STAGES" };
    ParameterControl placement { "PLACEMENT" };
    ParameterControl dynamic { "DYNAMIC" };
    ParameterControl speed { "SPEED" };
    ParameterControl inputHp { "INPUT HP" };
    ParameterControl outputLp { "OUTPUT LP" };
    ParameterControl mix { "MIX" };
    ParameterControl output { "OUT" };
    ResponseDisplay responseDisplay;
    LevelMeterPanel levelMeters;
    MultibandPanel multibandPanel;
    DistortionSettingsOverlay settingsOverlay;
    UpdateAvailableOverlay updateOverlay;
    update::UpdateChecker updateChecker;

    std::unique_ptr<SliderAttachment> driveAttachment;
    std::unique_ptr<SliderAttachment> secondaryAttachment;
    std::unique_ptr<SliderAttachment> asymAttachment;
    std::unique_ptr<ButtonAttachment> asymStereoAttachment;
    std::unique_ptr<SliderAttachment> toneAttachment;
    std::unique_ptr<SliderAttachment> stagesAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<SliderAttachment> placementAttachment;
    std::unique_ptr<SliderAttachment> dynamicAttachment;
    std::unique_ptr<SliderAttachment> speedAttachment;
    std::unique_ptr<SliderAttachment> inputHpAttachment;
    std::unique_ptr<ButtonAttachment> inputHpDetectorAttachment;
    std::unique_ptr<SliderAttachment> outputLpAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    std::unique_ptr<juce::ParameterAttachment> autoGainAttachment;
    std::unique_ptr<juce::ParameterAttachment> characterAttachment;
    std::unique_ptr<juce::ParameterAttachment> routeAttachment;
    std::unique_ptr<juce::ParameterAttachment> qualityAttachment;
    std::unique_ptr<ButtonAttachment> multibandAttachment;
    std::unique_ptr<ButtonAttachment> pluginPowerAttachment;
    std::unique_ptr<PrototypeSimpleMenuWindow> simpleMenu;
    std::unique_ptr<PrototypeModeMenuWindow> modeMenu;

    int displayedMode = -1;
    int displayedAutoGainMode = -1;
    default_family::ThemeState themeState;
    double lastThemePollMilliseconds = 0.0;
    bool updatingCharacter = false;
    int boundBand = -2;
    struct BandGroupDrag
    {
        bool active = false;
        float displayedStart = 0.0f;
        std::array<float, MultibandParameters::maximumBands> bandStarts {};
        std::array<juce::RangedAudioParameter*,
                   MultibandParameters::maximumBands> parameters {};
    } bandGroupDrag;
    bool multibandVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        DefaultDistortionAudioProcessorEditor)
};
} // namespace dd
