#pragma once

#include "DistortionEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace dd
{
class DefaultDistortionAudioProcessor final : public juce::AudioProcessor
{
public:
    DefaultDistortionAudioProcessor();
    ~DefaultDistortionAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destinationData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] float getInputPeak() const noexcept
    {
        return inputPeak.load (std::memory_order_relaxed);
    }

    [[nodiscard]] float getOutputPeak() const noexcept
    {
        return outputPeak.load (std::memory_order_relaxed);
    }

    [[nodiscard]] Parameters getCurrentParameters() const noexcept;
    [[nodiscard]] float getSmartAutoGainProgress() const noexcept
    {
        return engine.getSmartAutoGainProgress();
    }
    [[nodiscard]] bool isSmartAutoGainLocked() const noexcept
    {
        return engine.isSmartAutoGainLocked();
    }

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static float calculatePeak (const juce::AudioBuffer<float>&) noexcept;

    DistortionEngine engine;
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DefaultDistortionAudioProcessor)
};
} // namespace dd
