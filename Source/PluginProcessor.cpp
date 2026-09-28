#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace dd
{
namespace
{
constexpr int currentStateSchemaVersion = 8;
constexpr auto stateSchemaProperty = "defaultDistortionStateSchema";
constexpr std::array<const char*, 3> crossoverFrequencyIds {
    "crossover1Frequency", "crossover2Frequency", "crossover3Frequency"
};
constexpr std::array<const char*, 3> crossoverSlopeIds {
    "crossover1Slope", "crossover2Slope", "crossover3Slope"
};
constexpr std::array<const char*, 4> bandModeIds {
    "band1Mode", "band2Mode", "band3Mode", "band4Mode"
};
constexpr std::array<const char*, 4> bandDriveIds {
    "band1Drive", "band2Drive", "band3Drive", "band4Drive"
};
constexpr std::array<const char*, 4> bandCharacterIds {
    "band1Character", "band2Character", "band3Character", "band4Character"
};
constexpr std::array<const char*, 4> bandSecondaryIds {
    "band1Secondary", "band2Secondary", "band3Secondary", "band4Secondary"
};
constexpr std::array<const char*, 4> bandAsymIds {
    "band1Asym", "band2Asym", "band3Asym", "band4Asym"
};
constexpr std::array<const char*, 4> bandAsymStereoIds {
    "band1AsymStereo", "band2AsymStereo", "band3AsymStereo", "band4AsymStereo"
};
constexpr std::array<const char*, 4> bandToneIds {
    "band1Tone", "band2Tone", "band3Tone", "band4Tone"
};
constexpr std::array<const char*, 4> bandStagesIds {
    "band1Stages", "band2Stages", "band3Stages", "band4Stages"
};
constexpr std::array<const char*, 4> bandMixIds {
    "band1Mix", "band2Mix", "band3Mix", "band4Mix"
};
constexpr std::array<const char*, 4> bandBypassIds {
    "band1Bypass", "band2Bypass", "band3Bypass", "band4Bypass"
};
constexpr std::array<const char*, 4> bandTrimIds {
    "band1Trim", "band2Trim", "band3Trim", "band4Trim"
};

int remapLegacyModeIndex (int oldIndex) noexcept
{
    // 0.5.1 and earlier exposed a different choice order. Keeping this map
    // preserves the selected algorithm in existing REAPER projects.
    constexpr std::array<int, DistortionEngine::modeCount> oldToNew {
        0, 1, 9, 10, 11, 12, 14, 16, 17, 18,
        19, 20, 6, 22, 15, 23, 21, 24, 2, 3,
        4, 5, 13, 25, 26, 29, 27, 28, 7, 8
    };
    return oldToNew[static_cast<size_t> (
        juce::jlimit (0, DistortionEngine::modeCount - 1, oldIndex))];
}

void migrateLegacyModeOrder (juce::ValueTree& state)
{
    for (auto child : state)
    {
        if (child.getProperty ("id").toString() != ParamIDs::mode)
            continue;

        const auto oldMode = static_cast<int> (
            static_cast<float> (child.getProperty ("value", 0.0f)));
        child.setProperty (
            "value", remapLegacyModeIndex (oldMode), nullptr);
        break;
    }
}

void setStateParameterValue (juce::ValueTree& state,
                             const juce::String& parameterId,
                             float value)
{
    for (auto child : state)
        if (child.getProperty ("id").toString() == parameterId)
        {
            child.setProperty ("value", value, nullptr);
            return;
        }
}

void initialiseVersionFourParameters (juce::ValueTree& state)
{
    setStateParameterValue (state, ParamIDs::multibandEnabled, 0.0f);
    setStateParameterValue (state, ParamIDs::multibandLink, 1.0f);
    setStateParameterValue (state, ParamIDs::multibandBandCount, 0.0f);
    setStateParameterValue (state, ParamIDs::multibandPhase, 0.0f);
    constexpr std::array<float, 3> frequencies { 120.0f, 1000.0f, 5000.0f };
    for (int crossover = 0; crossover < 3; ++crossover)
    {
        setStateParameterValue (
            state,
            ParamIDs::crossoverFrequency (crossover),
            frequencies[static_cast<size_t> (crossover)]);
        setStateParameterValue (
            state, ParamIDs::crossoverSlope (crossover), 2.0f);
    }
    for (int band = 0; band < 4; ++band)
    {
        for (const auto* suffix : {
                 "Mode", "Drive", "Character", "Secondary", "Asym",
                 "AsymStereo", "Tone", "Bypass", "Trim" })
            setStateParameterValue (
                state, ParamIDs::band (band, suffix), 0.0f);
        setStateParameterValue (
            state, ParamIDs::band (band, "Stages"), 1.0f);
        setStateParameterValue (
            state, ParamIDs::band (band, "Mix"), 1.0f);
    }
}

void initialiseVersionFiveParameters (juce::ValueTree& state)
{
    setStateParameterValue (state, ParamIDs::pluginEnabled, 1.0f);
}

void initialiseVersionSixParameters (juce::ValueTree& state)
{
    const auto initialise = [&state] (const juce::String& prefix)
    {
        setStateParameterValue (state, prefix + "Route", 0.0f);
        setStateParameterValue (state, prefix + "Placement", 0.0f);
        setStateParameterValue (state, prefix + "Dynamic", 0.0f);
        setStateParameterValue (state, prefix + "Speed", 100.0f);
        setStateParameterValue (state, prefix + "InputHp", 0.0f);
        setStateParameterValue (state, prefix + "OutputLp", 20000.0f);
    };

    setStateParameterValue (state, ParamIDs::route, 0.0f);
    setStateParameterValue (state, ParamIDs::placement, 0.0f);
    setStateParameterValue (state, ParamIDs::dynamic, 0.0f);
    setStateParameterValue (state, ParamIDs::speed, 100.0f);
    setStateParameterValue (state, ParamIDs::inputHp, 0.0f);
    setStateParameterValue (state, ParamIDs::outputLp, 20000.0f);
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
        initialise ("band" + juce::String (band + 1));
}

void initialiseVersionSevenParameters (juce::ValueTree& state)
{
    setStateParameterValue (state, ParamIDs::transientStrength, 100.0f);
    setStateParameterValue (state, ParamIDs::transientBalance, 0.0f);
    setStateParameterValue (state, ParamIDs::transientHold, 50.0f);
    setStateParameterValue (state, ParamIDs::transientSmooth, 50.0f);
}

void initialiseVersionEightParameters (juce::ValueTree& state)
{
    setStateParameterValue (state, ParamIDs::inputHpDetector, 0.0f);
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
        setStateParameterValue (
            state, ParamIDs::band (band, "InputHpDetector"), 0.0f);
}
} // namespace

DefaultDistortionAudioProcessor::DefaultDistortionAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createLayout())
{
    parameters.addParameterListener (
        ParamIDs::multibandLink, this);
}

