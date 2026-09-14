#include "../Source/PluginEditor.h"

#include <juce_graphics/juce_graphics.h>

#include <filesystem>
#include <cmath>
#include <iostream>
#include <memory>

namespace
{
void setPlainValue (dd::DefaultDistortionAudioProcessor& processor,
                    const juce::String& parameterId,
                    float value)
{
    if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
            processor.parameters.getParameter (parameterId)))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

juce::Button* findButton (juce::Component& component,
                          const juce::String& name)
{
    if (auto* button = dynamic_cast<juce::Button*> (&component))
        if (button->getName() == name || button->getButtonText() == name)
            return button;

    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* match = findButton (*component.getChildComponent (index), name))
            return match;
    return nullptr;
}

dd::MultibandPanel* findMultibandPanel (juce::Component& component)
{
    if (auto* panel = dynamic_cast<dd::MultibandPanel*> (&component))
        return panel;
    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* panel = findMultibandPanel (
                *component.getChildComponent (index)))
            return panel;
    return nullptr;
}

bool imageUsesLightPalette (const juce::Image& image, float scale)
{
    return image.getPixelAt (
        juce::jmin (juce::roundToInt (10.0f * scale), image.getWidth() - 1),
        juce::jmin (juce::roundToInt (70.0f * scale), image.getHeight() - 1))
        .getPerceivedBrightness() > 0.5f;
}

bool writePng (const juce::Image& image, const juce::File& destination)
{
    destination.getParentDirectory().createDirectory();
    destination.deleteFile();
    juce::FileOutputStream stream (destination);
    if (! stream.openedOk())
        return false;
    return juce::PNGImageFormat().writeImageToStream (image, stream);
}

bool capture (const juce::File& outputDirectory,
              const juce::String& name,
              bool expanded,
              bool light,
              int bandCount,
              bool linked,
              int mode,
              float imageScale,
              bool prototypeDefaults = false,
              bool interactionState = false,
              bool extremeValues = false,
              bool hoverCrossover = false)
{
    dd::DefaultDistortionAudioProcessor processor;
    setPlainValue (processor, dd::ParamIDs::multibandEnabled,
                   expanded ? 1.0f : 0.0f);
    setPlainValue (processor, dd::ParamIDs::multibandBandCount,
                   static_cast<float> (bandCount - 2));
    setPlainValue (processor, dd::ParamIDs::multibandLink,
                   linked ? 1.0f : 0.0f);
    setPlainValue (processor, dd::ParamIDs::mode,
                   static_cast<float> (mode));
    setPlainValue (processor, dd::ParamIDs::drive,
                   prototypeDefaults || extremeValues ? 36.0f : 18.0f);
    if (extremeValues)
    {
        setPlainValue (processor, dd::ParamIDs::character, 1.0f);
        setPlainValue (processor, dd::ParamIDs::secondary, 1.0f);
        setPlainValue (processor, dd::ParamIDs::asym, 1.0f);
        setPlainValue (processor, dd::ParamIDs::tone, 1.0f);
        setPlainValue (processor, dd::ParamIDs::stages, 8.0f);
        setPlainValue (processor, dd::ParamIDs::placement, 100.0f);
        setPlainValue (processor, dd::ParamIDs::dynamic, 100.0f);
        setPlainValue (processor, dd::ParamIDs::speed, 100.0f);
        setPlainValue (processor, dd::ParamIDs::inputHp, 200.0f);
        setPlainValue (processor, dd::ParamIDs::outputLp, 2000.0f);
        setPlainValue (processor, dd::ParamIDs::mix, 0.0f);
        setPlainValue (processor, dd::ParamIDs::output, 12.0f);
    }
    if (prototypeDefaults)
    {
        setPlainValue (processor, dd::ParamIDs::character, 0.5f);
        setPlainValue (processor, dd::ParamIDs::secondary, 0.0f);
        setPlainValue (processor, dd::ParamIDs::autoGain, 1.0f);
        constexpr float crossovers[] { 100.0f, 500.0f, 2000.0f };
        constexpr float trims[] { 0.0f, -1.2f, 0.8f, 0.0f };
        for (int index = 0; index < 3; ++index)
            setPlainValue (processor, dd::ParamIDs::crossoverFrequency (index),
                           crossovers[index]);
        for (int index = 0; index < 4; ++index)
        {
            setPlainValue (processor, dd::ParamIDs::band (index, "Trim"),
                           trims[index]);
            setPlainValue (processor, dd::ParamIDs::band (index, "Mode"),
                           static_cast<float> (mode));
            setPlainValue (processor, dd::ParamIDs::band (index, "Drive"),
                           36.0f);
            setPlainValue (processor, dd::ParamIDs::band (index, "Character"),
                           0.5f);
            setPlainValue (processor, dd::ParamIDs::band (index, "Speed"),
                           100.0f);
            setPlainValue (processor, dd::ParamIDs::band (index, "InputHp"),
                           0.0f);
            setPlainValue (processor, dd::ParamIDs::band (index, "OutputLp"),
                           20000.0f);
            if (interactionState)
                setPlainValue (
                    processor, dd::ParamIDs::band (index, "Route"), 1.0f);
        }
        if (interactionState)
            setPlainValue (processor, dd::ParamIDs::multibandPhase, 1.0f);
    }
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    const auto configured = processor.getCurrentMultibandParameters();
    if (configured.enabled != expanded
        || configured.bandCount != bandCount
        || configured.linked != linked)
        return false;

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
        return false;
    const auto afterEditor = processor.getCurrentMultibandParameters();
    if (afterEditor.enabled != expanded
        || afterEditor.bandCount != bandCount
        || afterEditor.linked != linked)
        return false;
    if (interactionState)
    {
        processor.setSoloBand (0);
        if (auto* solo = findButton (*editor, "Band 1 Solo"))
            solo->setToggleState (true, juce::dontSendNotification);
    }
    if (hoverCrossover)
    {
        auto* panel = findMultibandPanel (*editor);
        if (panel == nullptr)
            return false;
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto x = 4.0f
            + std::log (100.0f / 20.0f) / std::log (20000.0f / 20.0f)
                * 640.0f;
        const juce::MouseEvent hover (
            source, { x, 50.0f }, juce::ModifierKeys {},
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            panel, panel, now, { x, 50.0f }, now, 0, false);
        panel->mouseMove (hover);
    }

    auto image = editor->createComponentSnapshot (
        editor->getLocalBounds(), true, imageScale);
    if (imageUsesLightPalette (image, imageScale) != light)
    {
        auto* brand = findButton (*editor, "default_distortion");
        if (brand == nullptr)
            return false;
        if (brand->onClick == nullptr)
            return false;
        brand->onClick();
        image = editor->createComponentSnapshot (
            editor->getLocalBounds(), true, imageScale);
    }

    const auto expectedWidth = juce::roundToInt (648.0f * imageScale);
    const auto expectedHeight = juce::roundToInt (
        static_cast<float> (expanded ? 450 : 286) * imageScale);
    if (image.getWidth() != expectedWidth || image.getHeight() != expectedHeight)
        return false;

    return writePng (image, outputDirectory.getChildFile (name + ".png"));
}

