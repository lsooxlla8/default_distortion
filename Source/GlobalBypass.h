#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace dd
{
class GlobalBypass
{
public:
    void prepare (double sampleRate,
                  int maximumBlockSize,
                  int channels,
                  int maximumLatencySamples,
                  bool initiallyEnabled)
    {
        maximumSamples = juce::jmax (1, maximumBlockSize);
        maximumChannels = juce::jmax (1, channels);
        dryInput.setSize (
            maximumChannels, maximumSamples, false, true, true);
        delayBuffer.setSize (
            maximumChannels,
            juce::jmax (1, maximumLatencySamples + maximumSamples + 1),
            false, true, true);
        wetMix.reset (juce::jmax (1.0, sampleRate), 0.01);
        reset (initiallyEnabled);
    }

    void reset (bool enabled)
    {
        dryInput.clear();
        delayBuffer.clear();
        delayPosition = 0;
        capturedChannels = 0;
        capturedSamples = 0;
        wetMix.setCurrentAndTargetValue (enabled ? 1.0f : 0.0f);
    }

    void captureInput (const juce::AudioBuffer<float>& input) noexcept
    {
        capturedChannels = juce::jmin (
            input.getNumChannels(), maximumChannels);
        capturedSamples = juce::jmin (
            input.getNumSamples(), maximumSamples);
        jassert (capturedChannels == input.getNumChannels());
        jassert (capturedSamples == input.getNumSamples());
        for (int channel = 0; channel < capturedChannels; ++channel)
            dryInput.copyFrom (
                channel, 0, input, channel, 0, capturedSamples);
    }

    [[nodiscard]] bool shouldProcessWet (bool enabled) const noexcept
    {
        return enabled || wetMix.isSmoothing() || wetMix.getCurrentValue() > 0.0f;
    }

    void processOutput (juce::AudioBuffer<float>& processed,
                        int latencySamples,
                        bool enabled) noexcept
    {
        const auto channels = juce::jmin (
            processed.getNumChannels(), capturedChannels);
        const auto samples = juce::jmin (
            processed.getNumSamples(), capturedSamples);
        const auto capacity = delayBuffer.getNumSamples();
        if (channels <= 0 || samples <= 0 || capacity <= 0)
            return;

        wetMix.setTargetValue (enabled ? 1.0f : 0.0f);
        const auto delay = juce::jlimit (
            0, capacity - maximumSamples - 1, latencySamples);
        for (int sample = 0; sample < samples; ++sample)
        {
            auto readPosition = delayPosition - delay;
            if (readPosition < 0)
                readPosition += capacity;
            const auto wet = wetMix.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto current = dryInput.getSample (channel, sample);
                const auto dry = delay == 0
                    ? current
                    : delayBuffer.getSample (channel, readPosition);
                delayBuffer.setSample (
                    channel, delayPosition, current);
                const auto effected = processed.getSample (channel, sample);
                processed.setSample (
                    channel, sample, dry + wet * (effected - dry));
            }
            delayPosition = (delayPosition + 1) % capacity;
        }
    }

private:
    juce::AudioBuffer<float> dryInput;
    juce::AudioBuffer<float> delayBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix;
    int maximumSamples = 1;
    int maximumChannels = 1;
    int capturedChannels = 0;
    int capturedSamples = 0;
    int delayPosition = 0;
};
} // namespace dd
