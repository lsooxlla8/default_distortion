#include "MultibandProcessor.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace dd
{
namespace
{
constexpr int maximumChannels = DistortionEngine::maximumChannels;
constexpr int maximumBands = MultibandParameters::maximumBands;
constexpr int maximumCrossovers = MultibandParameters::maximumCrossovers;
constexpr float minimumFrequency = 20.0f;
constexpr double linearPhaseSeconds = 2048.0 / 48000.0;

float maximumFrequency (double sampleRate) noexcept
{
    return static_cast<float> (juce::jmin (20000.0, 0.45 * sampleRate));
}

struct TptStage
{
    enum class Kind { onePole, stateVariable };

    Kind kind = Kind::onePole;
    bool highPass = false;
    float q = 0.70710678f;
    float g = 0.0f;
    float h = 1.0f;
    float r2 = 1.41421356f;
    std::array<float, maximumChannels> s1 {};
    std::array<float, maximumChannels> s2 {};

    void setCutoff (float frequency, double sampleRate) noexcept
    {
        g = std::tan (juce::MathConstants<float>::pi
                      * frequency / static_cast<float> (sampleRate));
        if (kind == Kind::onePole)
            h = g / (1.0f + g);
        else
        {
            r2 = 1.0f / juce::jmax (0.05f, q);
            h = 1.0f / (1.0f + r2 * g + g * g);
        }
    }

    float process (float input, int channel) noexcept
    {
        const auto index = static_cast<size_t> (channel);
        if (kind == Kind::onePole)
        {
            const auto v = (input - s1[index]) * h;
            const auto low = v + s1[index];
            s1[index] = low + v;
            return highPass ? input - low : low;
        }

        const auto high = (input - (r2 + g) * s1[index] - s2[index]) * h;
        const auto band = g * high + s1[index];
        s1[index] = g * high + band;
        const auto low = g * band + s2[index];
        s2[index] = g * band + low;
        return highPass ? high : low;
    }

    void reset() noexcept
    {
        s1.fill (0.0f);
        s2.fill (0.0f);
    }
};

struct TptPath
{
    std::array<TptStage, 8> stages {};
    int stageCount = 0;

    void configure (int slopeIndex, bool highPass)
    {
        stageCount = 0;
        const auto addOnePole = [&]
        {
            auto& stage = stages[static_cast<size_t> (stageCount++)];
            stage.kind = TptStage::Kind::onePole;
            stage.highPass = highPass;
            stage.q = 0.70710678f;
        };
        const auto addBiquad = [&] (float q)
        {
            auto& stage = stages[static_cast<size_t> (stageCount++)];
            stage.kind = TptStage::Kind::stateVariable;
            stage.highPass = highPass;
            stage.q = q;
        };

        if (slopeIndex <= 0)
        {
            addOnePole();
        }
        else
        {
            const auto butterworthOrder = slopeIndex == 1 ? 1
                : slopeIndex == 2 ? 2
                : slopeIndex == 3 ? 3
                : 4;
            for (int repeat = 0; repeat < 2; ++repeat)
            {
                if (butterworthOrder == 1)
                    addOnePole();
                else if (butterworthOrder == 2)
                    addBiquad (0.70710678f);
                else if (butterworthOrder == 3)
                {
                    addOnePole();
                    addBiquad (1.0f);
                }
                else
                {
                    addBiquad (0.54119610f);
                    addBiquad (1.30656296f);
                }
            }
        }

        reset();
    }

    void setCutoff (float frequency, double sampleRate) noexcept
    {
        for (int stage = 0; stage < stageCount; ++stage)
            stages[static_cast<size_t> (stage)].setCutoff (frequency, sampleRate);
    }

    float process (float input, int channel) noexcept
    {
        auto result = input;
        for (int stage = 0; stage < stageCount; ++stage)
            result = stages[static_cast<size_t> (stage)].process (result, channel);
        return result;
    }

    void reset() noexcept
    {
        for (auto& stage : stages)
            stage.reset();
    }
};

struct SplitFilter
{
    TptPath low;
    TptPath high;
    int slopeIndex = -1;
    float cutoff = 1000.0f;
    double sampleRate = 48000.0;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        configure (2, cutoff);
        reset();
    }

    void configure (int newSlopeIndex, float newCutoff)
    {
        newSlopeIndex = juce::jlimit (0, 4, newSlopeIndex);
        if (newSlopeIndex != slopeIndex)
        {
            slopeIndex = newSlopeIndex;
            low.configure (slopeIndex, false);
            high.configure (slopeIndex, true);
        }
        cutoff = juce::jlimit (
            minimumFrequency, maximumFrequency (sampleRate), newCutoff);
        low.setCutoff (cutoff, sampleRate);
        high.setCutoff (cutoff, sampleRate);
    }

    void processSample (float input,
                        int channel,
                        float& lowOutput,
                        float& highOutput) noexcept
    {
        lowOutput = low.process (input, channel);
        highOutput = high.process (input, channel);
        if (slopeIndex == 1 || slopeIndex == 3)
            highOutput = -highOutput;
    }

    float processAllpassSample (float input, int channel) noexcept
    {
        float lowOutput = 0.0f;
        float highOutput = 0.0f;
        processSample (input, channel, lowOutput, highOutput);
        return lowOutput + highOutput;
    }

    void reset() noexcept
    {
        low.reset();
        high.reset();
    }
};

struct MinimumPhaseBank
{
    double sampleRate = 48000.0;
    std::array<SplitFilter, maximumCrossovers> splits {};
    std::array<std::array<SplitFilter, maximumBands>, maximumCrossovers>
        compensators {};

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        for (auto& split : splits)
            split.prepare (sampleRate);
        for (auto& edge : compensators)
            for (auto& filter : edge)
                filter.prepare (sampleRate);
    }

    void reset()
    {
        for (auto& split : splits)
            split.reset();
        for (auto& edge : compensators)
            for (auto& filter : edge)
                filter.reset();
    }

    void process (const juce::AudioBuffer<float>& input,
                  std::array<juce::AudioBuffer<float>, maximumBands>& bands,
                  int bandCount,
                  const std::array<float, maximumCrossovers>& frequencies,
                  const std::array<int, maximumCrossovers>& slopes)
    {
        bandCount = juce::jlimit (2, maximumBands, bandCount);
        for (int edge = 0; edge < bandCount - 1; ++edge)
        {
            splits[static_cast<size_t> (edge)].configure (
                slopes[static_cast<size_t> (edge)],
                frequencies[static_cast<size_t> (edge)]);
            for (int band = 0; band < edge; ++band)
                compensators[static_cast<size_t> (edge)][static_cast<size_t> (band)]
                    .configure (slopes[static_cast<size_t> (edge)],
                                frequencies[static_cast<size_t> (edge)]);
        }

        const auto channels = input.getNumChannels();
        const auto samples = input.getNumSamples();
        for (auto& band : bands)
            band.clear();

        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < samples; ++sample)
            {
                auto remainder = input.getSample (channel, sample);
                for (int edge = 0; edge < bandCount - 1; ++edge)
                {
                    float low = 0.0f;
                    float high = 0.0f;
                    splits[static_cast<size_t> (edge)].processSample (
                        remainder, channel, low, high);
                    bands[static_cast<size_t> (edge)].setSample (
                        channel, sample, low);
                    remainder = high;
                }
                bands[static_cast<size_t> (bandCount - 1)].setSample (
                    channel, sample, remainder);
            }

        for (int edge = 1; edge < bandCount - 1; ++edge)
            for (int band = 0; band < edge; ++band)
                for (int channel = 0; channel < channels; ++channel)
                    for (int sample = 0; sample < samples; ++sample)
                    {
                        auto& buffer = bands[static_cast<size_t> (band)];
                        buffer.setSample (
                            channel,
                            sample,
                            compensators[static_cast<size_t> (edge)]
                                        [static_cast<size_t> (band)]
                                .processAllpassSample (
                                    buffer.getSample (channel, sample), channel));
                    }
    }
};

