#pragma once

#include "DistortionEngine.h"
#include "MultibandProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace dd
{
class DefaultDistortionAudioProcessor final
    : public juce::AudioProcessor,
      private juce::AudioProcessorValueTreeState::Listener
{
public:
    DefaultDistortionAudioProcessor();
    ~DefaultDistortionAudioProcessor() override;

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
    [[nodiscard]] MultibandParameters getCurrentMultibandParameters() const noexcept;
    void setSelectedBand (int band) noexcept;
    [[nodiscard]] int getSelectedBand() const noexcept;
    void setSoloBand (int band) noexcept;
    [[nodiscard]] int getSoloBand() const noexcept;
    void setAnalyzerEnabled (bool enabled) noexcept;
    void setMultibandLinkedFromUi (bool shouldLink);
    int pullAnalyzerSamples (float* inputDestination,
                             float* outputDestination,
                             int maximumSamples) noexcept;
    [[nodiscard]] float getSmartAutoGainProgress() const noexcept
    {
        return getCurrentMultibandParameters().enabled
            ? multibandEngine.getSmartAutoGainProgress()
            : engine.getSmartAutoGainProgress();
    }
    [[nodiscard]] bool isSmartAutoGainLocked() const noexcept
    {
        return getCurrentMultibandParameters().enabled
            ? multibandEngine.isSmartAutoGainLocked()
            : engine.isSmartAutoGainLocked();
    }

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static float calculatePeak (const juce::AudioBuffer<float>&) noexcept;

    DistortionEngine engine;
    MultibandProcessor multibandEngine;
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<int> selectedBand { 0 };
    std::atomic<int> soloBand { -1 };
    std::atomic<int> reportedLatency { 0 };
    std::atomic<bool> analyzerEnabled { false };

    static constexpr int analyzerCapacity = 32768;
    juce::AbstractFifo analyzerFifo { analyzerCapacity };
    std::array<float, analyzerCapacity> analyzerInput {};
    std::array<float, analyzerCapacity> analyzerOutput {};
    juce::AudioBuffer<float> analyzerInputBuffer;
    juce::AudioBuffer<float> analyzerInputDelayBuffer;
    int analyzerInputDelayPosition = 0;

    void pushAnalyzerSamples (const juce::AudioBuffer<float>& input,
                              const juce::AudioBuffer<float>& output) noexcept;
    void delayAnalyzerInput (juce::AudioBuffer<float>& input,
                             int latencySamples) noexcept;
    void copyMasterToAllBands (const Parameters& source);
    void copyBandToMasterAndAllBands (int sourceBand);
    void parameterChanged (const juce::String&, float) override;
    std::atomic<bool> handlingLinkTransition { false };
    std::atomic<bool> restoringState { false };
    std::atomic<bool> lastLinkedState { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DefaultDistortionAudioProcessor)
};
} // namespace dd
