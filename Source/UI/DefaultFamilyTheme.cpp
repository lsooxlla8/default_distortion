#include "DefaultFamilyTheme.h"

namespace default_family
{
namespace
{
juce::InterProcessLock& preferencesLock()
{
    static juce::InterProcessLock lock { "icanseesounds-default-family-ui" };
    return lock;
}

juce::PropertiesFile::Options sharedOptions()
{
    juce::PropertiesFile::Options options;
    // This legacy filename is already shared by default_eq and default_allpass.
    // Keeping it preserves existing choices while making the schema common.
    options.applicationName = "default_distortion-ui";
    options.filenameSuffix = "settings";
    options.folderName = "icanseesounds";
    options.osxLibrarySubFolder = "Application Support";
    // Theme edits write several keys at once. Save explicitly after the group
    // instead of rewriting the file once per colour field.
    options.millisecondsBeforeSaving = -1;
    options.processLock = &preferencesLock();
    return options;
}

juce::PropertiesFile& sharedProperties()
{
    static juce::PropertiesFile properties { sharedOptions() };
    return properties;
}

juce::PropertiesFile& editorProperties()
{
    static juce::PropertiesFile properties ([]
    {
        auto options = sharedOptions();
        options.applicationName = "default_distortion-editor";
        return options;
    }());
    return properties;
}

juce::Colour loadColour (juce::PropertiesFile& properties,
                         const char* key,
                         juce::Colour fallback)
{
    return juce::Colour::fromString (
        properties.getValue (key, fallback.toString())).withAlpha (1.0f);
}
} // namespace

ThemeState ThemePreferences::load (bool reloadFromDisk)
{
    auto& properties = sharedProperties();
    if (reloadFromDisk)
        properties.reload();

    ThemeState result;
    result.mode = properties.containsKey ("themeMode")
        ? juce::jlimit ((int) automatic, (int) black,
                       properties.getIntValue ("themeMode", (int) automatic))
        : automatic;
    result.lightBackground = loadColour (
        properties, "lightBackgroundColour", result.lightBackground);
    result.lightForeground = loadColour (
        properties, "lightForegroundColour", result.lightForeground);
    result.darkBackground = loadColour (
        properties, "darkBackgroundColour", result.darkBackground);
    result.darkForeground = loadColour (
        properties, "darkForegroundColour", result.darkForeground);
    return result;
}

void ThemePreferences::save (const ThemeState& requested)
{
    auto state = requested;
    state.mode = juce::jlimit ((int) automatic, (int) black, state.mode);
    auto& properties = sharedProperties();
    // Another default_* binary can own a separate PropertiesFile instance.
    // Reload before the atomic group of writes so unrelated future keys survive.
    properties.reload();
    properties.setValue ("themeMode", state.mode);
    properties.setValue ("lightTheme", state.mode != black);
    properties.setValue (
        "lightBackgroundColour", state.lightBackground.toString());
    properties.setValue (
        "lightForegroundColour", state.lightForeground.toString());
    properties.setValue (
        "darkBackgroundColour", state.darkBackground.toString());
    properties.setValue (
        "darkForegroundColour", state.darkForeground.toString());
    properties.saveIfNeeded();
}

int ThemePreferences::loadMode (bool reloadFromDisk)
{
    return load (reloadFromDisk).mode;
}

void ThemePreferences::saveMode (int mode)
{
    auto state = load (true);
    state.mode = mode;
    save (state);
}

bool ThemePreferences::isDarkForHour (int mode, int localHour) noexcept
{
    mode = juce::jlimit ((int) automatic, (int) black, mode);
    if (mode == white)
        return false;
    if (mode == black)
        return true;
    localHour = juce::jlimit (0, 23, localHour);
    return localHour < 8 || localHour >= 20;
}

float EditorPreferences::loadScale()
{
    return juce::jlimit (
        1.0f, 3.0f,
        static_cast<float> (
            editorProperties().getDoubleValue (
                "editorScale", EditorPreferences::defaultScale)));
}

void EditorPreferences::saveScale (float scale)
{
    auto& properties = editorProperties();
    properties.setValue ("editorScale", juce::jlimit (1.0f, 3.0f, scale));
    properties.saveIfNeeded();
}
} // namespace default_family
