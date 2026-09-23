#include "../Source/PluginEditor.h"
#include "../Source/UILayout.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>

namespace
{
struct TestContext
{
    int failures = 0;

    void expect (bool condition, const juce::String& message)
    {
        if (! condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

std::vector<juce::String> expectedVersionEightIds()
{
    std::vector<juce::String> result {
        dd::ParamIDs::mode,
        dd::ParamIDs::drive,
        dd::ParamIDs::character,
        dd::ParamIDs::secondary,
        dd::ParamIDs::asym,
        dd::ParamIDs::asymStereo,
        dd::ParamIDs::tone,
        dd::ParamIDs::stages,
        dd::ParamIDs::mix,
        dd::ParamIDs::output,
        dd::ParamIDs::quality,
        dd::ParamIDs::autoGain,
        dd::ParamIDs::pluginEnabled,
        dd::ParamIDs::multibandEnabled,
        dd::ParamIDs::multibandLink,
        dd::ParamIDs::multibandBandCount,
        dd::ParamIDs::multibandPhase
    };

    for (int crossover = 0; crossover < 3; ++crossover)
    {
        result.push_back (dd::ParamIDs::crossoverFrequency (crossover));
        result.push_back (dd::ParamIDs::crossoverSlope (crossover));
    }

    constexpr std::array<const char*, 11> bandSuffixes {
        "Mode", "Drive", "Character", "Secondary", "Asym", "AsymStereo",
        "Tone", "Stages", "Mix", "Bypass", "Trim"
    };
    for (int band = 0; band < 4; ++band)
        for (const auto* suffix : bandSuffixes)
            result.push_back (dd::ParamIDs::band (band, suffix));

    return result;
}

std::vector<juce::String> expectedVersionNineIds()
{
    std::vector<juce::String> result {
        dd::ParamIDs::route,
        dd::ParamIDs::placement,
        dd::ParamIDs::dynamic,
        dd::ParamIDs::speed,
        dd::ParamIDs::inputHp,
        dd::ParamIDs::outputLp
    };
    constexpr std::array<const char*, 6> suffixes {
        "Route", "Placement", "Dynamic", "Speed", "InputHp", "OutputLp"
    };
    for (int band = 0; band < 4; ++band)
        for (const auto* suffix : suffixes)
            result.push_back (dd::ParamIDs::band (band, suffix));
    result.push_back (dd::ParamIDs::transientStrength);
    result.push_back (dd::ParamIDs::transientBalance);
    result.push_back (dd::ParamIDs::transientHold);
    result.push_back (dd::ParamIDs::transientSmooth);
    return result;
}

std::vector<juce::String> expectedVersionTenIds()
{
    std::vector<juce::String> result { dd::ParamIDs::inputHpDetector };
    for (int band = 0; band < 4; ++band)
        result.push_back (
            dd::ParamIDs::band (band, "InputHpDetector"));
    return result;
}

juce::RangedAudioParameter* parameterFor (
    dd::DefaultDistortionAudioProcessor& processor,
    const juce::String& id)
{
    return dynamic_cast<juce::RangedAudioParameter*> (
        processor.parameters.getParameter (id));
}

void setPlainValue (dd::DefaultDistortionAudioProcessor& processor,
                    const juce::String& id,
                    float value)
{
    if (auto* parameter = parameterFor (processor, id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

juce::Slider* findSlider (juce::Component& component,
                          const juce::String& name)
{
    if (auto* slider = dynamic_cast<juce::Slider*> (&component))
        if (slider->getName() == name)
            return slider;
    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* slider = findSlider (
                *component.getChildComponent (index), name))
            return slider;
    return nullptr;
}

template <typename ComponentType>
ComponentType* findComponent (juce::Component& component)
{
    if (auto* match = dynamic_cast<ComponentType*> (&component))
        return match;
    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* match = findComponent<ComponentType> (
                *component.getChildComponent (index)))
            return match;
    return nullptr;
}

juce::Button* findButton (juce::Component& component,
                          const juce::String& text)
{
    if (auto* button = dynamic_cast<juce::Button*> (&component))
        if (button->getButtonText() == text)
            return button;
    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* button = findButton (
                *component.getChildComponent (index), text))
            return button;
    return nullptr;
}

dd::StripButton* findStripButton (juce::Component& component,
                                  const juce::String& text)
{
    if (auto* button = dynamic_cast<dd::StripButton*> (&component))
        if (button->getButtonText() == text)
            return button;
    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* button = findStripButton (
                *component.getChildComponent (index), text))
            return button;
    return nullptr;
}

juce::Component* findNamedComponent (juce::Component& component,
                                     const juce::String& name)
{
    if (component.getName() == name)
        return &component;
    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* match = findNamedComponent (
                *component.getChildComponent (index), name))
            return match;
    return nullptr;
}

juce::Component* findVisibleDesktopComponent (int width, int height)
{
    auto& desktop = juce::Desktop::getInstance();
    for (int index = desktop.getNumComponents(); --index >= 0;)
        if (auto* component = desktop.getComponent (index);
            component != nullptr && component->isVisible()
                && component->getWidth() == width
                && component->getHeight() == height)
            return component;
    return nullptr;
}

void testManifestAndDefaults (TestContext& context)
{
    dd::DefaultDistortionAudioProcessor processor;
    const auto oldIds = expectedVersionEightIds();
    const auto newIds = expectedVersionNineIds();
    const auto newestIds = expectedVersionTenIds();
    const auto& parameters = processor.getParameters();
    context.expect (
        parameters.size() == static_cast<int> (
            oldIds.size() + newIds.size() + newestIds.size()),
        "Unexpected parameter count");

    std::set<juce::String> uniqueIds;
    for (int index = 0; index < parameters.size(); ++index)
    {
        const auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (
            parameters[index]);
        context.expect (ranged != nullptr, "Non-ranged host parameter");
        if (ranged == nullptr)
            continue;
        const auto id = ranged->paramID;
        uniqueIds.insert (id);
        juce::String expected;
        if (index < static_cast<int> (oldIds.size()))
            expected = oldIds[static_cast<size_t> (index)];
        else if (index < static_cast<int> (oldIds.size() + newIds.size()))
            expected = newIds[static_cast<size_t> (index) - oldIds.size()];
        else
            expected = newestIds[static_cast<size_t> (index)
                                 - oldIds.size() - newIds.size()];
        context.expect (
            id == expected,
            "Parameter index " + juce::String (index)
                + " changed: expected " + expected + ", got " + id);
    }
    context.expect (
        uniqueIds.size() == static_cast<size_t> (parameters.size()),
        "Parameter IDs are not unique");

    const auto current = processor.getCurrentParameters();
    context.expect (current.route == 0, "ROUTE default is not M/S");
    context.expect (std::abs (current.placementPercent) < 0.001f,
                    "PLACEMENT default is not neutral: "
                        + juce::String (current.placementPercent));
    context.expect (std::abs (current.dynamicPercent) < 0.001f,
                    "DYNAMIC default is not neutral: "
                        + juce::String (current.dynamicPercent));
    context.expect (current.speedPercent == 100.0f,
                    "SPEED default is not 100%");
    context.expect (current.inputHpHz == 0.0f,
                    "INPUT HP default is not OFF");
    context.expect (! current.inputHpDetector,
                    "INPUT HP default route is not the audio path");
    context.expect (current.outputLpHz == 20000.0f,
                    "OUTPUT LP default is not OFF");
    context.expect (
        current.transientStrengthPercent == 100.0f
            && std::abs (current.transientBalancePercent) < 0.001f
            && current.transientHoldPercent == 50.0f
            && current.transientSmoothPercent == 50.0f,
        "T/S splitter defaults changed: "
            + juce::String (current.transientStrengthPercent) + "/"
            + juce::String (current.transientBalancePercent) + "/"
            + juce::String (current.transientHoldPercent) + "/"
            + juce::String (current.transientSmoothPercent));

    const auto* hp = parameterFor (processor, dd::ParamIDs::inputHp);
    const auto* lp = parameterFor (processor, dd::ParamIDs::outputLp);
    context.expect (hp != nullptr && hp->getText (0.0f, 32) == "OFF",
                    "INPUT HP endpoint is not labelled OFF");
    context.expect (
        hp != nullptr && hp->getText (1.0f, 32) == "2.00 kHz",
        "INPUT HP maximum is not 2 kHz");
    context.expect (lp != nullptr && lp->getText (1.0f, 32) == "OFF",
                    "OUTPUT LP endpoint is not labelled OFF");
}

void testUpdatePolicyContract (TestContext& context)
{
    const auto newer = dd::update::compareVersions ("0.9.0", "v0.10.0");
    const auto equal = dd::update::compareVersions ("0.9", "0.9.0");
    const auto older = dd::update::compareVersions ("0.9.0", "v0.8.9");
    const auto invalid = dd::update::compareVersions ("0.9.0", "latest");
    context.expect (newer.has_value() && *newer > 0,
                    "Update checker missed a newer minor version");
    context.expect (equal.has_value() && *equal == 0,
                    "Update checker does not normalize trailing zeroes");
    context.expect (older.has_value() && *older < 0,
                    "Update checker accepted an older version as newer");
    context.expect (! invalid.has_value(),
                    "Update checker accepted a non-version release tag");

    constexpr std::int64_t now = 2'000'000'000'000LL;
    context.expect (dd::update::isCheckDue (now, 0),
                    "First update check is not due");
    context.expect (! dd::update::isCheckDue (
                        now, now + dd::update::retryAfterFailureMilliseconds),
                    "Failed update check is retried before the next day");
    context.expect (! dd::update::isCheckDue (
                        now, now + dd::update::retryAfterSuccessMilliseconds),
                    "Successful update check is retried before the next week");
    context.expect (dd::update::isCheckDue (
                        now, now + dd::update::retryAfterSuccessMilliseconds + 1),
                    "Backward system-clock protection did not make the check due");

    dd::GeometricLookAndFeel look;
    dd::UpdateAvailableOverlay overlay;
    overlay.setLookAndFeel (&look);
    overlay.setBounds (0, 0, dd::ui::designWidth, dd::ui::compactHeight);
    overlay.setLatestVersion ("0.10.0");
    auto* open = findButton (overlay, "OPEN DEFAULT-AUDIO");
    auto* later = findButton (overlay, "LATER");
    context.expect (
        open != nullptr && later != nullptr
            && open->getBounds().getWidth() > later->getBounds().getWidth(),
        "Styled update window buttons are missing or mis-sized");
    overlay.setLookAndFeel (nullptr);
}

void testVersionFiveMigration (TestContext& context)
{
    dd::DefaultDistortionAudioProcessor source;
    setPlainValue (source, dd::ParamIDs::drive, 12.34f);
    setPlainValue (source, dd::ParamIDs::mix, 0.37f);
    setPlainValue (source, dd::ParamIDs::multibandLink, 0.0f);
    auto legacyState = source.parameters.copyState();
    const auto versionNineIds = expectedVersionNineIds();
    const auto versionTenIds = expectedVersionTenIds();
    std::set<juce::String> newIds (
        versionNineIds.begin(), versionNineIds.end());
    newIds.insert (versionTenIds.begin(), versionTenIds.end());
    for (int child = legacyState.getNumChildren() - 1; child >= 0; --child)
        if (newIds.contains (
                legacyState.getChild (child).getProperty ("id").toString()))
            legacyState.removeChild (child, nullptr);
    legacyState.setProperty ("defaultDistortionStateSchema", 5, nullptr);

    juce::MemoryBlock legacyBinary;
    if (auto xml = legacyState.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, legacyBinary);

    dd::DefaultDistortionAudioProcessor restored;
    setPlainValue (restored, dd::ParamIDs::route, 1.0f);
    setPlainValue (restored, dd::ParamIDs::placement, 78.0f);
    setPlainValue (restored, dd::ParamIDs::dynamic, -65.0f);
    setPlainValue (restored, dd::ParamIDs::speed, 4.0f);
    setPlainValue (restored, dd::ParamIDs::inputHp, 180.0f);
    setPlainValue (restored, dd::ParamIDs::inputHpDetector, 1.0f);
    setPlainValue (restored, dd::ParamIDs::outputLp, 2400.0f);
    restored.setStateInformation (
        legacyBinary.getData(), static_cast<int> (legacyBinary.getSize()));

    const auto current = restored.getCurrentParameters();
    context.expect (std::abs (current.driveDb - 12.34f) < 0.011f,
                    "v5 DRIVE was not restored");
    context.expect (std::abs (current.mix - 0.37f) < 0.0011f,
                    "v5 MIX was not restored");
    context.expect (current.route == 0, "v5 ROUTE did not migrate to M/S");
    context.expect (std::abs (current.placementPercent) < 0.001f,
                    "v5 PLACEMENT did not migrate to neutral");
    context.expect (std::abs (current.dynamicPercent) < 0.001f,
                    "v5 DYNAMIC did not migrate to neutral");
    context.expect (current.speedPercent == 100.0f,
                    "v5 SPEED did not migrate to default");
    context.expect (current.inputHpHz == 0.0f,
                    "v5 INPUT HP did not migrate to OFF");
    context.expect (! current.inputHpDetector,
                    "v5 INPUT HP route did not migrate to audio");
    context.expect (current.outputLpHz == 20000.0f,
                    "v5 OUTPUT LP did not migrate to OFF");
    context.expect (
        current.transientStrengthPercent == 100.0f
            && std::abs (current.transientBalancePercent) < 0.001f
            && current.transientHoldPercent == 50.0f
            && current.transientSmoothPercent == 50.0f,
        "v5 T/S splitter settings did not migrate to defaults");

    const auto bands = restored.getCurrentMultibandParameters();
    for (int band = 0; band < dd::MultibandParameters::maximumBands; ++band)
    {
        const auto& values = bands.bands[static_cast<size_t> (band)].saturation;
        context.expect (values.route == 0, "v5 band ROUTE migration failed");
        context.expect (std::abs (values.placementPercent) < 0.001f,
                        "v5 band PLACEMENT migration failed");
        context.expect (std::abs (values.dynamicPercent) < 0.001f,
                        "v5 band DYNAMIC migration failed");
        context.expect (values.speedPercent == 100.0f,
                        "v5 band SPEED migration failed");
        context.expect (values.inputHpHz == 0.0f,
                        "v5 band INPUT HP migration failed");
        context.expect (! values.inputHpDetector,
                        "v5 band INPUT HP route migration failed");
        context.expect (values.outputLpHz == 20000.0f,
                        "v5 band OUTPUT LP migration failed");
    }

    juce::MemoryBlock migratedBinary;
    restored.getStateInformation (migratedBinary);
    const auto migratedXml = juce::AudioProcessor::getXmlFromBinary (
        migratedBinary.getData(), static_cast<int> (migratedBinary.getSize()));
    context.expect (
        migratedXml != nullptr
            && migratedXml->getIntAttribute (
                "defaultDistortionStateSchema", 0) == 8,
        "Migrated state was not persisted as schema 8");
}

void testBooleanStateCanonicalization (TestContext& context)
{
    dd::DefaultDistortionAudioProcessor processor;
    auto* off = parameterFor (
        processor, dd::ParamIDs::band (1, "AsymStereo"));
    auto* on = parameterFor (
        processor, dd::ParamIDs::band (2, "AsymStereo"));
    context.expect (off != nullptr && on != nullptr,
                    "Boolean state test parameters are missing");
    if (off == nullptr || on == nullptr)
        return;
    off->setValueNotifyingHost (0.161642f);
    on->setValueNotifyingHost (0.700689f);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    off->setValueNotifyingHost (0.300689f);
    on->setValueNotifyingHost (0.800689f);
    processor.setStateInformation (
        state.getData(), static_cast<int> (state.getSize()));
    const auto* restoredOff = parameterFor (
        processor, dd::ParamIDs::band (1, "AsymStereo"));
    const auto* restoredOn = parameterFor (
        processor, dd::ParamIDs::band (2, "AsymStereo"));
    context.expect (
        restoredOff != nullptr && restoredOff->getValue() == 0.0f,
        "False boolean state was not persisted canonically");
    context.expect (
        restoredOn != nullptr && restoredOn->getValue() == 1.0f,
        "True boolean state was not persisted canonically");
}

void testLayoutContract (TestContext& context)
{
    context.expect (dd::ui::designWidth == 648, "Editor width contract changed");
    context.expect (dd::ui::compactHeight == 286,
                    "Compact height contract changed");
    context.expect (dd::ui::expandedHeight == 450,
                    "Expanded height contract changed");
    context.expect (dd::ui::header.height == 60, "Header height changed");
    context.expect (dd::ui::main.height == 182, "Main height changed");
    context.expect (dd::ui::utility.height == 28, "Utility height changed");
    context.expect (dd::ui::rta.width == 640 && dd::ui::rta.height == 160,
                    "RTA frame contract changed");
    for (const auto& control : dd::ui::controls)
        context.expect (control.width == 100 && control.height == 60,
                        "Control matrix cell changed");
    context.expect (
        dd::ui::meters.x + dd::ui::meters.width == dd::ui::response.x,
        "Meters/response divider is misaligned");
    context.expect (
        dd::ui::oversampling.x == dd::ui::meters.x
            && dd::ui::algorithmNext.x + dd::ui::algorithmNext.width
                == dd::ui::meters.x,
        "Algorithm/OS divider is misaligned with ASYM/meters");
    context.expect (
        dd::ui::utilityCells[2].x + dd::ui::utilityCells[2].width
            == dd::ui::meters.x,
        "PHASE/MIX divider is misaligned with controls/meters");

    dd::DefaultDistortionAudioProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 128);
    processor.prepareToPlay (48000.0, 128);
    setPlainValue (processor, dd::ParamIDs::multibandEnabled, 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor != nullptr)
        editor->setSize (dd::ui::designWidth, dd::ui::expandedHeight);
    if (editor != nullptr)
    {
        auto* dynamic = findSlider (*editor, "DYNAMIC");
        auto* speed = findSlider (*editor, "SPEED");
        auto* route = findButton (*editor, "ROUTE M/S");
        auto* placement = findSlider (*editor, "PLACEMENT");
        auto* tone = findSlider (*editor, "TONE");
        auto* stages = findSlider (*editor, "STAGES");
        auto* inputHp = findSlider (*editor, "INPUT HP");
        auto* outputLp = findSlider (*editor, "OUTPUT LP");
        context.expect (
            dynamic != nullptr && speed != nullptr
                && route != nullptr && placement != nullptr,
            "Second-row controls are missing");
        if (dynamic != nullptr && speed != nullptr
            && route != nullptr && placement != nullptr)
            context.expect (
                route->getBounds()
                        == juce::Rectangle<int> (4, 129, 100, 60)
                    && placement->getParentComponent()->getBounds()
                        == juce::Rectangle<int> (104, 129, 100, 60)
                    && dynamic->getParentComponent()->getBounds()
                        == juce::Rectangle<int> (204, 129, 100, 60)
                    && speed->getParentComponent()->getBounds()
                        == juce::Rectangle<int> (304, 129, 100, 60),
                "Second-row order is not ROUTE/PLACEMENT/DYNAMIC/SPEED");
        context.expect (
            tone != nullptr && stages != nullptr
                && inputHp != nullptr && outputLp != nullptr,
            "Bottom-row controls are missing");
        if (tone != nullptr && stages != nullptr
            && inputHp != nullptr && outputLp != nullptr)
            context.expect (
                tone->getParentComponent()->getBounds()
                        == juce::Rectangle<int> (4, 190, 100, 60)
                    && stages->getParentComponent()->getBounds()
                        == juce::Rectangle<int> (104, 190, 100, 60)
                    && inputHp->getParentComponent()->getBounds()
                        == juce::Rectangle<int> (204, 190, 100, 60)
                    && outputLp->getParentComponent()->getBounds()
                        == juce::Rectangle<int> (304, 190, 100, 60),
                "Bottom-row order is not TONE/STAGES/INPUT HP/OUTPUT LP");
    }
    auto* panel = editor != nullptr
        ? findComponent<dd::MultibandPanel> (*editor) : nullptr;
    auto* secondSolo = editor != nullptr
        ? findNamedComponent (*editor, "Band 2 Solo") : nullptr;
    context.expect (panel != nullptr && secondSolo != nullptr,
                    "Multiband live-layout controls are missing");
    if (panel != nullptr && secondSolo != nullptr)
    {
        const auto before = secondSolo->getX();
        const auto panelBounds = panel->getLocalBounds().toFloat().reduced (4.0f);
        const auto crossover = processor.getCurrentMultibandParameters()
            .crossoverHz[0];
        const auto x = panelBounds.getX()
            + std::log (crossover / 20.0f) / std::log (20000.0f / 20.0f)
                * panelBounds.getWidth();
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent down (
            source, { x, panelBounds.getCentreY() },
            juce::ModifierKeys::leftButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            panel, panel, now, { x, panelBounds.getCentreY() }, now, 1, false);
        panel->mouseDown (down);
        const juce::MouseEvent drag (
            source, { x + 80.0f, panelBounds.getCentreY() },
            juce::ModifierKeys::leftButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            panel, panel, now, { x, panelBounds.getCentreY() }, now, 1, true);
        panel->mouseDrag (drag);
        context.expect (
            secondSolo->getX() > before + 20,
            "Band S/B buttons did not follow the crossover drag ("
                + juce::String (before) + " -> "
                + juce::String (secondSolo->getX()) + ", x "
                + juce::String (x, 1) + ")");
        panel->mouseUp (drag);
    }
}

void testThemeAndSettingsContract (TestContext& context)
{
    context.expect (
        std::abs (default_family::EditorPreferences::defaultScale - 1.25f)
            < 0.001f,
        "First-run editor scale is not 1.25x");
    const auto originalTheme = default_family::ThemePreferences::load (true);
    const auto originalScale = default_family::EditorPreferences::loadScale();
    auto testTheme = originalTheme;
    testTheme.mode = default_family::ThemePreferences::black;
    testTheme.lightBackground = juce::Colour (0xffe8e7e6);
    testTheme.darkForeground = juce::Colour (0xffd8d7d6);
    default_family::ThemePreferences::save (testTheme);
    const auto restoredTheme = default_family::ThemePreferences::load (true);
    context.expect (
        restoredTheme == testTheme,
        "Shared default-family theme state did not round-trip");
    default_family::EditorPreferences::saveScale (1.75f);
    context.expect (
        std::abs (default_family::EditorPreferences::loadScale() - 1.75f)
            < 0.001f,
        "Editor scale preference did not round-trip");

    dd::DefaultDistortionAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    context.expect (editor != nullptr, "Settings editor could not be created");
    if (editor != nullptr)
    {
        editor->setSize (dd::ui::designWidth, dd::ui::compactHeight);
        auto* overlay = findComponent<dd::DistortionSettingsOverlay> (*editor);
        auto* brand = findButton (*editor, "default_distortion");
        context.expect (overlay != nullptr && brand != nullptr,
                        "Logo settings overlay components are missing");
        if (overlay != nullptr && brand != nullptr && brand->onClick != nullptr)
        {
            context.expect (! overlay->isVisible(),
                            "Settings overlay starts visible");
            brand->onClick();
            context.expect (
                overlay->isVisible()
                    && overlay->getBounds() == juce::Rectangle<int> (4, 68, 640, 182),
                "Logo did not open the 640x182 settings overlay");
            auto* strength = findSlider (*overlay, "T/S STRENGTH");
            auto* balance = findSlider (*overlay, "T/S BALANCE");
            auto* hold = findSlider (*overlay, "T/S HOLD");
            auto* smooth = findSlider (*overlay, "T/S SMOOTH");
            context.expect (
                strength != nullptr && balance != nullptr
                    && hold != nullptr && smooth != nullptr,
                "Logo menu is missing T/S splitter controls");
            if (strength != nullptr && balance != nullptr
                && hold != nullptr && smooth != nullptr)
            {
                context.expect (
                    strength->getValue() == 100.0
                        && std::abs (balance->getValue()) < 0.001
                        && hold->getValue() == 50.0
                        && smooth->getValue() == 50.0,
                    "Logo menu T/S controls do not expose DSP defaults");
                context.expect (
                    strength->getY() == 54 && strength->getBottom() == 118
                        && smooth->getRight() == overlay->getWidth(),
                    "Logo menu T/S control row geometry changed");
                setPlainValue (
                    processor, dd::ParamIDs::transientBalance, 25.0f);
                context.expect (
                    std::abs (balance->getValue() - 25.0) < 0.11,
                    "T/S Balance control is not attached to APVTS");
            }
            brand->onClick();
            context.expect (! overlay->isVisible(),
                            "Second logo click did not close settings");
        }
    }
    editor.reset();
    default_family::ThemePreferences::save (originalTheme);
    default_family::EditorPreferences::saveScale (originalScale);
}

void testUtilityStripInteractionContract (TestContext& context)
{
    dd::DefaultDistortionAudioProcessor processor;
    setPlainValue (processor, dd::ParamIDs::multibandEnabled, 0.0f);
    setPlainValue (processor, dd::ParamIDs::multibandLink, 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    context.expect (editor != nullptr, "Editor could not be created");
    if (editor == nullptr)
        return;

    editor->setSize (dd::ui::designWidth, dd::ui::compactHeight);
    auto* response = findComponent<dd::ResponseDisplay> (*editor);
    context.expect (
        response != nullptr && response->isRefreshActive(),
        "Response visualization refresh is not active before MULTIBAND toggle");

    // At fractional UI scales, the utility background and its child buttons
    // must meet on the same integer pixel. Otherwise the half-covered row is
    // visible as a grey/white horizontal seam over the active LINK button.
    editor->setSize (
        juce::roundToInt (dd::ui::designWidth * 1.25f),
        juce::roundToInt (dd::ui::compactHeight * 1.25f));
    const auto fractional = editor->createComponentSnapshot (
        editor->getLocalBounds(), true, 1.0f);
    const auto scale = static_cast<float> (editor->getWidth())
        / static_cast<float> (dd::ui::designWidth);
    const auto linkCentreX = juce::roundToInt (
        (dd::ui::utilityCells[1].x
         + 0.5f * dd::ui::utilityCells[1].width) * scale);
    const auto linkTop = juce::roundToInt (
        dd::ui::utilityCells[1].y * scale);
    context.expect (
        fractional.getPixelAt (linkCentreX, linkTop - 1)
            == fractional.getPixelAt (linkCentreX, linkTop),
        "Fractional UI scale leaves a horizontal seam above LINK");
    const auto headerDividerX = juce::roundToInt (573.0f * scale);
    const auto headerMiddleY = juce::roundToInt (34.0f * scale);
    context.expect (
        fractional.getPixelAt (headerDividerX, headerMiddleY)
            == fractional.getPixelAt (0, 0),
        "Fractional UI scale antialiases the POWER divider");
    const auto powerLeft = juce::roundToInt (dd::ui::power.x * scale);
    context.expect (
        fractional.getPixelAt (powerLeft - 1, headerMiddleY)
            == fractional.getPixelAt (0, 0),
        "Fractional UI scale leaves a white seam before POWER");

    editor->setSize (dd::ui::designWidth, dd::ui::compactHeight);
    auto* multiband = findStripButton (*editor, "MULTIBAND  OFF");
    context.expect (multiband != nullptr, "MULTIBAND strip button is missing");
    if (multiband != nullptr)
    {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto centre = multiband->getLocalBounds().toFloat().getCentre();
        const juce::MouseEvent down (
            source, centre, juce::ModifierKeys::leftButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            multiband, multiband, now, centre, now, 1, false);
        multiband->mouseDown (down);
        context.expect (
            multiband->getDisplayedToggleState(),
            "MULTIBAND does not preview its enabled state on mouse-down");
        multiband->mouseUp (down);
        context.expect (
            processor.getCurrentMultibandParameters().enabled,
            "MULTIBAND mouse-up did not commit the enabled state");

        const auto enabledCentre = multiband->getLocalBounds().toFloat().getCentre();
        const juce::MouseEvent disableDown (
            source, enabledCentre, juce::ModifierKeys::leftButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            multiband, multiband, now, enabledCentre, now, 1, false);
        multiband->mouseDown (disableDown);
        context.expect (
            ! multiband->getDisplayedToggleState(),
            "MULTIBAND does not preview its disabled state on mouse-down");
        multiband->mouseUp (disableDown);

        multiband->setToggleState (true, juce::sendNotification);
        auto* panel = findComponent<dd::MultibandPanel> (*editor);
        context.expect (
            processor.getCurrentMultibandParameters().enabled
                && panel != nullptr && panel->isVisible()
                && editor->getHeight() == dd::ui::expandedHeight,
            "MULTIBAND click did not expand the editor immediately");
    }

    auto* link = findStripButton (*editor, "LINK");
    context.expect (link != nullptr, "LINK strip button is missing");
    if (link != nullptr)
    {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto centre = link->getLocalBounds().toFloat().getCentre();
        const juce::MouseEvent down (
            source, centre, juce::ModifierKeys::leftButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            link, link, now, centre, now, 1, false);
        link->mouseDown (down);
        context.expect (
            ! link->getDisplayedToggleState(),
            "LINK does not preview its disabled state on mouse-down");
        link->mouseUp (down);
        context.expect (
            ! processor.getCurrentMultibandParameters().linked,
            "LINK mouse-up did not commit the disabled state");

        link->onClick();
        context.expect (
            processor.getCurrentMultibandParameters().linked
                && link->getToggleState(),
            "LINK re-enable did not update its visual state");
    }

    auto* detector = dynamic_cast<juce::Button*> (
        findNamedComponent (*editor, "Input HP Detector"));
    context.expect (detector != nullptr,
                    "INPUT HP detector-routing button is missing");
    if (detector != nullptr)
    {
        context.expect (
            detector->getBounds()
                == juce::Rectangle<int> (260, 177, 14, 24),
            "INPUT HP audio-route button geometry changed: "
                + detector->getBounds().toString());
        context.expect (
            ! detector->getToggleState()
                && ! processor.getCurrentParameters().inputHpDetector,
            "INPUT HP route does not default to audio input");
        context.expect (detector->isOpaque(),
                        "INPUT HP route button is not opaque");
        detector->setToggleState (true, juce::sendNotification);
        context.expect (
            processor.getCurrentParameters().inputHpDetector
                && detector->getToggleState()
                && detector->getButtonText().contains ("DYN"),
            "INPUT HP detector route did not update parameter and UI state");
    }
}

void testSmartGainAnimationContract (TestContext& context)
{
    dd::GeometricLookAndFeel look;
    look.setInverted (true);
    dd::SmartGainButton button;
    button.setLookAndFeel (&look);
    button.setBounds (0, 0, 110, 60);
    button.setValueText ("SMART");
    button.setLoadingState (0.20f, true);
    const auto early = button.createComponentSnapshot (
        button.getLocalBounds(), true, 1.0f);
    button.setLoadingState (0.80f, true);
    const auto late = button.createComponentSnapshot (
        button.getLocalBounds(), true, 1.0f);
    auto changed = false;
    for (int y = 54; ! changed && y < 59; ++y)
        for (int x = 5; x < 105; ++x)
            if (early.getPixelAt (x, y) != late.getPixelAt (x, y))
            {
                changed = true;
                break;
            }
    context.expect (changed,
                    "Smart Auto Gain progress animation is not visible");
    button.setLookAndFeel (nullptr);
}

void testSliderInteractionContract (TestContext& context)
{
    dd::DefaultDistortionAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    context.expect (editor != nullptr, "Editor could not be created");
    if (editor == nullptr)
        return;
    editor->setVisible (true);

    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    for (const auto* name : { "MIX", "OUT" })
    {
        auto* slider = findSlider (*editor, name);
        context.expect (slider != nullptr,
                        juce::String (name) + " slider is missing");
        if (slider == nullptr)
            continue;
        context.expect (
            slider->getMouseCursor()
                == juce::MouseCursor::UpDownResizeCursor,
            juce::String (name) + " cursor is not vertical-resize");

        slider->setValue (
            0.5 * (slider->getMinimum() + slider->getMaximum()),
            juce::dontSendNotification);
        const auto start = slider->getValue();
        const juce::MouseEvent down (
            source, { 10.0f, 10.0f },
            juce::ModifierKeys::leftButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            slider, slider, now, { 10.0f, 10.0f }, now, 1, false);
        slider->mouseDown (down);
        const juce::MouseEvent horizontal (
            source, { 80.0f, 10.0f },
            juce::ModifierKeys::leftButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            slider, slider, now, { 10.0f, 10.0f }, now, 1, true);
        slider->mouseDrag (horizontal);
        context.expect (
            std::abs (slider->getValue() - start) < 0.0001,
            juce::String (name) + " changed on horizontal drag");
        const juce::MouseEvent vertical (
            source, { 10.0f, -50.0f },
            juce::ModifierKeys::leftButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            slider, slider, now, { 10.0f, 10.0f }, now, 1, true);
        slider->mouseDrag (vertical);
        context.expect (
            slider->getValue() > start,
            juce::String (name) + " did not respond to vertical drag");
        slider->mouseUp (vertical);

        slider->setValue (
            slider->getMinimum()
                + 0.25 * (slider->getMaximum() - slider->getMinimum()),
            juce::dontSendNotification);
        const auto resetValue = slider->getDoubleClickReturnValue();
        const juce::MouseEvent rightDown (
            source, { 10.0f, 50.0f },
            juce::ModifierKeys::rightButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            slider, slider, now, { 10.0f, 50.0f }, now, 1, false);
        slider->mouseDown (rightDown);
        const juce::MouseEvent rightDrag (
            source, { 10.0f, 5.0f },
            juce::ModifierKeys::rightButtonModifier,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            slider, slider, now, { 10.0f, 50.0f }, now, 1, true);
        slider->mouseDrag (rightDrag);
        context.expect (
            std::abs (slider->getValue() - resetValue) < 0.0001,
            juce::String (name)
                + " right-click reset was overwritten by a drag");
        slider->mouseUp (rightDrag);
    }

    const auto* mixHit = editor->getComponentAt (
        juce::Point<int> { 443, 266 });
    const auto* outHit = editor->getComponentAt (
        juce::Point<int> { 520, 266 });
    context.expect (
        mixHit == findSlider (*editor, "MIX"),
        "MIX numeric label blocks the slider hit target; hit "
            + (mixHit != nullptr ? mixHit->getName() : juce::String { "null" }));
    context.expect (
        outHit == findSlider (*editor, "OUT"),
        "OUT numeric label blocks the slider hit target; hit "
            + (outHit != nullptr ? outHit->getName() : juce::String { "null" }));
}

void testPopupLifecycleContract (TestContext& context)
{
    dd::DefaultDistortionAudioProcessor processor;
    setPlainValue (processor, dd::ParamIDs::multibandPhase, 1.0f);
    setPlainValue (processor, dd::ParamIDs::quality, 2.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    context.expect (editor != nullptr, "Popup test editor could not be created");
    if (editor == nullptr)
        return;
    editor->setVisible (true);

    auto* phase = findButton (*editor, "PHASE  LINEAR");
    context.expect (phase != nullptr && phase->onClick != nullptr,
                    "Phase selector is missing in LINEAR state");
    if (phase != nullptr)
    {
        context.expect (! phase->getToggleState(),
                        "LINEAR incorrectly inverts the closed Phase cell");
        phase->onClick();
        auto* phasePopup = findVisibleDesktopComponent (100, 60);
        context.expect (
            phasePopup != nullptr,
            "Phase menu geometry is not 100x60");
        if (phasePopup != nullptr)
            context.expect (
                phasePopup->getScreenY() == phase->getScreenBounds().getBottom(),
                "Phase menu does not open downward from the selector");
        phase->onClick();
        context.expect (
            findVisibleDesktopComponent (100, 60) == nullptr,
            "Second Phase click did not dismiss the menu");
    }

    auto* mode = findButton (*editor, "01  SOFT CLIP");
    context.expect (mode != nullptr && mode->onClick != nullptr,
                    "Algorithm selector is missing");
    if (mode != nullptr && mode->onClick != nullptr)
    {
        const auto closedSelector = mode->createComponentSnapshot (
            mode->getLocalBounds(), true, 1.0f);
        mode->onClick();
        context.expect (
            findVisibleDesktopComponent (640, 182) != nullptr,
            "Algorithm menu is not a 640x182 screen-level window");
        context.expect ((bool) mode->getProperties().getWithDefault (
                            "pickerOpen", false),
                        "Algorithm selector did not enter open state");
        const auto openSelector = mode->createComponentSnapshot (
            mode->getLocalBounds(), true, 1.0f);
        auto selectorChanged = closedSelector.getWidth()
                != openSelector.getWidth()
            || closedSelector.getHeight() != openSelector.getHeight();
        for (int y = 0; ! selectorChanged && y < closedSelector.getHeight(); ++y)
            for (int x = 0; x < closedSelector.getWidth(); ++x)
                if (closedSelector.getPixelAt (x, y).getARGB()
                    != openSelector.getPixelAt (x, y).getARGB())
                {
                    selectorChanged = true;
                    break;
                }
        context.expect (
            ! selectorChanged,
            "Algorithm selector draws an open-state outline");
        mode->onClick();
        context.expect (
            findVisibleDesktopComponent (640, 182) == nullptr,
            "Second algorithm click did not dismiss the menu");
        context.expect (! (bool) mode->getProperties().getWithDefault (
                            "pickerOpen", false),
                        "Algorithm selector kept a stale open state");
    }

    auto* quality = findButton (*editor, "OS 4X");
    context.expect (quality != nullptr && quality->onClick != nullptr,
                    "OS selector is missing");
    if (quality != nullptr && quality->onClick != nullptr)
    {
        quality->onClick();
        auto* popup = findVisibleDesktopComponent (60, 112);
        context.expect (popup != nullptr,
                        "OS menu geometry is not 60x112");
        context.expect ((bool) quality->getProperties().getWithDefault (
                            "pickerOpen", false),
                        "OS selector did not enter open state");
        if (popup != nullptr)
            popup->keyPressed (juce::KeyPress (
                juce::KeyPress::escapeKey));
        context.expect (
            findVisibleDesktopComponent (60, 112) == nullptr,
            "Escape did not dismiss the OS menu");
        context.expect (! (bool) quality->getProperties().getWithDefault (
                            "pickerOpen", false),
                        "OS selector kept a stale open state");
    }
}

void testLatencyTransition (TestContext& context)
{
    constexpr int samples = 256;
    dd::DefaultDistortionAudioProcessor processor;
    processor.prepareToPlay (48000.0, samples);
    setPlainValue (processor, dd::ParamIDs::autoGain, 0.0f);
    setPlainValue (processor, dd::ParamIDs::mix, 0.0f);

    juce::AudioBuffer<float> audio (2, samples);
    juce::MidiBuffer midi;
    double phase = 0.0;
    auto previous = 0.0f;
    auto maximumStep = 0.0f;
    const auto fill = [&]
    {
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto value = 0.25f * static_cast<float> (std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * 440.0 / 48000.0;
            audio.setSample (0, sample, value);
            audio.setSample (1, sample, value);
        }
    };
    for (int block = 0; block < 28; ++block)
    {
        fill();
        processor.processBlock (audio, midi);
        previous = audio.getSample (0, samples - 1);
    }
    const auto baseLatency = processor.getLatencySamples();

    setPlainValue (processor, dd::ParamIDs::route, 1.0f);
    setPlainValue (processor, dd::ParamIDs::placement, -100.0f);
    fill();
    processor.processBlock (audio, midi);
    context.expect (processor.getLatencySamples() == baseLatency,
                    "T/S latency changed before the fade-out completed");
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto value = audio.getSample (0, sample);
        maximumStep = juce::jmax (maximumStep, std::abs (value - previous));
        previous = value;
    }

    fill();
    processor.processBlock (audio, midi);
    context.expect (processor.getLatencySamples() > baseLatency,
                    "T/S latency was not committed at the silent transition");
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto value = audio.getSample (0, sample);
        maximumStep = juce::jmax (maximumStep, std::abs (value - previous));
        previous = value;
    }
    context.expect (maximumStep < 0.04f,
                    "Live T/S route change is not click-guarded");
}

void testAnalyzerTransportAndLinking (TestContext& context)
{
    constexpr int samples = 128;
    dd::DefaultDistortionAudioProcessor processor;
    processor.prepareToPlay (48000.0, samples);
    processor.setAnalyzerEnabled (true, true);
    setPlainValue (processor, dd::ParamIDs::autoGain, 0.0f);
    setPlainValue (processor, dd::ParamIDs::drive, 0.0f);
    setPlainValue (processor, dd::ParamIDs::mix, 0.0f);

    juce::AudioBuffer<float> audio (2, samples);
    juce::MidiBuffer midi;
    std::array<float, dd::SpectrumFIFO::numBins> analyzerInput {};
    std::array<float, dd::SpectrumFIFO::numBins> analyzerOutput {};
    auto analyzerFrames = 0;
    double analyzerPhase = 0.0;
    const auto blocks = dd::SpectrumFIFO::fftSize / samples + 24;
    for (int block = 0; block < blocks; ++block)
    {
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto value = 0.25f * static_cast<float> (
                std::sin (analyzerPhase));
            analyzerPhase += juce::MathConstants<double>::twoPi
                * 1000.0 / 48000.0;
            audio.setSample (0, sample, value);
            audio.setSample (1, sample, value);
        }
        processor.processBlock (audio, midi);
        if (processor.pullAnalyzerFrames (
            analyzerInput.data(), analyzerOutput.data(),
            static_cast<int> (analyzerInput.size())) == 3)
        {
            ++analyzerFrames;
        }
    }
    context.expect (analyzerFrames > 0,
                    "Analyzer triple buffer did not publish a frame");
    const auto inputPeak = std::max_element (
        analyzerInput.begin() + 1, analyzerInput.end());
    const auto outputPeak = std::max_element (
        analyzerOutput.begin() + 1, analyzerOutput.end());
    const auto inputPeakBin = static_cast<int> (
        std::distance (analyzerInput.begin(), inputPeak));
    const auto outputPeakBin = static_cast<int> (
        std::distance (analyzerOutput.begin(), outputPeak));
    context.expect (std::abs (inputPeakBin - outputPeakBin) <= 1,
                    "Analyzer input/output peak bins are not aligned");
    context.expect (std::abs (*inputPeak - *outputPeak) < 0.15f,
                    "Analyzer input/output peak levels are not aligned");
    const auto statistics = processor.getAnalyzerStatistics();
    context.expect (
        statistics.valid
            && std::abs (statistics.levelDeltaDb) < 0.2f,
        "Distortion analyzer statistics are invalid for unity dry signal");