DefaultDistortionAudioProcessor::~DefaultDistortionAudioProcessor()
{
    parameters.removeParameterListener (
        ParamIDs::multibandLink, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout
DefaultDistortionAudioProcessor::createLayout()
{
    using Choice = juce::AudioParameterChoice;
    using Bool = juce::AudioParameterBool;
    using Float = juce::AudioParameterFloat;
    using Int = juce::AudioParameterInt;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray modeNames;
    for (const auto& name : DistortionEngine::getModeNames())
        modeNames.add (name);

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ParamIDs::mode, 1 },
        "Mode",
        modeNames,
        0));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::drive, 1 },
        "Drive",
        juce::NormalisableRange<float> { 0.0f, 36.0f, 0.01f },
        0.0f,
        juce::AudioParameterFloatAttributes {}
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float value, int)
            {
                const auto clean = std::abs (value) < 0.005f ? 0.0f : value;
                return juce::String (clean, 1) + " dB";
            })));

    const auto bipolarAttributes =
        juce::AudioParameterFloatAttributes {}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            });

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::character, 1 },
        "Character",
        juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f },
        0.0f,
        bipolarAttributes));

    const auto secondaryAttributes =
        juce::AudioParameterFloatAttributes {}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            });

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::secondary, 1 },
        "Secondary",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.0f,
        secondaryAttributes));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::asym, 1 },
        "Asymmetry",
        juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f },
        0.0f,
        bipolarAttributes));

    layout.add (std::make_unique<Bool> (
        juce::ParameterID { ParamIDs::asymStereo, 1 },
        "Stereo Asymmetry",
        false));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::tone, 1 },
        "Tone",
        juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f },
        0.0f,
        bipolarAttributes));

    layout.add (std::make_unique<Int> (
        juce::ParameterID { ParamIDs::stages, 1 },
        "Stages",
        1,
        DistortionEngine::maximumStages,
        1,
        juce::AudioParameterIntAttributes {}
            .withStringFromValueFunction ([] (int value, int)
            {
                return juce::String (value) + " stage";
            })));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::mix, 1 },
        "Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        1.0f,
        juce::AudioParameterFloatAttributes {}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            })));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::output, 1 },
        "Output",
        juce::NormalisableRange<float> { -24.0f, 12.0f, 0.01f },
        0.0f,
        juce::AudioParameterFloatAttributes {}
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float value, int)
            {
                const auto clean = std::abs (value) < 0.005f ? 0.0f : value;
                return juce::String (clean, 1) + " dB";
            })));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ParamIDs::quality, 1 },
        "Oversampling",
        juce::StringArray { "OFF", "2x", "4x", "8x" },
        0));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ParamIDs::autoGain, 1 },
        "Auto Gain",
        juce::StringArray { "Off", "Auto Gain", "Smart Auto Gain" },
        1));

    layout.add (std::make_unique<Bool> (
        juce::ParameterID { ParamIDs::pluginEnabled, 1 },
        "Plugin Enabled",
        true));

    layout.add (std::make_unique<Bool> (
        juce::ParameterID { ParamIDs::multibandEnabled, 1 },
        "Multiband Enabled",
        false));
    layout.add (std::make_unique<Bool> (
        juce::ParameterID { ParamIDs::multibandLink, 1 },
        "Multiband Link",
        true,
        juce::AudioParameterBoolAttributes {}.withMeta (true)));
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ParamIDs::multibandBandCount, 1 },
        "Multiband Band Count",
        juce::StringArray { "2", "3", "4" },
        2));
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ParamIDs::multibandPhase, 1 },
        "Multiband Phase",
        juce::StringArray { "Minimum Phase", "Linear Phase" },
        0));

    constexpr std::array<float, MultibandParameters::maximumCrossovers>
        defaultCrossovers { 100.0f, 500.0f, 2000.0f };
    for (int crossover = 0;
         crossover < MultibandParameters::maximumCrossovers;
         ++crossover)
    {
        auto frequencyRange = juce::NormalisableRange<float> {
            20.0f, 20000.0f, 0.01f
        };
        frequencyRange.setSkewForCentre (1000.0f);
        layout.add (std::make_unique<Float> (
            juce::ParameterID {
                ParamIDs::crossoverFrequency (crossover), 1 },
            "Crossover " + juce::String (crossover + 1) + " Frequency",
            frequencyRange,
            defaultCrossovers[static_cast<size_t> (crossover)],
            juce::AudioParameterFloatAttributes {}
                .withLabel ("Hz")
                .withStringFromValueFunction ([] (float value, int)
                {
                    return value >= 1000.0f
                        ? juce::String (value / 1000.0f, 2) + " kHz"
                        : juce::String (value, 0) + " Hz";
                })));
        layout.add (std::make_unique<Choice> (
            juce::ParameterID { ParamIDs::crossoverSlope (crossover), 1 },
            "Crossover " + juce::String (crossover + 1) + " Slope",
            juce::StringArray { "6 dB/oct", "12 dB/oct", "24 dB/oct",
                                "36 dB/oct", "48 dB/oct" },
            2));
    }

    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
    {
        const auto prefix = "Band " + juce::String (band + 1) + " ";
        const auto id = [band] (const char* suffix)
        {
            return juce::ParameterID { ParamIDs::band (band, suffix), 1 };
        };
        layout.add (std::make_unique<Choice> (
            id ("Mode"), prefix + "Mode", modeNames, 0));
        layout.add (std::make_unique<Float> (
            id ("Drive"), prefix + "Drive",
            juce::NormalisableRange<float> { 0.0f, 36.0f, 0.01f }, 0.0f,
            juce::AudioParameterFloatAttributes {}
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float value, int)
                {
                    const auto clean = std::abs (value) < 0.005f ? 0.0f : value;
                    return juce::String (clean, 1) + " dB";
                })));
        layout.add (std::make_unique<Float> (
            id ("Character"), prefix + "Character",
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f,
            bipolarAttributes));
        layout.add (std::make_unique<Float> (
            id ("Secondary"), prefix + "Secondary",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f,
            secondaryAttributes));
        layout.add (std::make_unique<Float> (
            id ("Asym"), prefix + "Asymmetry",
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f,
            bipolarAttributes));
        layout.add (std::make_unique<Bool> (
            id ("AsymStereo"), prefix + "Stereo Asymmetry", false));
        layout.add (std::make_unique<Float> (
            id ("Tone"), prefix + "Tone",
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f,
            bipolarAttributes));
        layout.add (std::make_unique<Int> (
            id ("Stages"), prefix + "Stages",
            1, DistortionEngine::maximumStages, 1,
            juce::AudioParameterIntAttributes {}
                .withStringFromValueFunction ([] (int value, int)
                {
                    return juce::String (value) + " stage";
                })));
        layout.add (std::make_unique<Float> (
            id ("Mix"), prefix + "Mix",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f,
            juce::AudioParameterFloatAttributes {}
                .withLabel ("%")
                .withStringFromValueFunction ([] (float value, int)
                {
                    return juce::String (juce::roundToInt (value * 100.0f)) + "%";
                })));
        layout.add (std::make_unique<Bool> (
            id ("Bypass"), prefix + "Bypass", false));
        layout.add (std::make_unique<Float> (
            id ("Trim"), prefix + "Trim",
            juce::NormalisableRange<float> { -12.0f, 12.0f, 0.01f }, 0.0f,
            juce::AudioParameterFloatAttributes {}
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float value, int)
                {
                    const auto clean = std::abs (value) < 0.005f ? 0.0f : value;
                    return juce::String (clean, 1) + " dB";
                })));
    }

    // 0.9 parameters are deliberately appended after the complete 0.8
    // manifest. Existing host parameter indices and automation lanes must not
    // move when a session is opened with the new version.
    const auto percentAttributes = juce::AudioParameterFloatAttributes {}
        .withLabel ("%")
        .withStringFromValueFunction ([] (float value, int)
        {
            const auto rounded = juce::roundToInt (value);
            return juce::String (rounded > 0 ? "+" : "")
                + juce::String (rounded) + "%";
        });
    const auto speedAttributes = juce::AudioParameterFloatAttributes {}
        .withLabel ("%")
        .withStringFromValueFunction ([] (float value, int)
        {
            return juce::String (juce::roundToInt (value)) + "%";
        });
    auto inputHpRange = juce::NormalisableRange<float> {
        0.0f, 2000.0f,
        [] (float, float, float normalised)
        {
            if (normalised <= 0.0f)
                return 0.0f;
            return std::exp (normalised * std::log (2000.0f));
        },
        [] (float, float, float value)
        {
            if (value <= 0.0f)
                return 0.0f;
            return std::log (juce::jlimit (1.0f, 2000.0f, value))
                / std::log (2000.0f);
        },
        [] (float, float, float value)
        {
            return value <= 0.5f ? 0.0f
                                 : juce::jlimit (1.0f, 2000.0f, value);
        }};
    auto outputLpRange = juce::NormalisableRange<float> {
        2000.0f, 20000.0f, 1.0f
    };
    outputLpRange.setSkewForCentre (6324.555f);
    const auto hpAttributes = juce::AudioParameterFloatAttributes {}
        .withLabel ("Hz")
        .withStringFromValueFunction ([] (float value, int)
        {
            if (value <= 0.5f)
                return juce::String { "OFF" };
            return value >= 1000.0f
                ? juce::String (value / 1000.0f, 2) + " kHz"
                : juce::String (juce::roundToInt (value)) + " Hz";
        });
    const auto lpAttributes = juce::AudioParameterFloatAttributes {}
        .withLabel ("Hz")
        .withStringFromValueFunction ([] (float value, int)
        {
            if (value >= 19999.5f)
                return juce::String { "OFF" };
            return value >= 10000.0f
                ? juce::String (value / 1000.0f, 1) + " kHz"
                : juce::String (value / 1000.0f, 2) + " kHz";
        });

    const auto addNewContextParameters = [&] (const auto& id,
                                               const juce::String& prefix)
    {
        layout.add (std::make_unique<Choice> (
            id ("Route"), prefix + "Route",
            juce::StringArray { "M/S", "T/S" }, 0));
        layout.add (std::make_unique<Float> (
            id ("Placement"), prefix + "Placement",
            juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f },
            0.0f, percentAttributes));
        layout.add (std::make_unique<Float> (
            id ("Dynamic"), prefix + "Dynamic",
            juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f },
            0.0f, percentAttributes));
        layout.add (std::make_unique<Float> (
            id ("Speed"), prefix + "Speed",
            juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f },
            100.0f, speedAttributes));
        layout.add (std::make_unique<Float> (
            id ("InputHp"), prefix + "Input HP",
            inputHpRange, 0.0f, hpAttributes));
        layout.add (std::make_unique<Float> (
            id ("OutputLp"), prefix + "Output LP",
            outputLpRange, 20000.0f, lpAttributes));
    };

    addNewContextParameters (
        [] (const char* suffix)
        {
            if (juce::String (suffix) == "Route")
                return juce::ParameterID { ParamIDs::route, 1 };
            if (juce::String (suffix) == "Placement")
                return juce::ParameterID { ParamIDs::placement, 1 };
            if (juce::String (suffix) == "Dynamic")
                return juce::ParameterID { ParamIDs::dynamic, 1 };
            if (juce::String (suffix) == "Speed")
                return juce::ParameterID { ParamIDs::speed, 1 };
            if (juce::String (suffix) == "InputHp")
                return juce::ParameterID { ParamIDs::inputHp, 1 };
            return juce::ParameterID { ParamIDs::outputLp, 1 };
        },
        {});
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
        addNewContextParameters (
            [band] (const char* suffix)
            {
                return juce::ParameterID { ParamIDs::band (band, suffix), 1 };
            },
            "Band " + juce::String (band + 1) + " ");

    const auto splitterAttributes = juce::AudioParameterFloatAttributes {}
        .withLabel ("%")
        .withStringFromValueFunction ([] (float value, int)
        {
            return juce::String (juce::roundToInt (value)) + "%";
        });
    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::transientStrength, 1 },
        "T/S Strength",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f },
        100.0f,
        splitterAttributes));
    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::transientBalance, 1 },
        "T/S Balance",
        juce::NormalisableRange<float> { -50.0f, 50.0f, 0.1f },
        0.0f,
        juce::AudioParameterFloatAttributes {}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float value, int)
            {
                const auto rounded = juce::roundToInt (value);
                return juce::String (rounded > 0 ? "+" : "")
                    + juce::String (rounded) + "%";
            })));
    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::transientHold, 1 },
        "T/S Hold",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f },
        50.0f,
        splitterAttributes));
    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::transientSmooth, 1 },
        "T/S Smooth",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f },
        50.0f,
        splitterAttributes));

    // Added after the complete 0.9 manifest so existing parameter indices and
    // automation lanes remain stable.
    layout.add (std::make_unique<Bool> (
        juce::ParameterID { ParamIDs::inputHpDetector, 1 },
        "Input HP Detector",
        false));
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
        layout.add (std::make_unique<Bool> (
            juce::ParameterID {
                ParamIDs::band (band, "InputHpDetector"), 1 },
            "Band " + juce::String (band + 1) + " Input HP Detector",
            false));

    return layout;
}

