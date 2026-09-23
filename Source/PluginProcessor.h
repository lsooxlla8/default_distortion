#pragma once

#include "DistortionEngine.h"
#include "GlobalBypass.h"
#include "MultibandProcessor.h"
#include "SpectrumFIFO.h"

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
    struct AnalyzerStatistics
    {
        float crestDeltaDb = 0.0f;
        float levelDeltaDb = 0.0f;
        bool valid = false;
    };
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

    [[nodiscard]] float getInputPeak (int channel) const noexcept
    {
        return inputChannelPeaks[static_cast<size_t> (
            juce::jlimit (0, 1, channel))].load (std::memory_order_relaxed);
    }

    [[nodiscard]] float getOutputPeak() const noexcept
    {
        return outputPeak.load (std::memory_order_relaxed);
    }

    [[nodiscard]] float getOutputPeak (int channel) const noexcept
    {
        return outputChannelPeaks[static_cast<size_t> (
            juce::jlimit (0, 1, channel))].load (std::memory_order_relaxed);
    }

    [[nodiscard]] Parameters getCurrentParameters() const noexcept;
    [[nodiscard]] MultibandParameters getCurrentMultibandParameters() const noexcept;
    void setSelectedBand (int band) noexcept;
    [[nodiscard]] int getSelectedBand() const noexcept;
    void setSoloBand (int band) noexcept;
    [[nodiscard]] int getSoloBand() const noexcept;
    void setAnalyzerEnabled (bool spectrumEnabled,
                             bool statisticsEnabled) noexcept;
    void setMeteringEnabled (bool enabled) noexcept;
    void setMultibandLinkedFromUi (bool shouldLink);
    int pullAnalyzerFrames (float* inputDestination,
                            float* outputDestination,
                            int maximumBins) noexcept;
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
    [[nodiscard]] float getSmartAutoGainDb() const noexcept
    {
        return getCurrentMultibandParameters().enabled
            ? multibandEngine.getSmartAutoGainDb()
            : engine.getSmartAutoGainDb();
    }
    [[nodiscard]] AnalyzerStatistics getAnalyzerStatistics() const noexcept;

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    DistortionEngine engine;
    MultibandProcessor multibandEngine;
    GlobalBypass globalBypass;
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, 2> inputChannelPeaks {};
    std::array<std::atomic<float>, 2> outputChannelPeaks {};
    std::atomic<int> selectedBand { 0 };
    std::atomic<int> soloBand { -1 };
    std::atomic<int> reportedLatency { 0 };
    std::atomic<bool> analyzerSpectrumEnabled { false };
    std::atomic<bool> analyzerStatisticsEnabled { false };
    std::atomic<bool> meteringEnabled { false };
    std::atomic<float> analyzerCrestDeltaDb { 0.0f };
    std::atomic<float> analyzerLevelDeltaDb { 0.0f };
    std::atomic<bool> analyzerStatisticsValid { false };
    float smoothedAnalyzerCrestDeltaDb = 0.0f;
    float smoothedAnalyzerLevelDeltaDb = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        latencyTransitionGain;

    SpectrumFIFO analyzerInputFifo;
    SpectrumFIFO analyzerOutputFifo;
    juce::AudioBuffer<float> analyzerInputBuffer;
    juce::AudioBuffer<float> analyzerInputDelayBuffer;
    int analyzerInputDelayPosition = 0;

    void delayAnalyzerInput (juce::AudioBuffer<float>& input,
                             int latencySamples) noexcept;
    void updateAnalyzerStatistics (
        const juce::AudioBuffer<float>& alignedInput,
        const juce::AudioBuffer<float>& output) noexcept;
    [[nodiscard]] int requestedLatencySamples (
        const Parameters&,
        const MultibandParameters&) const noexcept;
    void copyMasterToAllBands (const Parameters& source);
    void copyBandToMasterAndAllBands (int sourceBand);
    void parameterChanged (const juce::String&, float) override;
    std::atomic<bool> handlingLinkTransition { false };
    std::atomic<bool> restoringState { false };
    std::atomic<bool> lastLinkedState { true };
    Parameters processingMaster;
    MultibandParameters processingMultiband;
    bool latencyChangePending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DefaultDistortionAudioProcessor)
};
} // namespace dd
