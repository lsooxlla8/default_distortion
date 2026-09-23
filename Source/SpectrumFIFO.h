#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace dd
{
// Fixed 8192-point variant of the default_eq 0.5.3 analyzer transport.
// The audio thread and UI thread each own one slot and exchange only with the
// atomic middle slot. If the writer laps the reader, the older frame is
// replaced while the newest complete frame remains available to the UI.
class SpectrumFIFO
{
public:
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numBins = fftSize / 2;
    static constexpr int publishHop = 2048;
    static constexpr int numSlots = 3;
    static constexpr int maximumChannels = 2;

    SpectrumFIFO()
        : fft (fftOrder), storage (std::make_unique<Storage>())
    {
        for (int sample = 0; sample < fftSize; ++sample)
        {
            const auto phase = static_cast<float> (sample)
                / static_cast<float> (fftSize - 1);
            storage->hann[static_cast<size_t> (sample)] = 0.5f * (1.0f - std::cos (
                juce::MathConstants<float>::twoPi * phase));
        }

        constexpr auto octaveWidth = 1.0 / 24.0;
        const auto factor = std::pow (2.0, octaveWidth * 0.5);
        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto lower = std::clamp (
                static_cast<int> (std::floor (
                    static_cast<double> (bin) / factor)),
                0,
                numBins - 1);
            storage->smoothingLo[static_cast<size_t> (bin)] = lower;
            storage->smoothingHi[static_cast<size_t> (bin)] = std::clamp (
                static_cast<int> (std::ceil (
                    static_cast<double> (bin) * factor)) + 1,
                lower + 1,
                numBins);
        }
        reset();
    }

    void reset() noexcept
    {
        fifoWriteIndex = 0;
        samplesSincePublish = 0;
        writeSlot = 0;
        midSlot.store (1, std::memory_order_relaxed);
        readSlot = 2;
        fresh.store (false, std::memory_order_relaxed);
        for (auto& slot : storage->slots)
        {
            for (auto& channel : slot.samples)
                channel.fill (0.0f);
            slot.channels = 1;
        }
        for (auto& channel : storage->capture)
            channel.fill (0.0f);
        storage->outputMagnitudes.fill (-100.0f);
    }

    void pushBlock (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const auto channels = juce::jmin (
            maximumChannels, buffer.getNumChannels());
        const auto samples = buffer.getNumSamples();
        if (channels <= 0 || samples <= 0)
            return;

        auto index = fifoWriteIndex;
        for (int sample = 0; sample < samples; ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
                storage->capture[static_cast<size_t> (channel)]
                                [static_cast<size_t> (index)] =
                    buffer.getSample (channel, sample);
            if (++index >= fftSize)
                index = 0;
            if (++samplesSincePublish >= publishHop)
            {
                samplesSincePublish = 0;
                snapshotCapture (index, channels);
                writerFlip();
            }
        }
        fifoWriteIndex = index;
    }

    bool processIfReady() noexcept
    {
        if (! fresh.exchange (false, std::memory_order_acquire))
            return false;

        readSlot = midSlot.exchange (readSlot, std::memory_order_acquire);
        const auto& frame = storage->slots[static_cast<size_t> (readSlot)];
        storage->linearPower.fill (0.0f);
        const auto channelCount = juce::jlimit (
            1, maximumChannels, frame.channels);
        for (int channel = 0; channel < channelCount; ++channel)
        {
            storage->fftData.fill (0.0f);
            for (int sample = 0; sample < fftSize; ++sample)
                storage->fftData[static_cast<size_t> (sample)] =
                    frame.samples[static_cast<size_t> (channel)]
                                 [static_cast<size_t> (sample)]
                    * storage->hann[static_cast<size_t> (sample)];
            fft.performFrequencyOnlyForwardTransform (storage->fftData.data());
            for (int bin = 0; bin < numBins; ++bin)
            {
                const auto normalised =
                    storage->fftData[static_cast<size_t> (bin)]
                    * (4.0f / static_cast<float> (fftSize));
                storage->linearPower[static_cast<size_t> (bin)] +=
                    normalised * normalised
                    / static_cast<float> (channelCount);
            }
        }

        storage->cumulativePower[0] = 0.0;
        for (int bin = 0; bin < numBins; ++bin)
        {
            storage->cumulativePower[static_cast<size_t> (bin + 1)] =
                storage->cumulativePower[static_cast<size_t> (bin)]
                + storage->linearPower[static_cast<size_t> (bin)];
        }
        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto lower = storage->smoothingLo[static_cast<size_t> (bin)];
            const auto upper = storage->smoothingHi[static_cast<size_t> (bin)];
            const auto power = (
                storage->cumulativePower[static_cast<size_t> (upper)]
                - storage->cumulativePower[static_cast<size_t> (lower)])
                / static_cast<double> (upper - lower);
            storage->outputMagnitudes[static_cast<size_t> (bin)] =
                10.0f * std::log10 (static_cast<float> (
                    std::max (power, 1.0e-14)));
        }
        return true;
    }

    [[nodiscard]] const float* getMagnitudes() const noexcept
    {
        return storage->outputMagnitudes.data();
    }

private:
    void writerFlip() noexcept
    {
        writeSlot = midSlot.exchange (writeSlot, std::memory_order_release);
        fresh.store (true, std::memory_order_release);
    }

    void snapshotCapture (int oldestSample, int channels) noexcept
    {
        auto& frame = storage->slots[static_cast<size_t> (writeSlot)];
        const auto tailSamples = fftSize - oldestSample;
        frame.channels = juce::jlimit (1, maximumChannels, channels);
        for (int channel = 0; channel < frame.channels; ++channel)
        {
            const auto& source = storage->capture[static_cast<size_t> (channel)];
            auto& destination = frame.samples[static_cast<size_t> (channel)];
            std::copy_n (
                source.begin() + oldestSample,
                tailSamples,
                destination.begin());
            std::copy_n (
                source.begin(),
                oldestSample,
                destination.begin() + tailSamples);
        }
    }

    struct Storage
    {
        struct Frame
        {
            std::array<std::array<float, fftSize>, maximumChannels> samples {};
            int channels = 1;
        };
        std::array<Frame, numSlots> slots {};
        std::array<std::array<float, fftSize>, maximumChannels> capture {};
        std::array<float, fftSize * 2> fftData {};
        std::array<float, numBins> outputMagnitudes {};
        std::array<float, numBins> linearPower {};
        std::array<double, numBins + 1> cumulativePower {};
        std::array<float, fftSize> hann {};
        std::array<int, numBins> smoothingLo {};
        std::array<int, numBins> smoothingHi {};
    };

    juce::dsp::FFT fft;
    std::unique_ptr<Storage> storage;
    int fifoWriteIndex = 0;
    std::atomic<int> midSlot { 1 };
    std::atomic<bool> fresh { false };
    int samplesSincePublish = 0;
    int writeSlot = 0;
    int readSlot = 2;
};
} // namespace dd