void DefaultDistortionAudioProcessor::prepareToPlay (double newSampleRate,
                                                     int samplesPerBlock)
{
    engine.prepare (
        newSampleRate,
        samplesPerBlock,
        juce::jmax (1, getTotalNumOutputChannels()));
    multibandEngine.prepare (
        newSampleRate,
        samplesPerBlock,
        juce::jmax (1, getTotalNumOutputChannels()));
    analyzerInputBuffer.setSize (
        juce::jmax (1, getTotalNumOutputChannels()),
        juce::jmax (1, samplesPerBlock), false, false, true);
    const auto maximumProcessingLatency = juce::jmax (
        engine.getMaximumLatencySamples(),
        multibandEngine.getMaximumLatencySamples (true));
    analyzerInputDelayBuffer.setSize (
        juce::jmax (1, getTotalNumOutputChannels()),
        maximumProcessingLatency + juce::jmax (1, samplesPerBlock) + 1,
        false, true, true);
    analyzerInputDelayPosition = 0;
    analyzerInputFifo.reset();
    analyzerOutputFifo.reset();
    smoothedAnalyzerCrestDeltaDb = 0.0f;
    smoothedAnalyzerLevelDeltaDb = 0.0f;
    analyzerCrestDeltaDb.store (0.0f, std::memory_order_relaxed);
    analyzerLevelDeltaDb.store (0.0f, std::memory_order_relaxed);
    analyzerStatisticsValid.store (false, std::memory_order_relaxed);
    globalBypass.prepare (
        newSampleRate,
        samplesPerBlock,
        juce::jmax (1, getTotalNumOutputChannels()),
        maximumProcessingLatency,
        parameters.getRawParameterValue (ParamIDs::pluginEnabled)->load()
            >= 0.5f);
    latencyTransitionGain.reset (newSampleRate, 0.005);
    latencyTransitionGain.setCurrentAndTargetValue (1.0f);
    // Prime deterministic compensation on the host setup thread. Subsequent
    // edits use the pre-generated table directly in the audio callback; no
    // programme measurement or background recalibration is involved.
    engine.primeAutoGain (getCurrentParameters());
    processingMaster = getCurrentParameters();
    processingMultiband = getCurrentMultibandParameters();
    latencyChangePending = false;
    const auto latency = requestedLatencySamples (
        processingMaster, processingMultiband);
    reportedLatency.store (latency, std::memory_order_relaxed);
    setLatencySamples (latency);
}

