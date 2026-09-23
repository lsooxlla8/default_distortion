#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

namespace default_family
{
struct ThemeState
{
    int mode = 0;
    juce::Colour lightBackground { 0xfff6f6f6 };
    juce::Colour lightForeground { 0xff050505 };
    juce::Colour darkBackground { 0xff050505 };
    juce::Colour darkForeground { 0xfff6f6f6 };

    bool operator== (const ThemeState& other) const noexcept
    {
        return mode == other.mode
            && lightBackground == other.lightBackground
            && lightForeground == other.lightForeground
            && darkBackground == other.darkBackground
            && darkForeground == other.darkForeground;
    }
    bool operator!= (const ThemeState& other) const noexcept
    {
        return ! (*this == other);
    }
};

class ThemePreferences final
{
public:
    enum Mode { automatic = 0, white = 1, black = 2 };

    static ThemeState load (bool reloadFromDisk = false);
    static void save (const ThemeState&);
    static int loadMode (bool reloadFromDisk = false);
    static void saveMode (int mode);
    static bool isDarkForHour (int mode, int localHour) noexcept;
};

class EditorPreferences final
{
public:
    static constexpr float defaultScale = 1.25f;
    static float loadScale();
    static void saveScale (float scale);
};
} // namespace default_family