struct MinimumPhaseRouter
{
    std::array<MinimumPhaseBank, 2> banks;
    std::array<std::array<juce::AudioBuffer<float>, maximumBands>, 2> outputs;
    int activeBank = 0;
    int transitionBank = -1;
    int transitionRemaining = 0;
    int transitionLength = 480;
    int currentBandCount = 2;
    std::array<int, maximumCrossovers> currentSlopes { 2, 2, 2 };

    void prepare (double sampleRate, int maximumBlockSize, int channels)
    {
        for (auto& bank : banks)
            bank.prepare (sampleRate);
        for (auto& bank : outputs)
            for (auto& band : bank)
                band.setSize (channels, maximumBlockSize, false, false, true);
        transitionLength = juce::jmax (
            1, juce::roundToInt (sampleRate * 0.01));
        reset();
    }

    void reset()
    {
        for (auto& bank : banks)
            bank.reset();
        activeBank = 0;
        transitionBank = -1;
        transitionRemaining = 0;
        currentBandCount = 2;
        currentSlopes = { 2, 2, 2 };
    }

    void process (const juce::AudioBuffer<float>& input,
                  std::array<juce::AudioBuffer<float>, maximumBands>& result,
                  int bandCount,
                  const std::array<float, maximumCrossovers>& frequencies,
                  const std::array<int, maximumCrossovers>& slopes)
    {
        auto structuralChange = bandCount != currentBandCount;
        for (int edge = 0; edge < juce::jmax (bandCount, currentBandCount) - 1; ++edge)
            structuralChange = structuralChange
                || slopes[static_cast<size_t> (edge)]
                    != currentSlopes[static_cast<size_t> (edge)];
        if (structuralChange && transitionBank < 0)
        {
            transitionBank = 1 - activeBank;
            banks[static_cast<size_t> (transitionBank)].reset();
            transitionRemaining = transitionLength;
        }

        banks[static_cast<size_t> (activeBank)].process (
            input,
            outputs[static_cast<size_t> (activeBank)],
            currentBandCount,
            frequencies,
            currentSlopes);
        if (transitionBank >= 0)
            banks[static_cast<size_t> (transitionBank)].process (
                input,
                outputs[static_cast<size_t> (transitionBank)],
                bandCount,
                frequencies,
                slopes);

        for (auto& band : result)
            band.clear();
        const auto samples = input.getNumSamples();
        for (int channel = 0; channel < input.getNumChannels(); ++channel)
            for (int sample = 0; sample < samples; ++sample)
            {
                auto oldWeight = 1.0f;
                auto newWeight = 0.0f;
                if (transitionBank >= 0)
                {
                    const auto remaining = juce::jmax (
                        0, transitionRemaining - sample);
                    const auto amount = juce::jlimit (
                        0.0f, 1.0f,
                        1.0f - static_cast<float> (remaining)
                            / static_cast<float> (transitionLength));
                    oldWeight = std::cos (
                        amount * juce::MathConstants<float>::halfPi);
                    newWeight = std::sin (
                        amount * juce::MathConstants<float>::halfPi);
                    const auto normalise = juce::jmax (
                        1.0e-6f, oldWeight + newWeight);
                    oldWeight /= normalise;
                    newWeight /= normalise;
                }
                for (int band = 0; band < maximumBands; ++band)
                {
                    auto value = oldWeight
                        * outputs[static_cast<size_t> (activeBank)]
                              [static_cast<size_t> (band)]
                            .getSample (channel, sample);
                    if (transitionBank >= 0)
                        value += newWeight
                            * outputs[static_cast<size_t> (transitionBank)]
                                  [static_cast<size_t> (band)]
                                .getSample (channel, sample);
                    result[static_cast<size_t> (band)].setSample (
                        channel, sample, value);
                }
            }
        if (transitionBank >= 0)
        {
            transitionRemaining -= samples;
            if (transitionRemaining <= 0)
            {
                activeBank = transitionBank;
                transitionBank = -1;
                transitionRemaining = 0;
                currentBandCount = bandCount;
                currentSlopes = slopes;
            }
        }
    }
};

