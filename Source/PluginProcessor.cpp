#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace dd
{
namespace
{
constexpr int currentStateSchemaVersion = 3;
constexpr auto stateSchemaProperty = "defaultDistortionStateSchema";

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
} // namespace

DefaultDistortionAudioProcessor::DefaultDistortionAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createLayout())
{
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

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::wave, 1 },
        "Wave",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.0f,
        juce::AudioParameterFloatAttributes {}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            })));

    const auto secondaryAttributes =
        juce::AudioParameterFloatAttributes {}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            });

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::tapeBias, 1 },
        "Tape Bias",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.5f,
        secondaryAttributes));
    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::transformerAirGap, 1 },
        "Transformer Air Gap",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.0f,
        secondaryAttributes));
    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::downsampleJitter, 1 },
        "Downsample Jitter",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.0f,
        secondaryAttributes));
    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::bitCrusherDither, 1 },
        "Bit Crusher Dither",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.0f,
        secondaryAttributes));
    layout.add (std::make_unique<Float> (
        juce::ParameterID { ParamIDs::schmittSlew, 1 },
        "Schmitt Slew",
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

    return layout;
}

void DefaultDistortionAudioProcessor::prepareToPlay (double newSampleRate,
                                                     int samplesPerBlock)
{
    engine.prepare (
        newSampleRate,
        samplesPerBlock,
        juce::jmax (1, getTotalNumOutputChannels()));
    // Prime deterministic compensation on the host setup thread. Subsequent
    // edits use the pre-generated table directly in the audio callback; no
    // programme measurement or background recalibration is involved.
    engine.primeAutoGain (getCurrentParameters());
    setLatencySamples (engine.getLatencySamples());
}

void DefaultDistortionAudioProcessor::releaseResources()
{
    engine.reset();
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

    inputPeak.store (calculatePeak (buffer), std::memory_order_relaxed);
    engine.process (buffer, getCurrentParameters());
    outputPeak.store (calculatePeak (buffer), std::memory_order_relaxed);
}

Parameters DefaultDistortionAudioProcessor::getCurrentParameters() const noexcept
{
    Parameters result;
    result.mode = juce::roundToInt (
        parameters.getRawParameterValue (ParamIDs::mode)->load());
    result.driveDb = parameters.getRawParameterValue (ParamIDs::drive)->load();
    result.character = parameters.getRawParameterValue (ParamIDs::character)->load();
    result.wave = parameters.getRawParameterValue (ParamIDs::wave)->load();
    result.tapeBias = parameters.getRawParameterValue (ParamIDs::tapeBias)->load();
    result.transformerAirGap = parameters.getRawParameterValue (
        ParamIDs::transformerAirGap)->load();
    result.downsampleJitter = parameters.getRawParameterValue (
        ParamIDs::downsampleJitter)->load();
    result.bitCrusherDither = parameters.getRawParameterValue (
        ParamIDs::bitCrusherDither)->load();
    result.schmittSlew = parameters.getRawParameterValue (
        ParamIDs::schmittSlew)->load();
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

            state.setProperty (
                stateSchemaProperty, currentStateSchemaVersion, nullptr);
            parameters.replaceState (state);
        }
}
} // namespace dd

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dd::DefaultDistortionAudioProcessor();
}