bool capturePopup (const juce::File& outputDirectory,
                   const juce::String& name,
                   const juce::String& buttonName,
                   int expectedWidth,
                   int expectedHeight,
                   bool expanded)
{
    dd::DefaultDistortionAudioProcessor processor;
    setPlainValue (processor, dd::ParamIDs::multibandEnabled,
                   expanded ? 1.0f : 0.0f);
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
        return false;
    auto base = editor->createComponentSnapshot (
        editor->getLocalBounds(), true, 1.0f);
    if (! imageUsesLightPalette (base, 1.0f))
    {
        auto* brand = findButton (*editor, "default_distortion");
        if (brand == nullptr || brand->onClick == nullptr)
            return false;
        brand->onClick();
    }
    auto* button = findButton (*editor, buttonName);
    if (button == nullptr || button->onClick == nullptr)
        return false;
    button->onClick();

    auto& desktop = juce::Desktop::getInstance();
    for (int index = desktop.getNumComponents(); --index >= 0;)
    {
        auto* component = desktop.getComponent (index);
        if (component == nullptr || ! component->isVisible()
            || component->getWidth() != expectedWidth
            || component->getHeight() != expectedHeight)
            continue;
        const auto image = component->createComponentSnapshot (
            component->getLocalBounds(), true, 1.0f);
        return writePng (
            image, outputDirectory.getChildFile (name + ".png"));
    }
    return false;
}

bool captureSlopePopup (const juce::File& outputDirectory)
{
    dd::DefaultDistortionAudioProcessor processor;
    setPlainValue (processor, dd::ParamIDs::multibandEnabled, 1.0f);
    setPlainValue (processor, dd::ParamIDs::multibandBandCount, 2.0f);
    setPlainValue (processor, dd::ParamIDs::crossoverFrequency (0), 100.0f);
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
        return false;
    auto base = editor->createComponentSnapshot (
        editor->getLocalBounds(), true, 1.0f);
    if (! imageUsesLightPalette (base, 1.0f))
    {
        auto* brand = findButton (*editor, "default_distortion");
        if (brand == nullptr || brand->onClick == nullptr)
            return false;
        brand->onClick();
    }
    auto* panel = findMultibandPanel (*editor);
    if (panel == nullptr)
        return false;
    const auto x = 4.0f
        + std::log (100.0f / 20.0f) / std::log (20000.0f / 20.0f)
            * 640.0f;
    const auto position = juce::Point<float> { x, 14.0f };
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent hover (
        source, position, juce::ModifierKeys {},
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        panel, panel, now, position, now, 0, false);
    panel->mouseMove (hover);
    const juce::MouseEvent click (
        source, position, juce::ModifierKeys::leftButtonModifier,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        panel, panel, now, position, now, 1, false);
    panel->mouseDown (click);

    auto& desktop = juce::Desktop::getInstance();
    for (int index = desktop.getNumComponents(); --index >= 0;)
    {
        auto* component = desktop.getComponent (index);
        if (component == nullptr || ! component->isVisible()
            || component->getWidth() != 64 || component->getHeight() != 138)
            continue;
        return writePng (
            component->createComponentSnapshot (
                component->getLocalBounds(), true, 1.0f),
            outputDirectory.getChildFile ("popup-slope-light-1x.png"));
    }
    return false;
}