struct LinearPhaseBank : private juce::Thread
{
    struct KernelPackage
    {
        std::array<juce::AudioBuffer<float>, maximumCrossovers> impulses;
        std::array<float, maximumCrossovers> frequencies {};
        std::array<int, maximumCrossovers> slopes {};
    };

    LinearPhaseBank()
        : juce::Thread ("default_distortion linear crossover")
    {
        for (int edge = 0; edge < maximumCrossovers; ++edge)
        {
            requestedFrequencies[static_cast<size_t> (edge)].store (
                lastFrequencies[static_cast<size_t> (edge)]);
            requestedSlopes[static_cast<size_t> (edge)].store (
                lastSlopes[static_cast<size_t> (edge)]);
        }
    }

    ~LinearPhaseBank() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (2000);
    }

    double sampleRate = 48000.0;
    int channels = 2;
    int maximumBlockSize = 512;
    int groupDelay = 2048;
    int impulseLength = 4097;
    std::array<std::array<std::unique_ptr<juce::dsp::Convolution>,
                          maximumCrossovers>, 2> convolvers;
    std::array<std::array<juce::AudioBuffer<float>, maximumCrossovers>, 2>
        cumulative;
    std::array<std::vector<float>, maximumChannels> delayBuffers;
    std::array<int, maximumChannels> delayPositions {};
    std::array<float, maximumCrossovers> lastFrequencies {
        120.0f, 1000.0f, 5000.0f
    };
    std::array<int, maximumCrossovers> lastSlopes { 2, 2, 2 };
    std::array<std::atomic<float>, maximumCrossovers> requestedFrequencies;
    std::array<std::atomic<int>, maximumCrossovers> requestedSlopes;
    std::atomic<std::uint64_t> requestedGeneration { 0 };
    std::uint64_t generatedGeneration = 0;
    juce::SpinLock pendingLock;
    std::unique_ptr<KernelPackage> pendingKernels;
    int activeConvolutionBank = 0;
    int warmingConvolutionBank = -1;
    int warmupSamplesRemaining = 0;
    int crossfadeSamplesRemaining = 0;
    int crossfadeLength = 960;

    static juce::AudioBuffer<float> makeImpulse (double sampleRate,
                                                  int length,
                                                  float cutoff,
                                                  int slopeIndex)
    {
        auto fftOrder = 1;
        while ((1 << fftOrder) < length * 2)
            ++fftOrder;
        const auto fftSize = 1 << fftOrder;
        juce::dsp::FFT fft { fftOrder };
        std::vector<juce::dsp::Complex<float>> data (
            static_cast<size_t> (fftSize), { 0.0f, 0.0f });
        const auto exponent = static_cast<float> (
            MultibandProcessor::slopeDecibelsPerOctave (slopeIndex)) / 6.0f;
        for (int bin = 0; bin <= fftSize / 2; ++bin)
        {
            const auto frequency = static_cast<float> (bin) * static_cast<float> (sampleRate)
                / static_cast<float> (fftSize);
            const auto ratio = frequency / juce::jmax (1.0f, cutoff);
            const auto magnitude = 1.0f / (1.0f + std::pow (ratio, exponent));
            data[static_cast<size_t> (bin)] = { magnitude, 0.0f };
            if (bin > 0 && bin < fftSize / 2)
                data[static_cast<size_t> (fftSize - bin)] = {
                    magnitude, 0.0f
                };
        }
        fft.perform (data.data(), data.data(), true);

        juce::AudioBuffer<float> impulse (1, length);
        const auto centre = (length - 1) / 2;
        for (int tap = 0; tap < length; ++tap)
        {
            const auto source = (tap - centre + fftSize) % fftSize;
            const auto window = 0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::twoPi * static_cast<float> (tap)
                / static_cast<float> (length - 1));
            impulse.setSample (
                0, tap, data[static_cast<size_t> (source)].real() * window);
        }
        return impulse;
    }

    void prepare (double newSampleRate, int newMaximumBlockSize, int newChannels)
    {
        if (isThreadRunning())
        {
            signalThreadShouldExit();
            notify();
            stopThread (2000);
        }
        sampleRate = newSampleRate;
        maximumBlockSize = juce::jmax (1, newMaximumBlockSize);
        channels = juce::jlimit (1, maximumChannels, newChannels);
        groupDelay = juce::jmax (1, juce::roundToInt (sampleRate * linearPhaseSeconds));
        impulseLength = groupDelay * 2 + 1;

        juce::dsp::ProcessSpec spec {
            sampleRate,
            static_cast<juce::uint32> (maximumBlockSize),
            static_cast<juce::uint32> (channels)
        };
        for (int bank = 0; bank < 2; ++bank)
            for (int edge = 0; edge < maximumCrossovers; ++edge)
        {
            convolvers[static_cast<size_t> (bank)][static_cast<size_t> (edge)] =
                std::make_unique<juce::dsp::Convolution> (
                    juce::dsp::Convolution::NonUniform { 256 });
            convolvers[static_cast<size_t> (bank)][static_cast<size_t> (edge)]
                ->loadImpulseResponse (
                makeImpulse (sampleRate,
                             impulseLength,
                             lastFrequencies[static_cast<size_t> (edge)],
                             lastSlopes[static_cast<size_t> (edge)]),
                sampleRate,
                juce::dsp::Convolution::Stereo::no,
                juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);
            convolvers[static_cast<size_t> (bank)][static_cast<size_t> (edge)]
                ->prepare (spec);
            cumulative[static_cast<size_t> (bank)][static_cast<size_t> (edge)]
                .setSize (
                channels, maximumBlockSize, false, false, true);
        }
        const auto capacity = static_cast<size_t> (
            groupDelay + maximumBlockSize * 2 + 8);
        for (auto& delay : delayBuffers)
            delay.assign (capacity, 0.0f);
        delayPositions.fill (0);
        activeConvolutionBank = 0;
        warmingConvolutionBank = -1;
        warmupSamplesRemaining = 0;
        crossfadeSamplesRemaining = 0;
        crossfadeLength = juce::jmax (
            1, juce::roundToInt (sampleRate * 0.02));
        startThread (juce::Thread::Priority::low);
    }

    void reset()
    {
        for (auto& bank : convolvers)
            for (auto& convolution : bank)
                if (convolution != nullptr)
                    convolution->reset();
        for (auto& delay : delayBuffers)
            std::fill (delay.begin(), delay.end(), 0.0f);
        delayPositions.fill (0);
        activeConvolutionBank = 0;
        warmingConvolutionBank = -1;
        warmupSamplesRemaining = 0;
        crossfadeSamplesRemaining = 0;
    }

    float delaySample (float input, int channel) noexcept
    {
        auto& buffer = delayBuffers[static_cast<size_t> (channel)];
        auto& position = delayPositions[static_cast<size_t> (channel)];
        const auto read = (position - groupDelay + static_cast<int> (buffer.size()))
            % static_cast<int> (buffer.size());
        const auto output = buffer[static_cast<size_t> (read)];
        buffer[static_cast<size_t> (position)] = input;
        position = (position + 1) % static_cast<int> (buffer.size());
        return output;
    }

    void updateKernels (const std::array<float, maximumCrossovers>& frequencies,
                        const std::array<int, maximumCrossovers>& slopes)
    {
        auto changed = false;
        for (int edge = 0; edge < maximumCrossovers; ++edge)
        {
            const auto index = static_cast<size_t> (edge);
            if (std::abs (frequencies[index]
                          - requestedFrequencies[index].load (
                              std::memory_order_relaxed)) < 0.05f
                && slopes[index] == requestedSlopes[index].load (
                    std::memory_order_relaxed))
                continue;
            requestedFrequencies[index].store (
                frequencies[index], std::memory_order_relaxed);
            requestedSlopes[index].store (
                slopes[index], std::memory_order_relaxed);
            changed = true;
        }
        if (changed)
        {
            requestedGeneration.fetch_add (1, std::memory_order_release);
            notify();
        }
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (-1);
            if (threadShouldExit())
                break;
            const auto generation = requestedGeneration.load (
                std::memory_order_acquire);
            if (generation == generatedGeneration)
                continue;
            auto package = std::make_unique<KernelPackage>();
            for (int edge = 0; edge < maximumCrossovers; ++edge)
            {
                const auto index = static_cast<size_t> (edge);
                package->frequencies[index] = requestedFrequencies[index].load (
                    std::memory_order_relaxed);
                package->slopes[index] = requestedSlopes[index].load (
                    std::memory_order_relaxed);
                package->impulses[index] = makeImpulse (
                    sampleRate,
                    impulseLength,
                    package->frequencies[index],
                    package->slopes[index]);
            }
            {
                const juce::SpinLock::ScopedLockType lock (pendingLock);
                pendingKernels = std::move (package);
            }
            generatedGeneration = generation;
        }
    }

    void applyPendingKernels()
    {
        if (warmingConvolutionBank >= 0
            || crossfadeSamplesRemaining > 0)
            return;
        std::unique_ptr<KernelPackage> package;
        {
            const juce::SpinLock::ScopedTryLockType lock (pendingLock);
            if (! lock.isLocked() || pendingKernels == nullptr)
                return;
            package = std::move (pendingKernels);
        }
        warmingConvolutionBank = 1 - activeConvolutionBank;
        for (int edge = 0; edge < maximumCrossovers; ++edge)
        {
            const auto index = static_cast<size_t> (edge);
            convolvers[static_cast<size_t> (warmingConvolutionBank)][index]
                ->loadImpulseResponse (
                std::move (package->impulses[index]),
                sampleRate,
                juce::dsp::Convolution::Stereo::no,
                juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);
            lastFrequencies[index] = package->frequencies[index];
            lastSlopes[index] = package->slopes[index];
        }
        warmupSamplesRemaining = impulseLength
            + juce::roundToInt (sampleRate * 0.055);
    }

    void process (const juce::AudioBuffer<float>& input,
                  std::array<juce::AudioBuffer<float>, maximumBands>& bands,
                  int bandCount,
                  const std::array<float, maximumCrossovers>& frequencies,
                  const std::array<int, maximumCrossovers>& slopes)
    {
        updateKernels (frequencies, slopes);
        applyPendingKernels();
        const auto samples = input.getNumSamples();
        const auto activeChannels = input.getNumChannels();
        const auto processBank = [&] (int bank)
        {
            for (int edge = 0; edge < maximumCrossovers; ++edge)
            {
                auto& output = cumulative[static_cast<size_t> (bank)]
                    [static_cast<size_t> (edge)];
                output.setSize (activeChannels, samples, false, false, true);
                juce::dsp::AudioBlock<const float> inputBlock (input);
                juce::dsp::AudioBlock<float> outputBlock (output);
                juce::dsp::ProcessContextNonReplacing<float> context (
                    inputBlock, outputBlock);
                convolvers[static_cast<size_t> (bank)]
                    [static_cast<size_t> (edge)]->process (context);
            }
        };
        processBank (activeConvolutionBank);
        if (warmingConvolutionBank >= 0)
            processBank (warmingConvolutionBank);

        for (auto& band : bands)
            band.clear();
        bandCount = juce::jlimit (2, maximumBands, bandCount);
        const auto crossfading = crossfadeSamplesRemaining > 0
            && warmingConvolutionBank >= 0;
        const auto crossfadeStart = crossfadeSamplesRemaining;
        for (int channel = 0; channel < activeChannels; ++channel)
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto delayed = delaySample (
                    input.getSample (channel, sample), channel);
                auto previous = 0.0f;
                for (int edge = 0; edge < bandCount - 1; ++edge)
                {
                    const auto oldLow = cumulative[
                        static_cast<size_t> (activeConvolutionBank)]
                        [static_cast<size_t> (edge)].getSample (channel, sample);
                    auto low = oldLow;
                    if (crossfading)
                    {
                        const auto remaining = juce::jmax (
                            0, crossfadeStart - sample);
                        const auto amount = 1.0f
                            - static_cast<float> (remaining)
                                / static_cast<float> (crossfadeLength);
                        const auto newLow = cumulative[
                            static_cast<size_t> (warmingConvolutionBank)]
                            [static_cast<size_t> (edge)].getSample (channel, sample);
                        low = oldLow + juce::jlimit (0.0f, 1.0f, amount)
                            * (newLow - oldLow);
                    }
                    bands[static_cast<size_t> (edge)].setSample (
                        channel, sample, low - previous);
                    previous = low;
                }
                bands[static_cast<size_t> (bandCount - 1)].setSample (
                    channel, sample, delayed - previous);
            }

        if (warmingConvolutionBank >= 0 && warmupSamplesRemaining > 0)
        {
            warmupSamplesRemaining -= samples;
            if (warmupSamplesRemaining <= 0)
                crossfadeSamplesRemaining = crossfadeLength;
        }
        else if (crossfadeSamplesRemaining > 0)
        {
            crossfadeSamplesRemaining -= samples;
            if (crossfadeSamplesRemaining <= 0)
            {
                activeConvolutionBank = warmingConvolutionBank;
                warmingConvolutionBank = -1;
                crossfadeSamplesRemaining = 0;
            }
        }
    }
};

