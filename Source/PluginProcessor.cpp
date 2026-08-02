#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace dd
{
namespace
{
constexpr int currentStateSchemaVersion = 4;
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
        0));
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ParamIDs::multibandPhase, 1 },
        "Multiband Phase",
        juce::StringArray { "Minimum Phase", "Linear Phase" },
        0));

    constexpr std::array<float, MultibandParameters::maximumCrossovers>
        defaultCrossovers { 120.0f, 1000.0f, 5000.0f };
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
    // Prime deterministic compensation on the host setup thread. Subsequent
    // edits use the pre-generated table directly in the audio callback; no
    // programme measurement or background recalibration is involved.
    engine.primeAutoGain (getCurrentParameters());
    const auto multiband = getCurrentMultibandParameters();
    const auto latency = multiband.enabled
        ? multibandEngine.getLatencySamples (multiband.phaseMode == 1)
        : engine.getLatencySamples();
    reportedLatency.store (latency, std::memory_order_relaxed);
    setLatencySamples (latency);
}

void DefaultDistortionAudioProcessor::releaseResources()
{
    engine.reset();
    multibandEngine.reset();
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

    const auto shouldAnalyze = analyzerEnabled.load (std::memory_order_relaxed);
    if (shouldAnalyze)
    {
        analyzerInputBuffer.setSize (
            buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        analyzerInputBuffer.makeCopyOf (buffer, true);
    }
    inputPeak.store (calculatePeak (buffer), std::memory_order_relaxed);
    const auto master = getCurrentParameters();
    const auto multiband = getCurrentMultibandParameters();
    if (multiband.enabled)
        multibandEngine.process (
            buffer,
            master,
            multiband,
            soloBand.load (std::memory_order_relaxed));
    else
        engine.process (buffer, master);
    const auto requiredLatency = multiband.enabled
        ? multibandEngine.getLatencySamples (multiband.phaseMode == 1)
        : engine.getLatencySamples();
    if (requiredLatency != reportedLatency.load (std::memory_order_relaxed))
    {
        reportedLatency.store (requiredLatency, std::memory_order_relaxed);
        setLatencySamples (requiredLatency);
    }
    outputPeak.store (calculatePeak (buffer), std::memory_order_relaxed);
    if (shouldAnalyze)
        pushAnalyzerSamples (analyzerInputBuffer, buffer);
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
        values.bypass = read (bandBypassIds[index]) >= 0.5f;
        values.trimDb = read (bandTrimIds[index]);
    }
    return result;
}

float DefaultDistortionAudioProcessor::calculatePeak (
    const juce::AudioBuffer<float>& buffer) noexcept
{
    auto peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax (
            peak,
            buffer.getMagnitude (channel, 0, buffer.getNumSamples()));
    return peak;
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

void DefaultDistortionAudioProcessor::setAnalyzerEnabled (bool enabled) noexcept
{
    analyzerEnabled.store (enabled, std::memory_order_relaxed);
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

void DefaultDistortionAudioProcessor::pushAnalyzerSamples (
    const juce::AudioBuffer<float>& input,
    const juce::AudioBuffer<float>& output) noexcept
{
    const auto samples = juce::jmin (input.getNumSamples(), output.getNumSamples());
    const auto writable = juce::jmin (samples, analyzerFifo.getFreeSpace());
    if (writable <= 0)
        return;
    const auto scope = analyzerFifo.write (writable);
    int source = 0;
    const std::array<int, 2> starts { scope.startIndex1, scope.startIndex2 };
    const std::array<int, 2> sizes { scope.blockSize1, scope.blockSize2 };
    for (size_t block = 0; block < starts.size(); ++block)
        for (int sample = 0; sample < sizes[block]; ++sample, ++source)
        {
            auto inputMono = 0.0f;
            auto outputMono = 0.0f;
            for (int channel = 0; channel < input.getNumChannels(); ++channel)
                inputMono += input.getSample (channel, source);
            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                outputMono += output.getSample (channel, source);
            inputMono /= static_cast<float> (juce::jmax (1, input.getNumChannels()));
            outputMono /= static_cast<float> (juce::jmax (1, output.getNumChannels()));
            const auto destination = starts[block] + sample;
            analyzerInput[static_cast<size_t> (destination)] = inputMono;
            analyzerOutput[static_cast<size_t> (destination)] = outputMono;
        }
}

int DefaultDistortionAudioProcessor::pullAnalyzerSamples (
    float* inputDestination,
    float* outputDestination,
    int maximumSamples) noexcept
{
    const auto readable = juce::jmin (
        juce::jmax (0, maximumSamples), analyzerFifo.getNumReady());
    if (readable <= 0)
        return 0;
    const auto scope = analyzerFifo.read (readable);
    int destination = 0;
    const std::array<int, 2> starts { scope.startIndex1, scope.startIndex2 };
    const std::array<int, 2> sizes { scope.blockSize1, scope.blockSize2 };
    for (size_t block = 0; block < starts.size(); ++block)
        for (int sample = 0; sample < sizes[block]; ++sample, ++destination)
        {
            const auto source = starts[block] + sample;
            inputDestination[destination] = analyzerInput[static_cast<size_t> (source)];
            outputDestination[destination] = analyzerOutput[static_cast<size_t> (source)];
        }
    return readable;
}

juce::AudioProcessorEditor* DefaultDistortionAudioProcessor::createEditor()
{
    return new DefaultDistortionAudioProcessorEditor (*this);
}

void DefaultDistortionAudioProcessor::getStateInformation (
    juce::MemoryBlock& destinationData)
{
    auto state = parameters.copyState();
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
                initialiseVersionFourParameters (state);
            }

            state.setProperty (
                stateSchemaProperty, currentStateSchemaVersion, nullptr);
            restoringState.store (true, std::memory_order_release);
            parameters.replaceState (state);
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
