#pragma once

// Transient/sustain separation adapted from ZLSplitter revision
// 2f50824ab925eeff7950986eac640dab43c3ce67 (AGPL-3.0-only).
// Copyright (C) 2026 zsliu98.
//
// The upstream KFR backend is replaced by JUCE FFT. The 75%-overlap Hann
// windows, 5x5 median masks, complementary reconstruction, and parameter
// transforms are retained from the audited default_eq 0.5.3 adaptation.

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace dd
{
class TransientSplitter
{
    struct Median5
    {
        std::array<float, 5> values {};
        int position = 0;

        void clear() noexcept
        {
            values.fill (0.0f);
            position = 0;
        }

        void insert (float value) noexcept
        {
            values[static_cast<size_t> (position)] = value;
            position = (position + 1) % static_cast<int> (values.size());
        }

        [[nodiscard]] float median() const noexcept
        {
            auto sorted = values;
            std::sort (sorted.begin(), sorted.end());
            return sorted[2];
        }
    };

    struct Channel
    {
        int order = 10;
        int fftSize = 1024;
        int hopSize = 256;
        int position = 0;
        int hopCounter = 0;
        int linePosition = 0;
        int delayPosition = 0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> input;
        std::vector<float> output;
        std::vector<float> pairedInput;
        std::vector<float> pairedOutput;
        std::vector<float> fftData;
        std::vector<float> pairedFftData;
        std::vector<float> magnitude;
        std::vector<float> mask;
        std::vector<float> window;
        std::vector<float> delay;
        std::vector<float> pairedDelay;
        std::array<std::vector<float>, 3> spectralLines;
        std::array<std::vector<float>, 3> pairedSpectralLines;
        std::vector<Median5> timeMedian;

        void prepare (int newOrder)
        {
            order = newOrder;
            fftSize = 1 << order;
            hopSize = fftSize / 4;
            position = hopCounter = linePosition = delayPosition = 0;
            fft = std::make_unique<juce::dsp::FFT> (order);
            input.assign (static_cast<size_t> (fftSize), 0.0f);
            output.assign (static_cast<size_t> (fftSize), 0.0f);
            pairedInput.assign (static_cast<size_t> (fftSize), 0.0f);
            pairedOutput.assign (static_cast<size_t> (fftSize), 0.0f);
            fftData.assign (static_cast<size_t> (2 * fftSize), 0.0f);
            pairedFftData.assign (static_cast<size_t> (2 * fftSize), 0.0f);
            magnitude.assign (static_cast<size_t> (fftSize / 2 + 1), 0.0f);
            mask.assign (magnitude.size(), 0.0f);
            window.resize (static_cast<size_t> (fftSize));
            for (int sample = 0; sample < fftSize; ++sample)
                window[static_cast<size_t> (sample)] = 0.5f - 0.5f * std::cos (
                    2.0f * juce::MathConstants<float>::pi
                    * static_cast<float> (sample)
                    / static_cast<float> (fftSize));
            for (auto& line : spectralLines)
                line.assign (static_cast<size_t> (2 * fftSize), 0.0f);
            for (auto& line : pairedSpectralLines)
                line.assign (static_cast<size_t> (2 * fftSize), 0.0f);
            timeMedian.assign (magnitude.size(), {});
            delay.assign (
                static_cast<size_t> (fftSize + 2 * hopSize + 1), 0.0f);
            pairedDelay.assign (delay.size(), 0.0f);
        }

        void reset() noexcept
        {
            std::fill (input.begin(), input.end(), 0.0f);
            std::fill (output.begin(), output.end(), 0.0f);
            std::fill (pairedInput.begin(), pairedInput.end(), 0.0f);
            std::fill (pairedOutput.begin(), pairedOutput.end(), 0.0f);
            std::fill (mask.begin(), mask.end(), 0.0f);
            std::fill (delay.begin(), delay.end(), 0.0f);
            std::fill (pairedDelay.begin(), pairedDelay.end(), 0.0f);
            for (auto& line : spectralLines)
                std::fill (line.begin(), line.end(), 0.0f);
            for (auto& line : pairedSpectralLines)
                std::fill (line.begin(), line.end(), 0.0f);
            for (auto& median : timeMedian)
                median.clear();
            position = hopCounter = linePosition = delayPosition = 0;
        }

        static float portion (float transient,
                              float sustain,
                              float balance,
                              float separation) noexcept
        {
            const auto weightedTransient = transient * balance;
            const auto transientPower = weightedTransient * weightedTransient;
            const auto sustainPower = sustain * sustain;
            const auto ratio = transientPower
                / std::max (transientPower + sustainPower, 1.0e-8f);
            return std::clamp (
                (ratio - 0.5f) * separation, -5.0f, 0.5f) + 0.5f;
        }

        void processFrame (bool hasPairedSource,
                           float balanceValue,
                           float separationValue,
                           float holdValue,
                           float smoothValue)
        {
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            for (int sample = 0; sample < fftSize; ++sample)
                fftData[static_cast<size_t> (sample)] =
                    input[static_cast<size_t> ((position + sample) % fftSize)]
                    * window[static_cast<size_t> (sample)];
            fft->performRealOnlyForwardTransform (fftData.data(), true);

            if (hasPairedSource)
            {
                std::fill (
                    pairedFftData.begin(), pairedFftData.end(), 0.0f);
                for (int sample = 0; sample < fftSize; ++sample)
                    pairedFftData[static_cast<size_t> (sample)] =
                        pairedInput[static_cast<size_t> (
                            (position + sample) % fftSize)]
                        * window[static_cast<size_t> (sample)];
                fft->performRealOnlyForwardTransform (
                    pairedFftData.data(), true);
            }

            const auto bins = fftSize / 2 + 1;
            magnitude[0] = std::abs (fftData[0]);
            magnitude[static_cast<size_t> (bins - 1)] = std::abs (fftData[1]);
            for (int bin = 1; bin < bins - 1; ++bin)
                magnitude[static_cast<size_t> (bin)] = std::hypot (
                    fftData[static_cast<size_t> (2 * bin)],
                    fftData[static_cast<size_t> (2 * bin + 1)]);

            Median5 frequencyMedian;
            frequencyMedian.clear();
            frequencyMedian.insert (magnitude[0]);
            frequencyMedian.insert (magnitude[0]);
            frequencyMedian.insert (magnitude[0]);
            frequencyMedian.insert (magnitude[static_cast<size_t> (
                std::min (1, bins - 1))]);
            for (int bin = 0; bin < bins; ++bin)
            {
                frequencyMedian.insert (magnitude[static_cast<size_t> (
                    std::min (bins - 1, bin + 2))]);
                timeMedian[static_cast<size_t> (bin)].insert (
                    magnitude[static_cast<size_t> (bin)]);
                const auto current = portion (
                    frequencyMedian.median(),
                    timeMedian[static_cast<size_t> (bin)].median(),
                    balanceValue,
                    separationValue);
                mask[static_cast<size_t> (bin)] = std::max (
                    mask[static_cast<size_t> (bin)] * holdValue, current);
            }

            spectralLines[static_cast<size_t> (linePosition)] = fftData;
            if (hasPairedSource)
                pairedSpectralLines[static_cast<size_t> (linePosition)] =
                    pairedFftData;
            linePosition = (linePosition + 1)
                % static_cast<int> (spectralLines.size());
            fftData = spectralLines[static_cast<size_t> (linePosition)];
            if (hasPairedSource)
                pairedFftData = pairedSpectralLines[
                    static_cast<size_t> (linePosition)];

            auto mean = 0.0f;
            for (const auto value : mask)
                mean += value;
            mean /= static_cast<float> (std::max (1, bins));
            mean = std::clamp (
                (mean - 0.5f) * std::sqrt (separationValue), -0.5f, 0.5f)
                    + 0.5f;
            const auto multiplierForBin = [&] (int bin)
            {
                return
                    (mean - mask[static_cast<size_t> (bin)]) * smoothValue
                    + mask[static_cast<size_t> (bin)];
            };
            const auto apply = [&] (std::vector<float>& data,
                                    int real,
                                    int imaginary,
                                    int bin)
            {
                const auto multiplier = multiplierForBin (bin);
                data[static_cast<size_t> (real)] *= multiplier;
                if (imaginary >= 0)
                    data[static_cast<size_t> (imaginary)] *= multiplier;
            };
            apply (fftData, 0, -1, 0);
            apply (fftData, 1, -1, bins - 1);
            for (int bin = 1; bin < bins - 1; ++bin)
                apply (fftData, 2 * bin, 2 * bin + 1, bin);

            fft->performRealOnlyInverseTransform (fftData.data());
            for (int sample = 0; sample < fftSize; ++sample)
                output[static_cast<size_t> ((position + sample) % fftSize)]
                    += fftData[static_cast<size_t> (sample)]
                        * window[static_cast<size_t> (sample)] * (2.0f / 3.0f);

            if (hasPairedSource)
            {
                apply (pairedFftData, 0, -1, 0);
                apply (pairedFftData, 1, -1, bins - 1);
                for (int bin = 1; bin < bins - 1; ++bin)
                    apply (pairedFftData, 2 * bin, 2 * bin + 1, bin);
                fft->performRealOnlyInverseTransform (pairedFftData.data());
                for (int sample = 0; sample < fftSize; ++sample)
                    pairedOutput[static_cast<size_t> (
                        (position + sample) % fftSize)]
                        += pairedFftData[static_cast<size_t> (sample)]
                            * window[static_cast<size_t> (sample)]
                            * (2.0f / 3.0f);
            }
        }

        void process (const float* reference,
                      const float* pairedSource,
                      float* transient,
                      float* sustain,
                      float* pairedTransient,
                      float* pairedSustain,
                      int samples,
                      float balanceValue,
                      float separationValue,
                      float holdValue,
                      float smoothValue)
        {
            const auto delaySize = static_cast<int> (delay.size());
            for (int sample = 0; sample < samples; ++sample)
            {
                input[static_cast<size_t> (position)] = reference[sample];
                transient[sample] = output[static_cast<size_t> (position)];
                output[static_cast<size_t> (position)] = 0.0f;
                const auto read = (delayPosition + 1) % delaySize;
                const auto delayed = delay[static_cast<size_t> (read)];
                delay[static_cast<size_t> (delayPosition)] = reference[sample];
                if (pairedSource != nullptr)
                {
                    pairedInput[static_cast<size_t> (position)] =
                        pairedSource[sample];
                    pairedTransient[sample] = pairedOutput[
                        static_cast<size_t> (position)];
                    pairedOutput[static_cast<size_t> (position)] = 0.0f;
                    const auto pairedDelayed = pairedDelay[
                        static_cast<size_t> (read)];
                    pairedDelay[static_cast<size_t> (delayPosition)] =
                        pairedSource[sample];
                    pairedSustain[sample] =
                        pairedDelayed - pairedTransient[sample];
                }
                delayPosition = read;
                sustain[sample] = delayed - transient[sample];
                position = (position + 1) % fftSize;
                if (++hopCounter == hopSize)
                {
                    hopCounter = 0;
                    processFrame (
                        pairedSource != nullptr,
                        balanceValue,
                        separationValue,
                        holdValue,
                        smoothValue);
                }
            }
        }
    };

public:
    void prepare (double sampleRate, int maximumBlockSize, int channels)
    {
        juce::ignoreUnused (maximumBlockSize);
        preparedChannels = juce::jlimit (1, 2, channels);
        const auto order = sampleRate <= 50000.0 ? 10
                         : sampleRate <= 100000.0 ? 11
                         : sampleRate <= 200000.0 ? 12 : 13;
        for (auto& channel : channelStates)
            channel.prepare (order);
        latencySamples = (1 << order) + 2 * ((1 << order) / 4);
    }

    void reset() noexcept
    {
        for (auto& channel : channelStates)
            channel.reset();
    }

    [[nodiscard]] int latency() const noexcept { return latencySamples; }

    void setParameters (float strengthPercent,
                        float balancePercent,
                        float holdPercent,
                        float smoothPercent) noexcept
    {
        separation = std::exp (
            std::clamp (strengthPercent, 0.0f, 100.0f) * 0.04f) - 1.0f;
        balance = std::pow (
            16.0f,
            std::clamp (balancePercent, -50.0f, 50.0f) * 0.01f - 0.25f);
        const auto holdValue = std::clamp (
            holdPercent, 0.0f, 100.0f) * 0.01f;
        hold = (32.0f - std::pow (32.0f, 1.0f - holdValue))
            / 31.0f * 0.75f + 0.24f;
        smooth = std::clamp (smoothPercent, 0.0f, 100.0f) * 0.01f;
    }

    void process (const juce::AudioBuffer<float>& input,
                  juce::AudioBuffer<float>& transient,
                  juce::AudioBuffer<float>& sustain,
                  int samples)
    {
        const auto channels = std::min (
            { preparedChannels,
              input.getNumChannels(),
              transient.getNumChannels(),
              sustain.getNumChannels() });
        for (int channel = 0; channel < channels; ++channel)
            channelStates[static_cast<size_t> (channel)].process (
                input.getReadPointer (channel),
                nullptr,
                transient.getWritePointer (channel),
                sustain.getWritePointer (channel),
                nullptr,
                nullptr,
                samples,
                balance,
                separation,
                hold,
                smooth);
    }

    // The mask is derived once from referenceInput and applied to both the
    // reference and paired signal. This keeps dry/wet decomposition exactly
    // complementary and prevents their transient masks from drifting apart.
    void processPair (const juce::AudioBuffer<float>& referenceInput,
                      const juce::AudioBuffer<float>& pairedInput,
                      juce::AudioBuffer<float>& referenceTransient,
                      juce::AudioBuffer<float>& referenceSustain,
                      juce::AudioBuffer<float>& pairedTransient,
                      juce::AudioBuffer<float>& pairedSustain,
                      int samples)
    {
        const auto channels = std::min (
            { preparedChannels,
              referenceInput.getNumChannels(),
              pairedInput.getNumChannels(),
              referenceTransient.getNumChannels(),
              referenceSustain.getNumChannels(),
              pairedTransient.getNumChannels(),
              pairedSustain.getNumChannels() });
        for (int channel = 0; channel < channels; ++channel)
            channelStates[static_cast<size_t> (channel)].process (
                referenceInput.getReadPointer (channel),
                pairedInput.getReadPointer (channel),
                referenceTransient.getWritePointer (channel),
                referenceSustain.getWritePointer (channel),
                pairedTransient.getWritePointer (channel),
                pairedSustain.getWritePointer (channel),
                samples,
                balance,
                separation,
                hold,
                smooth);
    }

private:
    std::array<Channel, 2> channelStates;
    int preparedChannels = 2;
    int latencySamples = 1536;
    float balance = 0.5f;
    float separation = std::exp (4.0f) - 1.0f;
    float hold = 0.9f;
    float smooth = 0.5f;
};
} // namespace dd