void DefaultDistortionAudioProcessor::releaseResources()
{
    engine.reset();
    multibandEngine.reset();
    globalBypass.reset (
        parameters.getRawParameterValue (ParamIDs::pluginEnabled)->load()
            >= 0.5f);
    analyzerInputDelayBuffer.clear();
    analyzerInputDelayPosition = 0;
    analyzerInputFifo.reset();
    analyzerOutputFifo.reset();
}

bool DefaultDistortionAudioProcessor::isBusesLayoutSupported (
    const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono()
        && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void DefaultDistortionAudioProcessor::processBlock (
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels();
         channel < getTotalNumOutputChannels();
         ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    globalBypass.captureInput (buffer);

    const auto shouldAnalyzeSpectrum =
        analyzerSpectrumEnabled.load (std::memory_order_acquire);
    const auto shouldAnalyzeStatistics =
        analyzerStatisticsEnabled.load (std::memory_order_acquire);
    const auto shouldCaptureAnalyzerInput =
        shouldAnalyzeSpectrum || shouldAnalyzeStatistics;
    if (shouldCaptureAnalyzerInput)
    {
        analyzerInputBuffer.setSize (
            buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        analyzerInputBuffer.makeCopyOf (buffer, true);
    }
    const auto updateMeters = [] (
        const juce::AudioBuffer<float>& meterBuffer,
        std::atomic<float>& peak,
        std::array<std::atomic<float>, 2>& channelPeaks)
    {
        auto maximum = 0.0f;
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto channelPeak = meterBuffer.getMagnitude (
                juce::jmin (channel, meterBuffer.getNumChannels() - 1),
                0,
                meterBuffer.getNumSamples());
            channelPeaks[static_cast<size_t> (channel)].store (
                channelPeak, std::memory_order_relaxed);
            maximum = juce::jmax (maximum, channelPeak);
        }
        peak.store (maximum, std::memory_order_relaxed);
    };
    const auto shouldMeter = meteringEnabled.load (std::memory_order_acquire);
    if (shouldMeter)
        updateMeters (buffer, inputPeak, inputChannelPeaks);
    const auto requestedMaster = getCurrentParameters();
    const auto requestedMultiband = getCurrentMultibandParameters();
    const auto desiredLatency = requestedLatencySamples (
        requestedMaster, requestedMultiband);
    const auto currentLatency = reportedLatency.load (std::memory_order_relaxed);
    if (latencyChangePending
        && desiredLatency == currentLatency)
    {
        latencyChangePending = false;
        latencyTransitionGain.setTargetValue (1.0f);
        processingMaster = requestedMaster;
        processingMultiband = requestedMultiband;
    }
    else if (latencyChangePending
             && latencyTransitionGain.getCurrentValue() == 0.0f
             && ! latencyTransitionGain.isSmoothing())
    {
        processingMaster = requestedMaster;
        processingMultiband = requestedMultiband;
        reportedLatency.store (desiredLatency, std::memory_order_relaxed);
        setLatencySamples (desiredLatency);
        latencyChangePending = false;
        latencyTransitionGain.setTargetValue (1.0f);
    }
    else if (! latencyChangePending && desiredLatency != currentLatency)
    {
        latencyChangePending = true;
        latencyTransitionGain.setTargetValue (0.0f);
    }
    else if (! latencyChangePending)
    {
        processingMaster = requestedMaster;
        processingMultiband = requestedMultiband;
    }
    const auto& master = processingMaster;
    const auto& multiband = processingMultiband;
    const auto pluginEnabled =
        parameters.getRawParameterValue (ParamIDs::pluginEnabled)->load()
            >= 0.5f;
    if (globalBypass.shouldProcessWet (pluginEnabled))
    {
        if (multiband.enabled)
            multibandEngine.process (
                buffer,
                master,
                multiband,
                soloBand.load (std::memory_order_relaxed),
                nullptr);
        else
            engine.process (buffer, master);
    }
    const auto requiredLatency = reportedLatency.load (std::memory_order_relaxed);
    globalBypass.processOutput (
        buffer,
        requiredLatency,
        pluginEnabled);
    if (latencyTransitionGain.isSmoothing()
        || latencyTransitionGain.getCurrentValue() != 1.0f)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto guard = latencyTransitionGain.getNextValue();
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (
                    channel, sample, buffer.getSample (channel, sample) * guard);
        }
    if (shouldMeter)
        updateMeters (buffer, outputPeak, outputChannelPeaks);
    if (shouldCaptureAnalyzerInput)
    {
        delayAnalyzerInput (analyzerInputBuffer, requiredLatency);
        if (shouldAnalyzeStatistics)
            updateAnalyzerStatistics (analyzerInputBuffer, buffer);
        if (shouldAnalyzeSpectrum)
        {
            analyzerInputFifo.pushBlock (analyzerInputBuffer);
            analyzerOutputFifo.pushBlock (buffer);
        }
    }
}