struct DelayBank
{
    std::array<std::vector<float>, maximumChannels> buffers;
    std::array<int, maximumChannels> positions {};

    void prepare (int capacity)
    {
        for (auto& buffer : buffers)
            buffer.assign (static_cast<size_t> (capacity), 0.0f);
        positions.fill (0);
    }

    void reset()
    {
        for (auto& buffer : buffers)
            std::fill (buffer.begin(), buffer.end(), 0.0f);
        positions.fill (0);
    }

    float process (float input, int channel, int delaySamples) noexcept
    {
        auto& buffer = buffers[static_cast<size_t> (channel)];
        auto& position = positions[static_cast<size_t> (channel)];
        const auto read = (position - delaySamples + static_cast<int> (buffer.size()))
            % static_cast<int> (buffer.size());
        const auto output = buffer[static_cast<size_t> (read)];
        buffer[static_cast<size_t> (position)] = input;
        position = (position + 1) % static_cast<int> (buffer.size());
        return output;
    }
};

std::uint64_t hashParameters (const Parameters& master,
                              const MultibandParameters& multiband) noexcept
{
    auto hash = UINT64_C (1469598103934665603);
    const auto add = [&hash] (std::uint32_t value)
    {
        hash ^= value;
        hash *= UINT64_C (1099511628211);
    };
    add (static_cast<std::uint32_t> (multiband.linked));
    add (static_cast<std::uint32_t> (multiband.bandCount));
    add (static_cast<std::uint32_t> (multiband.phaseMode));
    add (static_cast<std::uint32_t> (master.quality));
    for (int edge = 0; edge < multiband.bandCount - 1; ++edge)
    {
        add (std::bit_cast<std::uint32_t> (
            multiband.crossoverHz[static_cast<size_t> (edge)]));
        add (static_cast<std::uint32_t> (
            multiband.crossoverSlope[static_cast<size_t> (edge)]));
    }
    for (int band = 0; band < multiband.bandCount; ++band)
    {
        auto values = multiband.bands[static_cast<size_t> (band)];
        if (multiband.linked)
            values.saturation = master;
        add (static_cast<std::uint32_t> (values.saturation.mode));
        add (std::bit_cast<std::uint32_t> (values.saturation.driveDb));
        add (std::bit_cast<std::uint32_t> (values.saturation.character));
        add (std::bit_cast<std::uint32_t> (values.saturation.secondary));
        add (std::bit_cast<std::uint32_t> (values.saturation.asymmetry));
        add (std::bit_cast<std::uint32_t> (values.saturation.tone));
        add (static_cast<std::uint32_t> (values.saturation.stages));
        add (std::bit_cast<std::uint32_t> (values.saturation.mix));
        add (static_cast<std::uint32_t> (values.bypass));
        add (std::bit_cast<std::uint32_t> (values.trimDb));
    }
    return hash;
}
} // namespace

