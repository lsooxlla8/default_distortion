#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace dd
{
namespace ParamIDs
{
inline constexpr auto mode = "mode";
inline constexpr auto drive = "drive";
inline constexpr auto character = "character";
inline constexpr auto secondary = "secondary";
inline constexpr auto asym = "asym";
inline constexpr auto asymStereo = "asymStereo";
inline constexpr auto tone = "tone";
inline constexpr auto stages = "stages";
inline constexpr auto mix = "mix";
inline constexpr auto output = "output";
inline constexpr auto quality = "quality";
inline constexpr auto autoGain = "autoGain";
inline constexpr auto pluginEnabled = "pluginEnabled";
inline constexpr auto multibandEnabled = "multibandEnabled";
inline constexpr auto multibandLink = "multibandLink";
inline constexpr auto multibandBandCount = "multibandBandCount";
inline constexpr auto multibandPhase = "multibandPhase";

inline juce::String crossoverFrequency (int crossover)
{
    return "crossover" + juce::String (crossover + 1) + "Frequency";
}

inline juce::String crossoverSlope (int crossover)
{
    return "crossover" + juce::String (crossover + 1) + "Slope";
}

inline juce::String band (int bandIndex, const char* suffix)
{
    return "band" + juce::String (bandIndex + 1) + suffix;
}
} // namespace ParamIDs

struct Parameters
{
    int mode = 0;
    float driveDb = 0.0f;
    float character = 0.0f;
    float secondary = 0.0f;
    float asymmetry = 0.0f;
    bool asymmetryStereo = false;
    float tone = 0.0f;
    int stages = 1;
    float mix = 1.0f;
    float outputDb = 0.0f;
    int quality = 0;
    int autoGainMode = 1;
};

struct BandParameters
{
    Parameters saturation;
    bool bypass = false;
    float trimDb = 0.0f;
};

struct MultibandParameters
{
    static constexpr int maximumBands = 4;
    static constexpr int maximumCrossovers = maximumBands - 1;

    bool enabled = false;
    bool linked = true;
    int bandCount = 4;
    int phaseMode = 0;
    std::array<float, maximumCrossovers> crossoverHz {
        100.0f, 500.0f, 2000.0f
    };
    std::array<int, maximumCrossovers> crossoverSlope {
        2, 2, 2
    };
    std::array<BandParameters, maximumBands> bands {};
};
} // namespace dd