Parameters DefaultDistortionAudioProcessor::getCurrentParameters() const noexcept
{
    Parameters result;
    result.mode = juce::roundToInt (
        parameters.getRawParameterValue (ParamIDs::mode)->load());
    result.driveDb = parameters.getRawParameterValue (ParamIDs::drive)->load();
    result.character = parameters.getRawParameterValue (ParamIDs::character)->load();
    result.secondary = parameters.getRawParameterValue (
        ParamIDs::secondary)->load();
    result.asymmetry = parameters.getRawParameterValue (ParamIDs::asym)->load();
    result.asymmetryStereo =
        parameters.getRawParameterValue (ParamIDs::asymStereo)->load() >= 0.5f;
    result.tone = parameters.getRawParameterValue (ParamIDs::tone)->load();
    result.stages = juce::roundToInt (
        parameters.getRawParameterValue (ParamIDs::stages)->load());
    result.mix = parameters.getRawParameterValue (ParamIDs::mix)->load();
    result.outputDb = parameters.getRawParameterValue (ParamIDs::output)->load();
    result.quality = juce::roundToInt (
        parameters.getRawParameterValue (ParamIDs::quality)->load());
    result.autoGainMode = juce::jlimit (
        0, 2, juce::roundToInt (
            parameters.getRawParameterValue (ParamIDs::autoGain)->load()));
    result.route = juce::jlimit (
        0, 1, juce::roundToInt (
            parameters.getRawParameterValue (ParamIDs::route)->load()));
    result.placementPercent = parameters.getRawParameterValue (
        ParamIDs::placement)->load();
    result.dynamicPercent = parameters.getRawParameterValue (
        ParamIDs::dynamic)->load();
    result.speedPercent = parameters.getRawParameterValue (
        ParamIDs::speed)->load();
    result.inputHpHz = parameters.getRawParameterValue (
        ParamIDs::inputHp)->load();
    result.inputHpDetector = parameters.getRawParameterValue (
        ParamIDs::inputHpDetector)->load() >= 0.5f;
    result.outputLpHz = parameters.getRawParameterValue (
        ParamIDs::outputLp)->load();
    result.transientStrengthPercent = parameters.getRawParameterValue (
        ParamIDs::transientStrength)->load();
    result.transientBalancePercent = parameters.getRawParameterValue (
        ParamIDs::transientBalance)->load();
    result.transientHoldPercent = parameters.getRawParameterValue (
        ParamIDs::transientHold)->load();
    result.transientSmoothPercent = parameters.getRawParameterValue (
        ParamIDs::transientSmooth)->load();
    return result;
}