struct MultibandProcessor::Impl
{
    struct KWeighting
    {
        juce::IIRFilter shelf;
        juce::IIRFilter highPass;

        float process (float sample) noexcept
        {
            return highPass.processSingleSampleRaw (
                shelf.processSingleSampleRaw (sample));
        }

        void reset()
        {
            shelf.reset();
            highPass.reset();
        }
    };

    double sampleRate = 48000.0;
    int maximumBlockSize = 512;
    int channels = 2;
    int bandLatency = 0;
    MinimumPhaseRouter minimumBank;
    LinearPhaseBank linearBank;
    std::array<DistortionEngine, maximumBands> engines;
    std::array<juce::AudioBuffer<float>, maximumBands> bands;
    juce::AudioBuffer<float> dryReference;
    DelayBank dryDelay;
    std::array<KWeighting, maximumChannels> dryWeighting;
    std::array<KWeighting, maximumChannels> wetWeighting;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smartGain;
    double dryEnergy = 0.0;
    double wetEnergy = 0.0;
    double wetPeak = 0.0;
    int settleSamples = 0;
    int measuredSamples = 0;
    std::uint64_t lastSignature = 0;
    bool smartLocked = false;
    bool wasSmart = false;
    bool wasSolo = false;
    std::atomic<float> smartProgress { 0.0f };
    std::atomic<bool> smartLockedForUi { false };