juce::String fileStemForMode (int displayPosition, int mode)
{
    auto name = dd::DistortionEngine::getModeNames()[static_cast<size_t> (mode)]
                    .toLowerCase()
                    .replaceCharacters (" /", "--");
    while (name.contains ("__"))
        name = name.replace ("__", "_");
    return "label-" + juce::String (displayPosition + 1).paddedLeft ('0', 2)
        + "-" + name;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiseGui;
    const auto outputDirectory = argc > 1
        ? juce::File (juce::String::fromUTF8 (argv[1]))
        : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-captures");

    struct Case
    {
        const char* name;
        bool expanded;
        bool light;
        int bands;
        bool linked;
        float scale;
    };
    constexpr Case cases[] {
        { "compact-light-1x", false, true, 4, true, 1.0f },
        { "compact-dark-1x", false, false, 4, true, 1.0f },
        { "expanded-2-linked-light-1x", true, true, 2, true, 1.0f },
        { "expanded-3-unlinked-light-1x", true, true, 3, false, 1.0f },
        { "expanded-4-linked-dark-1x", true, false, 4, true, 1.0f },
        { "expanded-4-unlinked-dark-2x", true, false, 4, false, 2.0f },
        { "expanded-4-unlinked-dark-3x", true, false, 4, false, 3.0f }
    };

    auto ok = true;
    for (const auto& item : cases)
        ok = capture (outputDirectory, item.name, item.expanded, item.light,
                      item.bands, item.linked, 0, item.scale) && ok;

    ok = capture (
        outputDirectory,
        "prototype-parity-expanded-light-1x",
        true,
        true,
        4,
        true,
        dd::DistortionEngine::getModeForDisplayPosition (9),
        1.0f,
        true) && ok;
    ok = capture (
        outputDirectory, "prototype-parity-compact-light-1x",
        false, true, 4, true,
        dd::DistortionEngine::getModeForDisplayPosition (9),
        1.0f, true) && ok;
    ok = capture (
        outputDirectory, "prototype-parity-compact-dark-1x",
        false, false, 4, true,
        dd::DistortionEngine::getModeForDisplayPosition (9),
        1.0f, true) && ok;
    ok = capture (
        outputDirectory, "prototype-parity-expanded-dark-1x",
        true, false, 4, true,
        dd::DistortionEngine::getModeForDisplayPosition (9),
        1.0f, true) && ok;
    ok = capture (
        outputDirectory, "prototype-parity-interaction-light-1x",
        true, true, 4, false,
        dd::DistortionEngine::getModeForDisplayPosition (9),
        1.0f, true, true) && ok;
    ok = capture (
        outputDirectory, "prototype-crossover-hover-light-1x",
        true, true, 4, true,
        dd::DistortionEngine::getModeForDisplayPosition (9),
        1.0f, true, false, false, true) && ok;

    ok = capturePopup (
        outputDirectory, "popup-mode-light-1x", "01  SOFT CLIP",
        640, 182, false) && ok;
    ok = capturePopup (
        outputDirectory, "popup-os-light-1x", "OS OFF",
        59, 112, false) && ok;
    ok = capturePopup (
        outputDirectory, "popup-phase-light-1x", "PHASE  MINIMUM",
        100, 60, true) && ok;
    ok = captureSlopePopup (outputDirectory) && ok;

    for (int displayPosition = 0;
         displayPosition < dd::DistortionEngine::modeCount;
         ++displayPosition)
    {
        const auto mode = dd::DistortionEngine::getModeForDisplayPosition (
            displayPosition);
        ok = capture (outputDirectory,
                      fileStemForMode (displayPosition, mode),
                      false, true, 4, true, mode, 1.0f) && ok;
        ok = capture (
            outputDirectory,
            "extreme-" + juce::String (displayPosition + 1).paddedLeft ('0', 2),
            false, true, 4, true, mode, 1.0f,
            false, false, true) && ok;
    }

    if (! ok)
    {
        std::cerr << "One or more UI reference captures failed\n";
        return 1;
    }
    std::cout << "Wrote UI render matrix to "
              << outputDirectory.getFullPathName() << '\n';
    return 0;
}