MultibandParameters
DefaultDistortionAudioProcessor::getCurrentMultibandParameters() const noexcept
{
    MultibandParameters result;
    const auto read = [this] (const char* id)
    {
        if (const auto* value = parameters.getRawParameterValue (id))
            return value->load();
        return 0.0f;
    };
    result.enabled = read (ParamIDs::multibandEnabled) >= 0.5f;
    result.linked = read (ParamIDs::multibandLink) >= 0.5f;
    result.bandCount = juce::jlimit (
        2, 4, 2 + juce::roundToInt (read (ParamIDs::multibandBandCount)));
    result.phaseMode = juce::jlimit (
        0, 1, juce::roundToInt (read (ParamIDs::multibandPhase)));
    const auto transientStrength = read (ParamIDs::transientStrength);
    const auto transientBalance = read (ParamIDs::transientBalance);
    const auto transientHold = read (ParamIDs::transientHold);
    const auto transientSmooth = read (ParamIDs::transientSmooth);
    for (int crossover = 0;
         crossover < MultibandParameters::maximumCrossovers;
         ++crossover)
    {
        result.crossoverHz[static_cast<size_t> (crossover)] = read (
            crossoverFrequencyIds[static_cast<size_t> (crossover)]);
        result.crossoverSlope[static_cast<size_t> (crossover)] = juce::jlimit (
            0, 4, juce::roundToInt (read (
                crossoverSlopeIds[static_cast<size_t> (crossover)])));
    }
    constexpr auto minimumRatio = 1.2599210498948732f;
    const auto maximumHz = static_cast<float> (
        juce::jmin (20000.0, 0.45 * juce::jmax (1.0, getSampleRate())));
    for (int crossover = 0; crossover < result.bandCount - 1; ++crossover)
    {
        const auto lower = crossover == 0
            ? 20.0f
            : result.crossoverHz[static_cast<size_t> (crossover - 1)]
                * minimumRatio;
        const auto remaining = result.bandCount - 2 - crossover;
        const auto upper = maximumHz
            / std::pow (minimumRatio, static_cast<float> (remaining));
        result.crossoverHz[static_cast<size_t> (crossover)] = juce::jlimit (
            lower, upper,
            result.crossoverHz[static_cast<size_t> (crossover)]);
    }
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
    {
        auto& values = result.bands[static_cast<size_t> (band)];
        const auto index = static_cast<size_t> (band);
        values.saturation.mode = juce::jlimit (
            0, DistortionEngine::modeCount - 1,
            juce::roundToInt (read (bandModeIds[index])));
        values.saturation.driveDb = read (bandDriveIds[index]);
        values.saturation.character = read (bandCharacterIds[index]);
        values.saturation.secondary = read (bandSecondaryIds[index]);
        values.saturation.asymmetry = read (bandAsymIds[index]);
        values.saturation.asymmetryStereo = read (bandAsymStereoIds[index]) >= 0.5f;
        values.saturation.tone = read (bandToneIds[index]);
        values.saturation.stages = juce::jlimit (
            1, DistortionEngine::maximumStages,
            juce::roundToInt (read (bandStagesIds[index])));
        values.saturation.mix = read (bandMixIds[index]);
        const auto readNew = [this, band] (const char* suffix)
        {
            return parameters.getRawParameterValue (
                ParamIDs::band (band, suffix))->load();
        };
        values.saturation.route = juce::jlimit (
            0, 1, juce::roundToInt (readNew ("Route")));
        values.saturation.placementPercent = readNew ("Placement");
        values.saturation.dynamicPercent = readNew ("Dynamic");
        values.saturation.speedPercent = readNew ("Speed");
        values.saturation.inputHpHz = readNew ("InputHp");
        values.saturation.inputHpDetector =
            readNew ("InputHpDetector") >= 0.5f;
        values.saturation.outputLpHz = readNew ("OutputLp");
        values.saturation.transientStrengthPercent = transientStrength;
        values.saturation.transientBalancePercent = transientBalance;
        values.saturation.transientHoldPercent = transientHold;
        values.saturation.transientSmoothPercent = transientSmooth;
        values.bypass = read (bandBypassIds[index]) >= 0.5f;
        values.trimDb = read (bandTrimIds[index]);
    }
    return result;
}

int DefaultDistortionAudioProcessor::requestedLatencySamples (
    const Parameters& master,
    const MultibandParameters& multiband) const noexcept
{
    const auto usesTransientRouting = [] (const Parameters& values)
    {
        return values.route == 1
            && std::abs (values.placementPercent) >= 1.0e-7f;
    };
    if (! multiband.enabled)
        return usesTransientRouting (master)
            ? engine.getMaximumLatencySamples()
            : engine.getBaseLatencySamples();

    auto transientActive = false;
    if (multiband.linked)
        transientActive = usesTransientRouting (master);
    else
        for (int band = 0;
             band < juce::jlimit (2, MultibandParameters::maximumBands,
                                  multiband.bandCount);
             ++band)
            transientActive = transientActive
                || usesTransientRouting (
                    multiband.bands[static_cast<size_t> (band)].saturation);
    const auto linearPhase = multiband.phaseMode == 1;
    return transientActive
        ? multibandEngine.getMaximumLatencySamples (linearPhase)
        : multibandEngine.getBaseLatencySamples (linearPhase);
}

void DefaultDistortionAudioProcessor::setSelectedBand (int band) noexcept
{
    selectedBand.store (juce::jlimit (0, 3, band), std::memory_order_relaxed);
}

int DefaultDistortionAudioProcessor::getSelectedBand() const noexcept
{
    return selectedBand.load (std::memory_order_relaxed);
}

void DefaultDistortionAudioProcessor::setSoloBand (int band) noexcept
{
    soloBand.store (juce::jlimit (-1, 3, band), std::memory_order_relaxed);
}

int DefaultDistortionAudioProcessor::getSoloBand() const noexcept
{
    return soloBand.load (std::memory_order_relaxed);
}

void DefaultDistortionAudioProcessor::setAnalyzerEnabled (
    bool spectrumEnabled,
    bool statisticsEnabled) noexcept
{
    analyzerSpectrumEnabled.store (
        spectrumEnabled, std::memory_order_release);
    analyzerStatisticsEnabled.store (
        statisticsEnabled, std::memory_order_release);
    if (! statisticsEnabled)
        analyzerStatisticsValid.store (false, std::memory_order_relaxed);
}

void DefaultDistortionAudioProcessor::setMeteringEnabled (bool enabled) noexcept
{
    meteringEnabled.store (enabled, std::memory_order_release);
}