    void prepareWeighting()
    {
        const auto shelf = juce::IIRCoefficients::makeHighShelf (
            sampleRate,
            1681.974450955533,
            0.7071752369554196f,
            juce::Decibels::decibelsToGain (3.999843853973347f));
        const auto highPass = juce::IIRCoefficients::makeHighPass (
            sampleRate, 38.13547087602444, 0.5003270373238773f);
        for (auto* bank : { &dryWeighting, &wetWeighting })
            for (auto& filter : *bank)
            {
                filter.shelf.setCoefficients (shelf);
                filter.highPass.setCoefficients (highPass);
                filter.reset();
            }
    }

    void restartSmartMeasurement (bool preserveGain)
    {
        dryEnergy = 0.0;
        wetEnergy = 0.0;
        wetPeak = 0.0;
        settleSamples = 0;
        measuredSamples = 0;
        smartLocked = false;
        smartProgress.store (0.0f, std::memory_order_relaxed);
        smartLockedForUi.store (false, std::memory_order_relaxed);
        for (auto& filter : dryWeighting)
            filter.reset();
        for (auto& filter : wetWeighting)
            filter.reset();
        if (! preserveGain)
        {
            smartGain.setCurrentAndTargetValue (1.0f);
        }
    }

    void prepare (double newSampleRate, int newMaximumBlockSize, int newChannels)
    {
        sampleRate = newSampleRate;
        maximumBlockSize = juce::jmax (1, newMaximumBlockSize);
        channels = juce::jlimit (1, maximumChannels, newChannels);
        minimumBank.prepare (sampleRate, maximumBlockSize, channels);
        linearBank.prepare (sampleRate, maximumBlockSize, channels);
        for (auto& engine : engines)
            engine.prepare (sampleRate, maximumBlockSize, channels);
        bandLatency = engines.front().getLatencySamples();
        for (auto& band : bands)
            band.setSize (channels, maximumBlockSize, false, false, true);
        dryReference.setSize (channels, maximumBlockSize, false, false, true);
        dryDelay.prepare (
            bandLatency + linearBank.groupDelay + maximumBlockSize * 2 + 16);
        prepareWeighting();
        smartGain.reset (sampleRate, 0.02);
        smartGain.setCurrentAndTargetValue (1.0f);
        restartSmartMeasurement (false);
    }