    setPlainValue (processor, dd::ParamIDs::route, 1.0f);
    setPlainValue (processor, dd::ParamIDs::placement, -42.0f);
    setPlainValue (processor, dd::ParamIDs::dynamic, 73.0f);
    setPlainValue (processor, dd::ParamIDs::speed, 19.0f);
    setPlainValue (processor, dd::ParamIDs::inputHp, 123.0f);
    setPlainValue (processor, dd::ParamIDs::inputHpDetector, 1.0f);
    setPlainValue (processor, dd::ParamIDs::outputLp, 4321.0f);
    processor.setMultibandLinkedFromUi (false);
    auto multiband = processor.getCurrentMultibandParameters();
    for (const auto& band : multiband.bands)
    {
        const auto& values = band.saturation;
        context.expect (
            values.route == 1
                && std::abs (values.placementPercent + 42.0f) < 0.11f
                && std::abs (values.dynamicPercent - 73.0f) < 0.11f
                && std::abs (values.speedPercent - 19.0f) < 0.11f
                && std::abs (values.inputHpHz - 123.0f) < 0.11f
                && values.inputHpDetector
                && std::abs (values.outputLpHz - 4321.0f) < 1.1f,
            "Unlink did not copy the 0.9 master context to every band");
    }

    processor.setSelectedBand (1);
    setPlainValue (processor, dd::ParamIDs::band (1, "Route"), 0.0f);
    setPlainValue (processor, dd::ParamIDs::band (1, "Placement"), 88.0f);
    setPlainValue (processor, dd::ParamIDs::band (1, "Dynamic"), -64.0f);
    setPlainValue (processor, dd::ParamIDs::band (1, "Speed"), 91.0f);
    setPlainValue (processor, dd::ParamIDs::band (1, "InputHp"), 77.0f);
    setPlainValue (
        processor, dd::ParamIDs::band (1, "InputHpDetector"), 0.0f);
    setPlainValue (processor, dd::ParamIDs::band (1, "OutputLp"), 6789.0f);
    processor.setMultibandLinkedFromUi (true);
    const auto master = processor.getCurrentParameters();
    context.expect (
        master.route == 0
            && std::abs (master.placementPercent - 88.0f) < 0.11f
            && std::abs (master.dynamicPercent + 64.0f) < 0.11f
            && std::abs (master.speedPercent - 91.0f) < 0.11f
            && std::abs (master.inputHpHz - 77.0f) < 0.11f
            && ! master.inputHpDetector
            && std::abs (master.outputLpHz - 6789.0f) < 1.1f,
        "Link did not promote the selected 0.9 band context to master");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const auto originalTheme = default_family::ThemePreferences::load (true);
    const auto originalScale = default_family::EditorPreferences::loadScale();
    struct PreferenceRestorer
    {
        default_family::ThemeState theme;
        float scale = 1.0f;
        ~PreferenceRestorer()
        {
            default_family::ThemePreferences::save (theme);
            default_family::EditorPreferences::saveScale (scale);
        }
    } restorePreferences { originalTheme, originalScale };
    TestContext context;
    testManifestAndDefaults (context);
    testUpdatePolicyContract (context);
    testVersionFiveMigration (context);
    testBooleanStateCanonicalization (context);
    testLayoutContract (context);
    testThemeAndSettingsContract (context);
    testUtilityStripInteractionContract (context);
    testSmartGainAnimationContract (context);
    testSliderInteractionContract (context);
    testPopupLifecycleContract (context);
    testLatencyTransition (context);
    testAnalyzerTransportAndLinking (context);
    if (context.failures == 0)
        std::cout << "All plugin contract tests passed\n";
    return context.failures == 0 ? 0 : 1;
}