DefaultDistortionAudioProcessor::AnalyzerStatistics
DefaultDistortionAudioProcessor::getAnalyzerStatistics() const noexcept
{
    return {
        analyzerCrestDeltaDb.load (std::memory_order_relaxed),
        analyzerLevelDeltaDb.load (std::memory_order_relaxed),
        analyzerStatisticsValid.load (std::memory_order_relaxed)
    };
}

void DefaultDistortionAudioProcessor::updateAnalyzerStatistics (
    const juce::AudioBuffer<float>& alignedInput,
    const juce::AudioBuffer<float>& output) noexcept
{
    struct BlockValues
    {
        double sumSquares = 0.0;
        float peak = 0.0f;
        int values = 0;
    };

    const auto measure = [] (const juce::AudioBuffer<float>& buffer)
    {
        BlockValues result;
        const auto channels = juce::jmin (2, buffer.getNumChannels());
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto value = buffer.getSample (channel, sample);
                result.sumSquares += static_cast<double> (value) * value;
                result.peak = juce::jmax (result.peak, std::abs (value));
                ++result.values;
            }
        }
        return result;
    };

    const auto input = measure (alignedInput);
    const auto processed = measure (output);
    constexpr auto epsilon = 1.0e-20;
    if (input.values == 0 || processed.values == 0
        || input.sumSquares <= epsilon || processed.sumSquares <= epsilon)
    {
        analyzerStatisticsValid.store (false, std::memory_order_relaxed);
        return;
    }

    const auto meanSquare = [] (const BlockValues& value)
    {
        return value.sumSquares / static_cast<double> (juce::jmax (1, value.values));
    };
    const auto crestDb = [&] (const BlockValues& value)
    {
        return static_cast<float> (20.0 * std::log10 (
            juce::jmax (static_cast<double> (value.peak), 1.0e-10)
            / std::sqrt (juce::jmax (meanSquare (value), epsilon))));
    };
    const auto crestDelta = crestDb (processed) - crestDb (input);
    const auto levelDelta = static_cast<float> (10.0 * std::log10 (
        meanSquare (processed) / meanSquare (input)));
    const auto rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const auto seconds = static_cast<double> (output.getNumSamples()) / rate;
    const auto smoothing = static_cast<float> (1.0 - std::exp (-seconds / 0.30));
    const auto smooth = [smoothing] (float& current, float target)
    {
        current += smoothing * (target - current);
    };
    smooth (smoothedAnalyzerCrestDeltaDb, crestDelta);
    smooth (smoothedAnalyzerLevelDeltaDb, levelDelta);
    analyzerCrestDeltaDb.store (
        smoothedAnalyzerCrestDeltaDb, std::memory_order_relaxed);
    analyzerLevelDeltaDb.store (
        smoothedAnalyzerLevelDeltaDb, std::memory_order_relaxed);
    analyzerStatisticsValid.store (true, std::memory_order_relaxed);
}

void DefaultDistortionAudioProcessor::copyMasterToAllBands (
    const Parameters& source)
{
    const auto set = [this] (const juce::String& id, float plainValue)
    {
        if (auto* parameter = parameters.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (plainValue));
            parameter->endChangeGesture();
        }
    };
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
    {
        set (ParamIDs::band (band, "Mode"), static_cast<float> (source.mode));
        set (ParamIDs::band (band, "Drive"), source.driveDb);
        set (ParamIDs::band (band, "Character"), source.character);
        set (ParamIDs::band (band, "Secondary"), source.secondary);
        set (ParamIDs::band (band, "Asym"), source.asymmetry);
        set (ParamIDs::band (band, "AsymStereo"), source.asymmetryStereo ? 1.0f : 0.0f);
        set (ParamIDs::band (band, "Tone"), source.tone);
        set (ParamIDs::band (band, "Stages"), static_cast<float> (source.stages));
        set (ParamIDs::band (band, "Mix"), source.mix);
        set (ParamIDs::band (band, "Route"), static_cast<float> (source.route));
        set (ParamIDs::band (band, "Placement"), source.placementPercent);
        set (ParamIDs::band (band, "Dynamic"), source.dynamicPercent);
        set (ParamIDs::band (band, "Speed"), source.speedPercent);
        set (ParamIDs::band (band, "InputHp"), source.inputHpHz);
        set (ParamIDs::band (band, "InputHpDetector"),
             source.inputHpDetector ? 1.0f : 0.0f);
        set (ParamIDs::band (band, "OutputLp"), source.outputLpHz);
    }
}

void DefaultDistortionAudioProcessor::copyBandToMasterAndAllBands (
    int sourceBand)
{
    const auto multiband = getCurrentMultibandParameters();
    const auto& source = multiband.bands[static_cast<size_t> (
        juce::jlimit (0, 3, sourceBand))].saturation;
    const auto set = [this] (const juce::String& id, float plainValue)
    {
        if (auto* parameter = parameters.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (plainValue));
            parameter->endChangeGesture();
        }
    };
    set (ParamIDs::mode, static_cast<float> (source.mode));
    set (ParamIDs::drive, source.driveDb);
    set (ParamIDs::character, source.character);
    set (ParamIDs::secondary, source.secondary);
    set (ParamIDs::asym, source.asymmetry);
    set (ParamIDs::asymStereo, source.asymmetryStereo ? 1.0f : 0.0f);
    set (ParamIDs::tone, source.tone);
    set (ParamIDs::stages, static_cast<float> (source.stages));
    set (ParamIDs::mix, source.mix);
    set (ParamIDs::route, static_cast<float> (source.route));
    set (ParamIDs::placement, source.placementPercent);
    set (ParamIDs::dynamic, source.dynamicPercent);
    set (ParamIDs::speed, source.speedPercent);
    set (ParamIDs::inputHp, source.inputHpHz);
    set (ParamIDs::inputHpDetector,
         source.inputHpDetector ? 1.0f : 0.0f);
    set (ParamIDs::outputLp, source.outputLpHz);
    copyMasterToAllBands (source);
}