    void reset()
    {
        minimumBank.reset();
        linearBank.reset();
        for (auto& engine : engines)
            engine.reset();
        dryDelay.reset();
        lastSignature = 0;
        wasSmart = false;
        wasSolo = false;
        smartGain.setCurrentAndTargetValue (1.0f);
        restartSmartMeasurement (false);
    }

    void updateSmart (juce::AudioBuffer<float>& output,
                      const juce::AudioBuffer<float>& dry,
                      const Parameters& master,
                      const MultibandParameters& multiband,
                      int soloBand)
    {
        const auto smart = master.autoGainMode == 2;
        const auto signature = hashParameters (master, multiband);
        const auto solo = soloBand >= 0;
        if (smart && (! wasSmart || signature != lastSignature
                      || (wasSolo && ! solo)))
            restartSmartMeasurement (wasSmart);
        if (! smart)
        {
            smartGain.setCurrentAndTargetValue (1.0f);
            smartLocked = false;
            smartProgress.store (0.0f, std::memory_order_relaxed);
            smartLockedForUi.store (false, std::memory_order_relaxed);
        }
        lastSignature = signature;
        wasSmart = smart;
        wasSolo = solo;

        if (smart && ! smartLocked && ! solo)
        {
            constexpr auto settleSeconds = 0.22;
            constexpr auto measureSeconds = 0.55;
            const auto settleTarget = juce::roundToInt (sampleRate * settleSeconds);
            const auto measureTarget = juce::roundToInt (sampleRate * measureSeconds);
            const auto samples = output.getNumSamples();
            const auto canMeasure = settleSamples >= settleTarget;
            for (int sample = 0; sample < samples; ++sample)
                for (int channel = 0; channel < output.getNumChannels(); ++channel)
                {
                    const auto index = static_cast<size_t> (channel);
                    const auto weightedDry = dryWeighting[index].process (
                        dry.getSample (channel, sample));
                    const auto wet = output.getSample (channel, sample);
                    const auto weightedWet = wetWeighting[index].process (wet);
                    if (canMeasure)
                    {
                        dryEnergy += static_cast<double> (weightedDry) * weightedDry;
                        wetEnergy += static_cast<double> (weightedWet) * weightedWet;
                        wetPeak = juce::jmax (
                            wetPeak, std::abs (static_cast<double> (wet)));
                    }
                }
            if (! canMeasure)
                settleSamples += samples;
            else
                measuredSamples += samples;

            const auto progress = settleSamples < settleTarget
                ? 0.45f * static_cast<float> (settleSamples)
                    / static_cast<float> (juce::jmax (1, settleTarget))
                : 0.45f + 0.55f * static_cast<float> (measuredSamples)
                    / static_cast<float> (juce::jmax (1, measureTarget));
            smartProgress.store (
                juce::jlimit (0.0f, 1.0f, progress), std::memory_order_relaxed);

            if (measuredSamples >= measureTarget
                && dryEnergy > 1.0e-10 && wetEnergy > 1.0e-10)
            {
                auto gain = static_cast<float> (std::sqrt (dryEnergy / wetEnergy));
                gain = juce::jlimit (
                    juce::Decibels::decibelsToGain (-72.0f),
                    juce::Decibels::decibelsToGain (18.0f),
                    gain);
                if (wetPeak > 1.0e-6)
                    gain = juce::jmin (gain, static_cast<float> (0.98 / wetPeak));
                smartGain.setTargetValue (gain);
                smartLocked = true;
                smartProgress.store (1.0f, std::memory_order_relaxed);
                smartLockedForUi.store (true, std::memory_order_relaxed);
            }
        }

        for (int sample = 0; sample < output.getNumSamples(); ++sample)
        {
            const auto gain = smart ? smartGain.getNextValue() : 1.0f;
            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                output.setSample (
                    channel, sample, output.getSample (channel, sample) * gain);
        }
    }

