#pragma once

#include <juce_core/juce_core.h>

namespace dd
{
namespace ParamIDs
{
inline constexpr auto mode = "mode";
inline constexpr auto drive = "drive";
inline constexpr auto character = "character";
inline constexpr auto wave = "wave";
inline constexpr auto tapeBias = "tapeBias";
inline constexpr auto transformerAirGap = "transformerAirGap";
inline constexpr auto downsampleJitter = "downsampleJitter";
inline constexpr auto bitCrusherDither = "bitCrusherDither";
inline constexpr auto schmittSlew = "schmittSlew";
inline constexpr auto asym = "asym";
inline constexpr auto asymStereo = "asymStereo";
inline constexpr auto tone = "tone";
inline constexpr auto stages = "stages";
inline constexpr auto mix = "mix";
inline constexpr auto output = "output";
inline constexpr auto quality = "quality";
inline constexpr auto autoGain = "autoGain";
} // namespace ParamIDs

struct Parameters
{
    int mode = 0;
    float driveDb = 0.0f;
    float character = 0.0f;
    float wave = 0.0f;
    float tapeBias = 0.5f;
    float transformerAirGap = 0.0f;
    float downsampleJitter = 0.0f;
    float bitCrusherDither = 0.0f;
    float schmittSlew = 0.0f;
    float asymmetry = 0.0f;
    bool asymmetryStereo = false;
    float tone = 0.0f;
    int stages = 1;
    float mix = 1.0f;
    float outputDb = 0.0f;
    int quality = 0;
    int autoGainMode = 1;
};
} // namespace dd