void DefaultDistortionAudioProcessor::setMultibandLinkedFromUi (
    bool shouldLink)
{
    const auto current = getCurrentMultibandParameters().linked;
    if (current == shouldLink)
        return;
    if (auto* parameter = parameters.getParameter (ParamIDs::multibandLink))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (shouldLink ? 1.0f : 0.0f);
        parameter->endChangeGesture();
    }
}

void DefaultDistortionAudioProcessor::parameterChanged (
    const juce::String& parameterId,
    float newValue)
{
    if (parameterId != ParamIDs::multibandLink
        || restoringState.load (std::memory_order_relaxed))
        return;
    const auto linked = newValue >= 0.5f;
    const auto previous = lastLinkedState.exchange (
        linked, std::memory_order_relaxed);
    if (linked == previous
        || handlingLinkTransition.exchange (true, std::memory_order_acq_rel))
        return;
    if (linked)
        copyBandToMasterAndAllBands (getSelectedBand());
    else
        copyMasterToAllBands (getCurrentParameters());
    handlingLinkTransition.store (false, std::memory_order_release);
}

void DefaultDistortionAudioProcessor::delayAnalyzerInput (
    juce::AudioBuffer<float>& input,
    int latencySamples) noexcept
{
    const auto capacity = analyzerInputDelayBuffer.getNumSamples();
    const auto channels = juce::jmin (
        input.getNumChannels(), analyzerInputDelayBuffer.getNumChannels());
    if (capacity <= 0 || channels <= 0)
        return;

    const auto delay = juce::jlimit (0, capacity - 1, latencySamples);
    for (int sample = 0; sample < input.getNumSamples(); ++sample)
    {
        auto readPosition = analyzerInputDelayPosition - delay;
        if (readPosition < 0)
            readPosition += capacity;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto current = input.getSample (channel, sample);
            const auto delayed = delay == 0
                ? current
                : analyzerInputDelayBuffer.getSample (channel, readPosition);
            analyzerInputDelayBuffer.setSample (
                channel, analyzerInputDelayPosition, current);
            input.setSample (channel, sample, delayed);
        }
        analyzerInputDelayPosition = (analyzerInputDelayPosition + 1) % capacity;
    }
}

int DefaultDistortionAudioProcessor::pullAnalyzerFrames (
    float* inputDestination,
    float* outputDestination,
    int maximumBins) noexcept
{
    const auto bins = juce::jlimit (
        0, SpectrumFIFO::numBins, maximumBins);
    auto ready = 0;
    if (analyzerInputFifo.processIfReady())
    {
        std::copy_n (analyzerInputFifo.getMagnitudes(), bins, inputDestination);
        ready |= 1;
    }
    if (analyzerOutputFifo.processIfReady())
    {
        std::copy_n (analyzerOutputFifo.getMagnitudes(), bins, outputDestination);
        ready |= 2;
    }
    return ready;
}

juce::AudioProcessorEditor* DefaultDistortionAudioProcessor::createEditor()
{
    return new DefaultDistortionAudioProcessorEditor (*this);
}

void DefaultDistortionAudioProcessor::getStateInformation (
    juce::MemoryBlock& destinationData)
{
    auto state = parameters.copyState();
    // AudioParameterBool keeps the last normalised host value internally even
    // though hosts observe a discrete 0/1 parameter. Persist the observable
    // boolean value so a state round-trip cannot restore an intermediate float
    // such as 0.16 and fail host state-restoration checks.
    for (auto child : state)
    {
        const auto parameterId = child.getProperty ("id").toString();
        if (const auto* boolean = dynamic_cast<const juce::AudioParameterBool*> (
                parameters.getParameter (parameterId)))
            child.setProperty (
                "value", boolean->get() ? 1.0f : 0.0f, nullptr);
    }
    state.setProperty (
        stateSchemaProperty, currentStateSchemaVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destinationData);
}

void DefaultDistortionAudioProcessor::setStateInformation (
    const void* data,
    int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            const auto schemaVersion = static_cast<int> (
                state.getProperty (stateSchemaProperty, 0));
            if (schemaVersion < 2)
                migrateLegacyModeOrder (state);

            if (schemaVersion < currentStateSchemaVersion)
            {
                const auto defaults = parameters.copyState();
                for (auto defaultChild : defaults)
                {
                    const auto id = defaultChild.getProperty ("id").toString();
                    auto found = false;
                    for (auto existingChild : state)
                        if (existingChild.getProperty ("id").toString() == id)
                        {
                            found = true;
                            break;
                        }
                    if (! found)
                        state.appendChild (defaultChild.createCopy(), nullptr);
                }
                if (schemaVersion < 4)
                    initialiseVersionFourParameters (state);
                if (schemaVersion < 5)
                    initialiseVersionFiveParameters (state);
                if (schemaVersion < 6)
                    initialiseVersionSixParameters (state);
                if (schemaVersion < 7)
                    initialiseVersionSevenParameters (state);
                if (schemaVersion < 8)
                    initialiseVersionEightParameters (state);
            }

            state.setProperty (
                stateSchemaProperty, currentStateSchemaVersion, nullptr);
            restoringState.store (true, std::memory_order_release);
            parameters.replaceState (state);
            // APVTS may see no ValueTree property change for a boolean whose
            // logical value stayed false/true, while AudioParameterBool still
            // holds an intermediate host-normalised float. Force the saved
            // discrete value back into the parameter object on same-instance
            // state restoration.
            for (auto child : state)
            {
                const auto parameterId = child.getProperty ("id").toString();
                if (auto* boolean = dynamic_cast<juce::AudioParameterBool*> (
                        parameters.getParameter (parameterId)))
                {
                    const auto saved = static_cast<float> (
                        child.getProperty ("value", 0.0f));
                    boolean->setValueNotifyingHost (saved >= 0.5f ? 1.0f : 0.0f);
                }
            }
            lastLinkedState.store (
                parameters.getRawParameterValue (ParamIDs::multibandLink)->load()
                    >= 0.5f,
                std::memory_order_relaxed);
            restoringState.store (false, std::memory_order_release);
        }
}
} // namespace dd

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dd::DefaultDistortionAudioProcessor();
}