    void process (juce::AudioBuffer<float>& buffer,
                  const Parameters& master,
                  const MultibandParameters& multiband,
                  int soloBand)
    {
        const auto activeBands = juce::jlimit (2, maximumBands, multiband.bandCount);
        const auto samples = buffer.getNumSamples();
        const auto activeChannels = buffer.getNumChannels();
        dryReference.setSize (activeChannels, samples, false, false, true);
        dryReference.makeCopyOf (buffer, true);
        for (auto& band : bands)
            band.setSize (activeChannels, samples, false, false, true);

        if (multiband.phaseMode == 0)
            minimumBank.process (
                buffer, bands, activeBands,
                multiband.crossoverHz, multiband.crossoverSlope);
        else
            linearBank.process (
                buffer, bands, activeBands,
                multiband.crossoverHz, multiband.crossoverSlope);

        buffer.clear();
        for (int band = 0; band < activeBands; ++band)
        {
            const auto index = static_cast<size_t> (band);
            BandParameters values;
            if (multiband.linked)
            {
                values = multiband.bands[index];
                values.saturation = master;
            }
            else
                values = multiband.bands[index];
            values.saturation.quality = master.quality;
            values.saturation.autoGainMode = master.autoGainMode == 1 ? 1 : 0;
            values.saturation.outputDb = values.trimDb;
            if (values.bypass)
            {
                values.saturation.mix = 0.0f;
                values.saturation.autoGainMode = 0;
            }
            engines[index].processBand (bands[index], values.saturation);
            if (soloBand < 0 || soloBand == band)
                for (int channel = 0; channel < activeChannels; ++channel)
                    buffer.addFrom (channel, 0, bands[index], channel, 0, samples);
        }

        const auto totalLatency = bandLatency
            + (multiband.phaseMode == 0 ? 0 : linearBank.groupDelay);
        for (int channel = 0; channel < activeChannels; ++channel)
            for (int sample = 0; sample < samples; ++sample)
                dryReference.setSample (
                    channel,
                    sample,
                    dryDelay.process (
                        dryReference.getSample (channel, sample),
                        channel,
                        totalLatency));

        updateSmart (buffer, dryReference, master, multiband, soloBand);

        const auto outputGain = juce::Decibels::decibelsToGain (master.outputDb);
        for (int channel = 0; channel < activeChannels; ++channel)
            for (int sample = 0; sample < samples; ++sample)
            {
                auto value = buffer.getSample (channel, sample) * outputGain;
                if (master.outputDb <= 0.001f)
                    value = juce::jlimit (-1.0f, 1.0f, value);
                buffer.setSample (
                    channel, sample, std::isfinite (value) ? value : 0.0f);
            }
    }
};

MultibandProcessor::MultibandProcessor()
    : impl (std::make_unique<Impl>())
{
}

MultibandProcessor::~MultibandProcessor() = default;

void MultibandProcessor::prepare (double sampleRate,
                                  int maximumBlockSize,
                                  int channels)
{
    impl->prepare (sampleRate, maximumBlockSize, channels);
}

void MultibandProcessor::reset()
{
    impl->reset();
}

void MultibandProcessor::process (juce::AudioBuffer<float>& buffer,
                                  const Parameters& master,
                                  const MultibandParameters& multiband,
                                  int soloBand)
{
    impl->process (buffer, master, multiband, soloBand);
}

int MultibandProcessor::getLatencySamples (bool linearPhase) const noexcept
{
    return impl->bandLatency + (linearPhase ? impl->linearBank.groupDelay : 0);
}

float MultibandProcessor::getSmartAutoGainProgress() const noexcept
{
    return impl->smartProgress.load (std::memory_order_relaxed);
}

bool MultibandProcessor::isSmartAutoGainLocked() const noexcept
{
    return impl->smartLockedForUi.load (std::memory_order_relaxed);
}

int MultibandProcessor::slopeDecibelsPerOctave (int slopeIndex) noexcept
{
    constexpr std::array<int, 5> slopes { 6, 12, 24, 36, 48 };
    return slopes[static_cast<size_t> (juce::jlimit (0, 4, slopeIndex))];
}
} // namespace dd
