#include "../Source/DistortionEngine.h"
#include "../Source/GlobalBypass.h"
#include "../Source/MultibandProcessor.h"
#include "../Source/SpectrumFIFO.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <limits>
#include <new>
#include <set>
#include <vector>

namespace allocation_probe
{
thread_local bool enabled = false;
thread_local size_t count = 0;
}

void* operator new (std::size_t size)
{
    if (allocation_probe::enabled)
        ++allocation_probe::count;
    if (auto* memory = std::malloc (size))
        return memory;
    throw std::bad_alloc {};
}

void* operator new[] (std::size_t size)
{
    return ::operator new (size);
}

void operator delete (void* memory) noexcept
{
    std::free (memory);
}

void operator delete[] (void* memory) noexcept
{
    std::free (memory);
}

void operator delete (void* memory, std::size_t) noexcept
{
    std::free (memory);
}

void operator delete[] (void* memory, std::size_t) noexcept
{
    std::free (memory);
}

#if JUCE_MAC
#include <pthread.h>
#endif

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

struct TestContext
{
    int failures = 0;

    void expect (bool condition, const juce::String& message)
    {
        if (! condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void fillSignal (juce::AudioBuffer<float>& buffer, double& phase)
{
    constexpr auto fundamental = 173.0;
    constexpr auto second = 997.0;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto time = phase / sampleRate;
        const auto value = 0.61 * std::sin (
            juce::MathConstants<double>::twoPi * fundamental * time)
            + 0.19 * std::sin (
                juce::MathConstants<double>::twoPi * second * time);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (
                channel,
                sample,
                static_cast<float> (value * (channel == 0 ? 1.0 : 0.87)));
        phase += 1.0;
    }
}

void testNewDefaultsAndGlobalBypass (TestContext& context)
{
    const dd::MultibandParameters defaults;
    context.expect (
        defaults.bandCount == 4,
        "New multiband instances do not default to four bands");
    context.expect (
        defaults.crossoverHz == std::array<float, 3> { 100.0f, 500.0f, 2000.0f },
        "New multiband crossover defaults are incorrect");

    constexpr int testBlockSize = 256;
    dd::GlobalBypass bypass;
    bypass.prepare (sampleRate, testBlockSize, 1, 64, true);
    juce::AudioBuffer<float> input (1, testBlockSize);
    juce::AudioBuffer<float> output (1, testBlockSize);
    input.clear();
    output.clear();
    auto previous = 1.0f;
    auto maximumStep = 0.0f;
    for (int block = 0; block < 4; ++block)
    {
        input.clear();
        output.clear();
        for (int sample = 0; sample < testBlockSize; ++sample)
        {
            input.setSample (0, sample, -1.0f);
            output.setSample (0, sample, 1.0f);
        }
        bypass.captureInput (input);
        bypass.processOutput (output, 0, false);
        for (int sample = 0; sample < testBlockSize; ++sample)
        {
            const auto value = output.getSample (0, sample);
            maximumStep = juce::jmax (maximumStep, std::abs (value - previous));
            previous = value;
        }
    }
    context.expect (
        maximumStep < 0.01f,
        "Global bypass transition is not click-free");
    context.expect (
        std::abs (output.getSample (0, testBlockSize - 1) + 1.0f) < 1.0e-6f,
        "Global OFF does not settle to the dry signal");
    context.expect (
        ! bypass.shouldProcessWet (false),
        "Global OFF continues to request wet processing after its fade");

    dd::GlobalBypass delayedBypass;
    delayedBypass.prepare (sampleRate, 64, 1, 64, false);
    juce::AudioBuffer<float> impulse (1, 64);
    juce::AudioBuffer<float> silentEffect (1, 64);
    impulse.clear();
    silentEffect.clear();
    impulse.setSample (0, 0, 1.0f);
    delayedBypass.captureInput (impulse);
    delayedBypass.processOutput (silentEffect, 32, false);
    context.expect (
        std::abs (silentEffect.getSample (0, 32) - 1.0f) < 1.0e-6f,
        "Global OFF dry signal is not aligned to the reported latency");
    for (int sample = 0; sample < 64; ++sample)
        if (sample != 32)
            context.expect (
                std::abs (silentEffect.getSample (0, sample)) < 1.0e-6f,
                "Global OFF latency path introduced an unexpected sample");
}

double measureMultibandReconstructionGain (double rate,
                                           int bandCount,
                                           int slopeIndex,
                                           bool linearPhase,
                                           double frequency,
                                           TestContext& context)
{
    constexpr int testBlockSize = 256;
    dd::MultibandProcessor processor;
    processor.prepare (rate, testBlockSize, 2);
    dd::Parameters master;
    master.autoGainMode = 0;
    master.outputDb = 0.01f;
    dd::MultibandParameters multiband;
    multiband.enabled = true;
    multiband.linked = false;
    multiband.bandCount = bandCount;
    multiband.phaseMode = linearPhase ? 1 : 0;
    multiband.crossoverHz = { 120.0f, 1000.0f, 5000.0f };
    multiband.crossoverSlope.fill (slopeIndex);
    for (auto& band : multiband.bands)
        band.bypass = true;

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    double phase = 0.0;
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    const auto warmupBlocks = linearPhase ? 96 : 32;
    constexpr int measuredBlocks = 48;
    for (int block = 0; block < warmupBlocks + measuredBlocks; ++block)
    {
        for (int sample = 0; sample < testBlockSize; ++sample)
        {
            const auto value = 0.12f * static_cast<float> (std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * frequency / rate;
            for (int channel = 0; channel < 2; ++channel)
                buffer.setSample (channel, sample, value);
            if (block >= warmupBlocks)
                inputEnergy += static_cast<double> (value) * value * 2.0;
        }
        processor.process (buffer, master, multiband, -1);
        if (block >= warmupBlocks)
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < testBlockSize; ++sample)
                {
                    const auto value = buffer.getSample (channel, sample);
                    context.expect (
                        std::isfinite (value),
                        "Multiband reconstruction produced a non-finite sample");
                    outputEnergy += static_cast<double> (value) * value;
                }
    }
    return std::sqrt (outputEnergy / juce::jmax (1.0e-20, inputEnergy));
}

void testMultibandCrossoversAndSmartGain (TestContext& context)
{
    constexpr std::array<double, 3> rates { 44100.0, 48000.0, 96000.0 };
    constexpr std::array<double, 5> frequencies {
        55.0, 240.0, 1500.0, 6500.0, 14000.0
    };
    for (const auto rate : rates)
        for (int bands = 2; bands <= 4; ++bands)
            for (int slope = 0; slope < 5; ++slope)
                for (const auto frequency : frequencies)
                {
                    if (frequency >= 0.4 * rate)
                        continue;
                    const auto gain = measureMultibandReconstructionGain (
                        rate, bands, slope, false, frequency, context);
                    context.expect (
                        gain > 0.975 && gain < 1.025,
                        "Minimum-phase reconstruction gain is not flat at "
                            + juce::String (frequency)
                            + " Hz, " + juce::String (bands)
                            + " bands, "
                            + juce::String (
                                dd::MultibandProcessor::slopeDecibelsPerOctave (
                                    slope))
                            + " dB/oct: " + juce::String (gain));
                }

    for (const auto bands : { 2, 4 })
        for (const auto frequency : { 55.0, 1500.0, 10000.0 })
        {
            const auto gain = measureMultibandReconstructionGain (
                48000.0, bands, 2, true, frequency, context);
            context.expect (
                gain > 0.97 && gain < 1.03,
                "Linear-phase complementary reconstruction is not unity at "
                    + juce::String (frequency) + " Hz, "
                    + juce::String (bands) + " bands: "
                    + juce::String (gain));
        }

    dd::MultibandProcessor processor;
    processor.prepare (sampleRate, 256, 2);
    context.expect (
        processor.getLatencySamples (true)
            > processor.getLatencySamples (false),
        "Linear phase does not report additional latency");
    dd::Parameters master;
    master.autoGainMode = 2;
    master.outputDb = 0.01f;
    dd::MultibandParameters multiband;
    multiband.enabled = true;
    multiband.linked = false;
    multiband.bandCount = 4;
    for (auto& band : multiband.bands)
        band.bypass = true;
    juce::AudioBuffer<float> buffer (2, 256);
    double phase = 0.0;
    for (int block = 0; block < 220; ++block)
    {
        fillSignal (buffer, phase);
        processor.process (buffer, master, multiband, -1);
    }
    context.expect (
        processor.isSmartAutoGainLocked()
            && processor.getSmartAutoGainProgress() >= 0.999f,
        "Multiband Smart Auto Gain did not lock on the summed signal");

    dd::MultibandProcessor linear;
    linear.prepare (sampleRate, 256, 2);
    master.autoGainMode = 0;
    master.outputDb = 0.0f;
    multiband.phaseMode = 1;
    const auto latency = linear.getLatencySamples (true);
    std::vector<float> inputHistory;
    std::vector<float> outputHistory;
    inputHistory.reserve (256 * 160);
    outputHistory.reserve (256 * 160);
    std::uint32_t noise = UINT32_C (0x13579bdf);
    for (int block = 0; block < 160; ++block)
    {
        for (int sample = 0; sample < 256; ++sample)
        {
            noise = noise * UINT32_C (1664525) + UINT32_C (1013904223);
            const auto value = 0.05f
                * (2.0f * static_cast<float> (noise & UINT32_C (0x00ffffff))
                    / static_cast<float> (UINT32_C (0x00ffffff)) - 1.0f);
            inputHistory.push_back (value);
            buffer.setSample (0, sample, value);
            buffer.setSample (1, sample, value);
        }
        linear.process (buffer, master, multiband, -1);
        for (int sample = 0; sample < 256; ++sample)
            outputHistory.push_back (buffer.getSample (0, sample));
    }
    double referenceEnergy = 0.0;
    double errorEnergy = 0.0;
    const auto start = juce::jmax (latency + 4096, 12000);
    for (int sample = start;
         sample < static_cast<int> (outputHistory.size());
         ++sample)
    {
        const auto reference = inputHistory[static_cast<size_t> (sample - latency)];
        const auto error = outputHistory[static_cast<size_t> (sample)] - reference;
        referenceEnergy += static_cast<double> (reference) * reference;
        errorEnergy += static_cast<double> (error) * error;
    }
    const auto nullDb = 10.0 * std::log10 (
        juce::jmax (1.0e-30, errorEnergy)
        / juce::jmax (1.0e-30, referenceEnergy));
    context.expect (
        nullDb < -70.0,
        "Linear-phase bands do not null against the delayed input: "
            + juce::String (nullDb) + " dB");

    dd::MultibandProcessor deltaProcessor;
    deltaProcessor.prepare (sampleRate, 256, 2);
    dd::Parameters deltaMaster;
    deltaMaster.mode = static_cast<int> (
        dd::DistortionEngine::Mode::deltaCrusher);
    deltaMaster.driveDb = 36.0f;
    deltaMaster.character = 1.0f;
    deltaMaster.stages = 1;
    deltaMaster.autoGainMode = 0;
    deltaMaster.outputDb = 0.01f;
    dd::MultibandParameters deltaMultiband;
    deltaMultiband.enabled = true;
    deltaMultiband.linked = true;
    deltaMultiband.bandCount = 4;
    juce::AudioBuffer<float> deltaBuffer (2, 256);
    auto maximumChannelDifference = 0.0f;
    double deltaPhase = 0.0;
    for (int block = 0; block < 96; ++block)
    {
        for (int sample = 0; sample < deltaBuffer.getNumSamples(); ++sample)
        {
            const auto value = 0.25f * static_cast<float> (
                std::sin (deltaPhase));
            deltaPhase += juce::MathConstants<double>::twoPi * 173.0
                / sampleRate;
            deltaBuffer.setSample (0, sample, value);
            deltaBuffer.setSample (
                1, sample, block < 24 ? -0.83f * value : value);
        }
        deltaProcessor.process (
            deltaBuffer, deltaMaster, deltaMultiband, -1);
        if (block >= 24)
            for (int sample = 0; sample < deltaBuffer.getNumSamples(); ++sample)
                maximumChannelDifference = juce::jmax (
                    maximumChannelDifference,
                    std::abs (
                        deltaBuffer.getSample (0, sample)
                        - deltaBuffer.getSample (1, sample)));
    }
    context.expect (
        maximumChannelDifference < 1.0e-7f,
        "Multiband Delta Crusher turns identical mono input into stereo "
            "(maximum L/R difference "
            + juce::String (maximumChannelDifference, 8) + ")");

    dd::MultibandProcessor automated;
    automated.prepare (sampleRate, 256, 2);
    multiband.phaseMode = 0;
    multiband.bandCount = 4;
    multiband.crossoverSlope = { 0, 1, 4 };
    double automationPhase = 0.0;
    auto maximumMagnitude = 0.0f;
    for (int block = 0; block < 180; ++block)
    {
        fillSignal (buffer, automationPhase);
        const auto sweep = static_cast<float> (block % 30) / 29.0f;
        multiband.crossoverHz = {
            35.0f * std::pow (10.0f, sweep),
            450.0f * std::pow (8.0f, 1.0f - sweep),
            5200.0f + 6500.0f * sweep
        };
        if (block % 17 == 0)
        {
            multiband.bandCount = 2 + (block / 17) % 3;
            for (auto& slope : multiband.crossoverSlope)
                slope = (slope + 1) % 5;
        }
        automated.process (buffer, master, multiband, -1);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = buffer.getSample (channel, sample);
                context.expect (
                    std::isfinite (value),
                    "Minimum-phase crossover automation produced NaN/Inf");
                maximumMagnitude = juce::jmax (
                    maximumMagnitude, std::abs (value));
            }
    }
    context.expect (
        maximumMagnitude < 8.0f,
        "Minimum-phase crossover automation produced an unbounded transient: "
            + juce::String (maximumMagnitude));
}

double processModeFingerprint (int mode, int stages, float tone, TestContext& context)
{
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 2);

    dd::Parameters parameters;
    parameters.mode = mode;
    parameters.driveDb = 18.0f;
    parameters.character = 0.23f;
    parameters.asymmetry = -0.17f;
    parameters.tone = tone;
    parameters.stages = stages;
    parameters.mix = 1.0f;
    parameters.outputDb = -3.0f;
    parameters.quality = 0;
    parameters.autoGainMode = 0;

    juce::AudioBuffer<float> buffer (2, blockSize);
    double signalPhase = 0.0;
    double energy = 0.0;
    double weightedSum = 0.0;

    for (int block = 0; block < 12; ++block)
    {
        fillSignal (buffer, signalPhase);
        engine.process (buffer, parameters);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = static_cast<double> (data[sample]);
                context.expect (
                    std::isfinite (value),
                    "Mode " + juce::String (mode + 1) + " produced non-finite audio");
                energy += value * value;
                weightedSum += value
                    * static_cast<double> (1 + ((sample + channel * 13) % 31));
            }
        }
    }

    context.expect (
        energy > 1.0e-8,
        "Mode " + juce::String (mode + 1) + " produced silence");
    return weightedSum / std::sqrt (juce::jmax (energy, 1.0e-12));
}

void testModeMetadata (TestContext& context)
{
    const auto& names = dd::DistortionEngine::getModeNames();
    const auto& characterNames = dd::DistortionEngine::getCharacterNames();
    const std::array<juce::String, dd::DistortionEngine::modeCount> expectedNames {
        "Soft Clip",
        "Hard Clip",
        "Diode Clipper",
        "Triode Stage",
        "Transistor / FET",
        "Tape Hysteresis",
        "Harmonic Morph",
        "Phase Distortion",
        "Spectral Clip",
        "Sign / Square",
        "Zero-Square",
        "Full-Wave Rectifier",
        "Soft Full-Wave",
        "Transformer Core",
        "Sine Erosion",
        "Class-B Saturation",
        "Topology Fold",
        "Recursive Foldback",
        "Sine Fold",
        "Chebyshev Fold",
        "Modulo Wrap",
        "Downsample",
        "Bit Crusher",
        "Bit Rotation",
        "Delta Crusher",
        "Slew Limiter",
        "Schmitt Hysteresis",
        "Feedback Saturator",
        "Resonant Feedback Clip",
        "Dynamic Sag"
    };
    context.expect (
        static_cast<int> (names.size()) == dd::DistortionEngine::modeCount,
        "Mode name count is not 30");
    context.expect (
        characterNames.size() == names.size(),
        "Character labels do not match mode count");
    context.expect (
        dd::DistortionEngine::getModeForDisplayPosition (9)
                == static_cast<int> (
                    dd::DistortionEngine::Mode::sineErosion)
            && dd::DistortionEngine::getDisplayPositionForMode (
                static_cast<int> (
                    dd::DistortionEngine::Mode::sineErosion)) == 9,
        "Sine Erosion is not tenth in the user-facing mode order");

    std::set<std::string> uniqueNames;
    for (const auto& name : names)
    {
        context.expect (name.isNotEmpty(), "A mode has an empty name");
        uniqueNames.insert (name.toStdString());
    }
    context.expect (
        uniqueNames.size() == names.size(),
        "Mode names are not unique");
    context.expect (
        names == expectedNames,
        "Mode order does not match the published 30-algorithm order");
}

void dumpRegressionFingerprints()
{
    std::cout << std::setprecision (17);
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
    {
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, blockSize, 2);
        dd::Parameters parameters;
        parameters.mode = mode;
        parameters.driveDb = 17.37f;
        parameters.character =
            dd::DistortionEngine::isCharacterBipolar (mode)
                ? 0.31f
                : 0.413f;
        parameters.asymmetry = 0.19f;
        parameters.tone = 0.23f;
        parameters.stages = 3;
        parameters.mix = 1.0f;
        parameters.outputDb = -3.0f;
        parameters.quality = 0;
        parameters.autoGainMode = 0;

        juce::AudioBuffer<float> buffer (2, blockSize);
        double phase = 0.0;
        double energy = 0.0;
        double sum = 0.0;
        double weighted = 0.0;
        for (int block = 0; block < 24; ++block)
        {
            fillSignal (buffer, phase);
            engine.process (buffer, parameters);
            if (block < 8)
                continue;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    const auto value = static_cast<double> (
                        buffer.getSample (channel, sample));
                    energy += value * value;
                    sum += value;
                    weighted += value
                        * static_cast<double> (
                            1 + ((sample + 17 * channel) % 37));
                }
        }
        std::cout << mode << ' ' << energy << ' ' << sum << ' '
                  << weighted << '\n';
    }
}

void dumpVisualizationFingerprints()
{
    std::cout << std::setprecision (17);
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
    {
        dd::Parameters parameters;
        parameters.mode = mode;
        parameters.driveDb = 17.37f;
        parameters.character = dd::DistortionEngine::isCharacterBipolar (mode)
            ? 0.31f : 0.413f;
        parameters.asymmetry = 0.19f;
        parameters.tone = 0.23f;
        parameters.stages = 3;
        dd::DistortionEngine::Visualization visualization;
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, visualization);
        double energy = 0.0;
        double weighted = 0.0;
        for (size_t point = 0; point < visualization.output.size(); ++point)
        {
            const auto value = static_cast<double> (visualization.output[point]);
            energy += value * value;
            weighted += value * static_cast<double> (1 + point % 37);
        }
        std::cout << mode << ' ' << visualization.spectralDomain << ' '
                  << energy << ' ' << weighted << '\n';
    }
}

void testVersionEightVisualizationSnapshots (TestContext& context)
{
    constexpr std::array<double, dd::DistortionEngine::modeCount> energies {
        299.95919102551096, 175.96068699146173, 257.14013753935961,
        92.877648481528226, 93.312763517585481, 8.1781553312368391,
        94.238846132342957, 49.416243454242938, 47.385367130275164,
        177.89142776983522, 172.75460439632241, 109.35201716808001,
        622859.02413216059, 211.83531114730727, 167.18625657876964,
        167.86993261543182, 17.774857203110066, 20.775564491450552,
        31.078447042118345, 96.41429509190678, 24.912767365357045,
        11.598975625980183, 93.106616009136118, 6.4476106679569565,
        80.290416098524275, 0.38027489355339378, 172.94506534804566,
        165.3278794563862, 101.82736555986678, 87.760863107682297
    };
    constexpr std::array<double, dd::DistortionEngine::modeCount> weighted {
        281.55117690563202, 224.36887747049332, 281.66199898719788,
        1841.1023117722943, 1463.4076444804668, -62.038499512062117,
        1089.2627401510254, -645.42350653842175, -101.96342594490852,
        89.327598989009857, 225.99558597360738, 1409.4485324576963,
        188582.23256824291, 100.65764954686165, 2707.1385645605624,
        1268.4999257484451, -152.86343750543892, -155.33020150495577,
        280.828044076683, 922.73985170945525, -45.272050202242099,
        594.54099584720097, 673.98215615749359, 33.21148837916553,
        309.50201855413616, 151.28550757281482, 93.918638050556183,
        -19.383059173822403, 1839.946959676221, 280.13748859310749
    };
    const auto near = [] (double actual, double expected)
    {
        return std::abs (actual - expected)
            <= 1.0e-5 * juce::jmax (1.0, std::abs (expected));
    };
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
    {
        dd::Parameters parameters;
        parameters.mode = mode;
        parameters.driveDb = 17.37f;
        parameters.character = dd::DistortionEngine::isCharacterBipolar (mode)
            ? 0.31f : 0.413f;
        parameters.asymmetry = 0.19f;
        parameters.tone = 0.23f;
        parameters.stages = 3;
        dd::DistortionEngine::Visualization visualization;
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, visualization);
        double actualEnergy = 0.0;
        double actualWeighted = 0.0;
        for (size_t point = 0; point < visualization.output.size(); ++point)
        {
            const auto value = static_cast<double> (visualization.output[point]);
            actualEnergy += value * value;
            actualWeighted += value * static_cast<double> (1 + point % 37);
        }
        context.expect (
            near (actualEnergy, energies[static_cast<size_t> (mode)])
                && near (actualWeighted, weighted[static_cast<size_t> (mode)]),
            "0.8 visualization snapshot changed for mode "
                + juce::String (mode + 1));
    }
}

void testCanonicalClipCeilings (TestContext& context)
{
    for (const auto mode : {
             dd::DistortionEngine::Mode::morphSoftClip,
             dd::DistortionEngine::Mode::hardClip })
    {
        for (const auto character : { 0.0f, 0.5f, 1.0f })
        {
            dd::Parameters parameters;
            parameters.mode = static_cast<int> (mode);
            parameters.driveDb = 36.0f;
            parameters.character = character;
            parameters.stages = 1;

            dd::DistortionEngine::Visualization view;
            dd::DistortionEngine::makeVisualization (
                parameters, sampleRate, view);
            const auto peak = *std::max_element (
                view.output.begin(), view.output.end());
            const auto maximumPeak =
                mode == dd::DistortionEngine::Mode::morphSoftClip
                    ? 1.25001f
                    : 1.00001f;
            context.expect (
                peak >= (mode == dd::DistortionEngine::Mode::morphSoftClip
                            ? 1.249f
                            : 0.999f)
                    && peak <= maximumPeak,
                dd::DistortionEngine::getModeNames()[
                    static_cast<size_t> (parameters.mode)]
                    + " ceiling is not 0 dBFS");
        }

        dd::DistortionEngine engine;
        engine.prepare (sampleRate, blockSize, 1);
        dd::Parameters audioParameters;
        audioParameters.mode = static_cast<int> (mode);
        audioParameters.driveDb = 36.0f;
        audioParameters.character = 0.5f;
        audioParameters.stages = 1;
        audioParameters.autoGainMode = 0;
        juce::AudioBuffer<float> buffer (1, blockSize);
        double phase = 0.0;
        auto audioPeak = 0.0f;
        for (int block = 0; block < 12; ++block)
        {
            fillSignal (buffer, phase);
            engine.process (buffer, audioParameters);
            audioPeak = juce::jmax (
                audioPeak, buffer.getMagnitude (0, 0, blockSize));
        }
        context.expect (
            audioPeak >= 0.999f && audioPeak <= 1.00001f,
            dd::DistortionEngine::getModeNames()[
                static_cast<size_t> (audioParameters.mode)]
                + " audio path does not reach the 0 dBFS ceiling");
    }
}

void testVitalClipTransfers (TestContext& context)
{
    auto vitalTanhReference = [] (float value)
    {
        const auto absValue = std::abs (value);
        const auto square = value * value;
        const auto numerator =
            value
            * (2.45550750702956f
               + 2.45550750702956f * absValue
               + square
                   * (0.893229853513558f
                      + 0.821226666969744f * absValue));
        const auto denominator =
            2.44506634652299f
            + (2.44506634652299f + square)
                * std::abs (
                    value
                    + 0.814642734961073f * value * absValue);
        return numerator / denominator;
    };

    for (const auto mode : {
             dd::DistortionEngine::Mode::morphSoftClip })
    {
        dd::Parameters parameters;
        parameters.mode = static_cast<int> (mode);
        parameters.driveDb = 0.0f;
        parameters.character = 0.0f;
        parameters.stages = 1;
        dd::DistortionEngine::Visualization view;
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, view);

        for (size_t point = 0; point < view.input.size(); ++point)
        {
            const auto input = view.input[point];
            const auto expected = vitalTanhReference (input)
                * (1.25f / std::abs (vitalTanhReference (1.5f)));
            context.expect (
                std::abs (view.output[point] - expected) < 2.0e-6f,
                dd::DistortionEngine::getModeNames()[
                    static_cast<size_t> (parameters.mode)]
                    + " preview no longer uses a linearly scaled Vital curve");
        }
    }

    dd::Parameters hard;
    hard.mode = static_cast<int> (
        dd::DistortionEngine::Mode::hardClip);
    hard.driveDb = 0.0f;
    hard.character = 0.0f;
    hard.stages = 1;
    auto diode = hard;
    diode.mode = static_cast<int> (
        dd::DistortionEngine::Mode::diodeClipper);
    dd::DistortionEngine::Visualization hardView;
    dd::DistortionEngine::Visualization diodeView;
    dd::DistortionEngine::makeVisualization (
        hard, sampleRate, hardView);
    dd::DistortionEngine::makeVisualization (
        diode, sampleRate, diodeView);
    for (size_t point = 0; point < hardView.input.size(); ++point)
    {
        context.expect (
            std::abs (hardView.input[point] - diodeView.input[point])
                    < 1.0e-6f
                && std::abs (
                    hardView.output[point] - diodeView.output[point])
                    < 1.0e-6f,
            "Hard Clip and Diode Clipper previews differ at Drive 0");
    }

    hard.driveDb = 24.0f;
    diode.driveDb = hard.driveDb;
    dd::DistortionEngine::makeVisualization (
        hard, sampleRate, hardView);
    dd::DistortionEngine::makeVisualization (
        diode, sampleRate, diodeView);
    double drivenDifference = 0.0;
    for (size_t point = 0; point < hardView.output.size(); ++point)
        drivenDifference += std::abs (static_cast<double> (
            hardView.output[point] - diodeView.output[point]));
    drivenDifference /= static_cast<double> (hardView.output.size());
    context.expect (
        drivenDifference > 0.03,
        "Hard Clip and Diode Clipper cease to be distinct above Drive 0");
}

void testClipMorphEndpointsAndHardPlateau (TestContext& context)
{
    auto vitalTanhReference = [] (float value)
    {
        const auto absValue = std::abs (value);
        const auto square = value * value;
        return value
            * (2.45550750702956f
               + 2.45550750702956f * absValue
               + square
                   * (0.893229853513558f
                      + 0.821226666969744f * absValue))
            / (2.44506634652299f
               + (2.44506634652299f + square)
                   * std::abs (
                       value
                       + 0.814642734961073f * value * absValue));
    };
    auto cubicReference = [] (float value)
    {
        if (value >= 1.0f)
            return 1.0f;
        if (value <= -1.0f)
            return -1.0f;
        return 1.5f * (value - value * value * value / 3.0f);
    };

    for (const auto mode : {
             dd::DistortionEngine::Mode::morphSoftClip,
             dd::DistortionEngine::Mode::hardClip })
    {
        dd::Parameters parameters;
        parameters.mode = static_cast<int> (mode);
        parameters.driveDb =
            mode == dd::DistortionEngine::Mode::hardClip
                ? 0.01f
                : 0.0f;
        parameters.character = 1.0f;
        parameters.stages = 1;
        dd::DistortionEngine::Visualization view;
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, view);
        for (size_t point = 0; point < view.input.size(); ++point)
        {
            const auto drivenInput =
                view.input[point]
                * juce::Decibels::decibelsToGain (
                    parameters.driveDb);
            const auto normalisedDrive =
                parameters.driveDb / 36.0f;
            const auto depth =
                normalisedDrive * normalisedDrive
                * (3.0f - 2.0f * normalisedDrive);
            const auto hardSoftEndpoint = juce::jlimit (
                -1.0f,
                1.0f,
                vitalTanhReference (drivenInput));
            const auto expected =
                mode == dd::DistortionEngine::Mode::morphSoftClip
                    ? 1.25f * cubicReference (view.input[point])
                    : view.input[point]
                        + depth
                            * (hardSoftEndpoint - view.input[point]);
            context.expect (
                std::abs (view.output[point] - expected) < 2.0e-6f,
                "Clip Character 100% does not reach its documented endpoint");
        }
    }

    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 1);
    dd::Parameters hard;
    hard.mode = static_cast<int> (dd::DistortionEngine::Mode::hardClip);
    hard.driveDb = 36.0f;
    hard.character = 0.0f;
    hard.asymmetry = 0.0f;
    hard.tone = 0.0f;
    hard.stages = 1;
    hard.mix = 1.0f;
    hard.outputDb = 0.0f;
    hard.quality = 0;
    hard.autoGainMode = 0;
    juce::AudioBuffer<float> buffer (1, blockSize);

    auto processConstant = [&] (float value)
    {
        for (int block = 0; block < 160; ++block)
        {
            for (int sample = 0; sample < blockSize; ++sample)
                buffer.setSample (0, sample, value);
            engine.process (buffer, hard);
        }
        return buffer.getSample (0, blockSize - 1);
    };

    const auto positivePlateau = processConstant (0.9f);
    const auto negativePlateau = processConstant (-0.9f);
    context.expect (
        std::abs (positivePlateau - 1.0f) < 1.0e-6f,
        "Hard Clip full path does not hold a flat +1 clamp plateau");
    context.expect (
        std::abs (negativePlateau + 1.0f) < 1.0e-6f,
        "Hard Clip full path does not hold a flat -1 clamp plateau");
}

void testEveryMode (TestContext& context)
{
    std::set<long long> coarseFingerprints;
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
    {
        const auto fingerprint = processModeFingerprint (mode, 3, 0.21f, context);
        coarseFingerprints.insert (
            static_cast<long long> (std::llround (fingerprint * 1000.0)));
    }

    context.expect (
        coarseFingerprints.size() >= 22,
        "Too many modes share the same coarse processing fingerprint");
}

void testStageCascadeChangesAudio (TestContext& context)
{
    const auto oneStage = processModeFingerprint (
        static_cast<int> (dd::DistortionEngine::Mode::hardClip), 1, 0.0f, context);
    const auto eightStages = processModeFingerprint (
        static_cast<int> (dd::DistortionEngine::Mode::hardClip), 8, 0.0f, context);
    context.expect (
        std::abs (oneStage - eightStages) > 1.0e-3,
        "The stage cascade does not alter Hard Clip");

    const std::array<dd::DistortionEngine::Mode, 5> regressionModes {
        dd::DistortionEngine::Mode::tapeHysteresis,
        dd::DistortionEngine::Mode::transformerCore,
        dd::DistortionEngine::Mode::schmittHysteresis,
        dd::DistortionEngine::Mode::feedbackSaturator,
        dd::DistortionEngine::Mode::resonantFeedbackClip
    };
    for (const auto mode : regressionModes)
    {
        dd::Parameters one;
        one.mode = static_cast<int> (mode);
        one.driveDb = 18.0f;
        one.character = 0.78f;
        one.asymmetry = 0.11f;
        one.stages = 1;
        auto eight = one;
        eight.stages = 8;

        dd::DistortionEngine::Visualization oneView;
        dd::DistortionEngine::Visualization eightView;
        dd::DistortionEngine::makeVisualization (
            one, sampleRate, oneView);
        dd::DistortionEngine::makeVisualization (
            eight, sampleRate, eightView);

        double oneEffect = 0.0;
        double eightEffect = 0.0;
        double stageDifference = 0.0;
        for (size_t point = 0; point < oneView.output.size(); ++point)
        {
            oneEffect += std::abs (
                static_cast<double> (
                    oneView.output[point] - oneView.input[point]));
            eightEffect += std::abs (
                static_cast<double> (
                    eightView.output[point] - eightView.input[point]));
            stageDifference += std::abs (
                static_cast<double> (
                    eightView.output[point] - oneView.output[point]));
        }
        context.expect (
            eightEffect >= 0.55 * oneEffect,
            "Eight stages weaken the effect toward dry in mode "
                + juce::String (one.mode + 1)
                + " (one " + juce::String (oneEffect, 3)
                + ", eight " + juce::String (eightEffect, 3) + ")");
        context.expect (
            stageDifference > 0.005,
            "Stages have no observable cascade effect in mode "
                + juce::String (one.mode + 1));
    }
}

#if JUCE_MAC
struct SmallStackResult
{
    bool completed = false;
    bool finite = false;
};

void* runSmallStackRegression (void* opaque)
{
    auto& result = *static_cast<SmallStackResult*> (opaque);
    auto engine = std::make_unique<dd::DistortionEngine>();
    engine->prepare (sampleRate, blockSize, 2);

    dd::Parameters parameters;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::phaseDistortion);
    parameters.driveDb = 30.0f;
    parameters.character = 0.5f;
    parameters.asymmetry = 0.2f;
    parameters.stages = 8;
    parameters.autoGainMode = 1;

    juce::AudioBuffer<float> buffer (2, blockSize);
    double phase = 0.0;
    fillSignal (buffer, phase);
    engine->process (buffer, parameters);

    dd::DistortionEngine::Visualization visualization;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, visualization);

    result.finite = true;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            result.finite = result.finite
                && std::isfinite (buffer.getSample (channel, sample));
    for (const auto value : visualization.output)
        result.finite = result.finite && std::isfinite (value);
    result.completed = true;
    return nullptr;
}

void testReaperSizedAudioThreadStack (TestContext& context)
{
    pthread_attr_t attributes;
    context.expect (
        pthread_attr_init (&attributes) == 0,
        "Could not initialise the constrained-stack regression thread");
    constexpr size_t reaperSizedStack = 512 * 1024;
    context.expect (
        pthread_attr_setstacksize (&attributes, reaperSizedStack) == 0,
        "Could not set a REAPER-sized audio-thread stack");

    pthread_t thread {};
    SmallStackResult result;
    const auto created = pthread_create (
        &thread,
        &attributes,
        runSmallStackRegression,
        &result);
    pthread_attr_destroy (&attributes);
    context.expect (
        created == 0,
        "Could not create the constrained-stack regression thread");
    if (created == 0)
        pthread_join (thread, nullptr);

    context.expect (
        result.completed && result.finite,
        "DSP or visualization failed on a 512 KiB host thread stack");
}
#endif

void testOversamplingPaths (TestContext& context)
{
    for (int quality = 0; quality < 4; ++quality)
    {
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, blockSize, 2);

        dd::Parameters parameters;
        parameters.mode = static_cast<int> (dd::DistortionEngine::Mode::sineFold);
        parameters.driveDb = 24.0f;
        parameters.character = 0.8f;
        parameters.stages = 4;
        parameters.quality = quality;
        parameters.autoGainMode = 0;

        juce::AudioBuffer<float> buffer (2, blockSize);
        double phase = 0.0;
        double energy = 0.0;
        for (int block = 0; block < 8; ++block)
        {
            fillSignal (buffer, phase);
            engine.process (buffer, parameters);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const auto value = buffer.getSample (channel, sample);
                    context.expect (
                        std::isfinite (value),
                        "Quality path " + juce::String (quality)
                            + " produced non-finite audio");
                    energy += static_cast<double> (value) * value;
                }
        }
        context.expect (
            energy > 1.0e-8,
            "Quality path " + juce::String (quality) + " produced silence");
    }
}

double measureModeRms (
    int mode, bool autoGain, float mix, float secondary = 0.0f)
{
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 2);

    dd::Parameters parameters;
    parameters.mode = mode;
    parameters.driveDb = 20.0f;
    parameters.character =
        dd::DistortionEngine::isCharacterBipolar (mode) ? 0.35f : 0.70f;
    parameters.asymmetry = 0.12f;
    parameters.secondary = secondary;
    parameters.stages = 2;
    parameters.mix = mix;
    parameters.outputDb = 0.0f;
    parameters.quality = 0;
    parameters.autoGainMode = autoGain ? 1 : 0;
    engine.primeAutoGain (parameters);

    juce::AudioBuffer<float> buffer (2, blockSize);
    double phase = 0.0;
    double energy = 0.0;
    int countedSamples = 0;
    for (int block = 0; block < 180; ++block)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto value = 0.25118864f * static_cast<float> (
                std::sin (
                    juce::MathConstants<double>::twoPi * 173.0
                    * phase / sampleRate));
            for (int channel = 0;
                 channel < buffer.getNumChannels();
                 ++channel)
                buffer.setSample (channel, sample, value);
            phase += 1.0;
        }
        engine.process (buffer, parameters);
        if (block < 120)
            continue;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = static_cast<double> (
                    buffer.getSample (channel, sample));
                energy += value * value;
                ++countedSamples;
            }
    }

    return std::sqrt (energy / juce::jmax (1, countedSamples));
}

double measureConfiguredRms (dd::Parameters parameters,
                             bool autoGain,
                             float mix)
{
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 1);
    parameters.mix = mix;
    parameters.autoGainMode = autoGain ? 1 : 0;
    engine.primeAutoGain (parameters);
    juce::AudioBuffer<float> buffer (1, blockSize);
    double phase = 0.0;
    double energy = 0.0;
    int countedSamples = 0;
    for (int block = 0; block < 180; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
            buffer.setSample (0, sample, 0.25118864f * static_cast<float> (
                std::sin (
                    juce::MathConstants<double>::twoPi * 173.0
                    * phase++ / sampleRate)));
        engine.process (buffer, parameters);
        if (block < 120)
            continue;
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = static_cast<double> (buffer.getSample (0, sample));
            energy += value * value;
            ++countedSamples;
        }
    }
    return std::sqrt (energy / juce::jmax (1, countedSamples));
}

void testAutoGainForEveryMode (TestContext& context)
{
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
    {
        const auto reference = measureModeRms (mode, false, 0.0f);
        const auto uncompensated = measureModeRms (mode, false, 1.0f);
        const auto compensated = measureModeRms (mode, true, 1.0f);
        const auto uncompensatedError = std::abs (
            juce::Decibels::gainToDecibels (
                static_cast<float> (uncompensated / reference), -100.0f));
        const auto compensatedError = std::abs (
            juce::Decibels::gainToDecibels (
                static_cast<float> (compensated / reference), -100.0f));

        context.expect (
            std::isfinite (compensated) && compensated > 1.0e-8,
            "Auto Gain produced invalid output in mode "
                + juce::String (mode + 1));
        context.expect (
            compensatedError <= 6.0f,
            "Auto Gain is more than 6 dB from the dry RMS in mode "
                + juce::String (mode + 1)
                + " (error " + juce::String (compensatedError, 2)
                + " dB, dry " + juce::String (reference, 5)
                + ", wet " + juce::String (uncompensated, 5)
                + ", auto " + juce::String (compensated, 5) + ")");
        context.expect (
            compensatedError <= uncompensatedError + 0.75f,
            "Auto Gain makes level matching worse in mode "
                + juce::String (mode + 1));
    }
}

void testSpectralClipAutoGain (TestContext& context)
{
    const auto mode = static_cast<int> (
        dd::DistortionEngine::Mode::spectralClip);
    const auto dry = measureModeRms (mode, false, 0.0f);
    const auto uncompensated = measureModeRms (mode, false, 1.0f);
    const auto compensated = measureModeRms (mode, true, 1.0f);
    const auto errorDb = std::abs (juce::Decibels::gainToDecibels (
        static_cast<float> (compensated / dry), -100.0f));
    context.expect (
        errorDb <= 0.5f,
        "Spectral Clip deterministic Auto Gain misses dry RMS by "
            + juce::String (errorDb, 2) + " dB");
    context.expect (
        std::abs (compensated - dry) < std::abs (uncompensated - dry),
        "Spectral Clip deterministic Auto Gain does not improve level matching");
}

void testSineErosionNoiseAutoGain (TestContext& context)
{
    const auto mode = static_cast<int> (
        dd::DistortionEngine::Mode::sineErosion);
    const auto dry = measureModeRms (mode, false, 0.0f, 1.0f);
    const auto compensated = measureModeRms (mode, true, 1.0f, 1.0f);
    const auto errorDb = std::abs (juce::Decibels::gainToDecibels (
        static_cast<float> (compensated / dry), -100.0f));
    context.expect (
        std::isfinite (compensated) && errorDb <= 1.0f,
        "Sine Erosion Noise-aware Auto Gain misses the dry reference by "
            + juce::String (errorDb, 2) + " dB");
}

void testSecondaryControlAutoGain (TestContext& context)
{
    dd::Parameters parameters;
    parameters.driveDb = 18.0f;
    parameters.character = 0.75f;
    parameters.stages = 2;
    const auto dry = measureConfiguredRms (parameters, false, 0.0f);
    const auto expectCompensated = [&] (
        dd::DistortionEngine::Mode mode,
        auto setSecondary,
        const juce::String& name)
    {
        auto configured = parameters;
        configured.mode = static_cast<int> (mode);
        setSecondary (configured);
        const auto wet = measureConfiguredRms (configured, true, 1.0f);
        const auto referenceGain = dd::DistortionEngine::calculateReferenceAutoGain (
            configured, sampleRate);
        const auto errorDb = std::abs (juce::Decibels::gainToDecibels (
            static_cast<float> (wet / dry), -100.0f));
        context.expect (
            std::isfinite (wet)
                && errorDb <= (mode == dd::DistortionEngine::Mode::downsample
                        ? 2.0f
                        : 1.0f),
            name + "-aware Auto Gain misses dry RMS by "
                + juce::String (errorDb, 2) + " dB (reference gain "
                + juce::String (referenceGain, 4) + ")");
    };
    expectCompensated (
        dd::DistortionEngine::Mode::tapeHysteresis,
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Tape Bias");
    expectCompensated (
        dd::DistortionEngine::Mode::transformerCore,
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Transformer Air Gap");
    expectCompensated (
        dd::DistortionEngine::Mode::downsample,
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Downsample Jitter");
    expectCompensated (
        dd::DistortionEngine::Mode::bitCrusher,
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Bit Crusher Dither");
    expectCompensated (
        dd::DistortionEngine::Mode::schmittHysteresis,
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Schmitt Slew");
}

void testEveryCharacterHasARealVisualization (TestContext& context)
{
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
    {
        dd::Parameters minimum;
        minimum.mode = mode;
        minimum.driveDb = 18.0f;
        minimum.asymmetry = 0.13f;
        minimum.stages = 2;
        minimum.character =
            dd::DistortionEngine::isCharacterBipolar (mode) ? -1.0f : 0.0f;

        auto maximum = minimum;
        maximum.character = 1.0f;

        dd::DistortionEngine::Visualization low;
        dd::DistortionEngine::Visualization high;
        dd::DistortionEngine::makeVisualization (minimum, sampleRate, low);
        dd::DistortionEngine::makeVisualization (maximum, sampleRate, high);

        double difference = 0.0;
        for (int point = 0;
             point < dd::DistortionEngine::Visualization::pointCount;
             ++point)
        {
            const auto lowValue = low.output[static_cast<size_t> (point)];
            const auto highValue = high.output[static_cast<size_t> (point)];
            context.expect (
                std::isfinite (lowValue) && std::isfinite (highValue),
                "Visualization contains non-finite values in mode "
                    + juce::String (mode + 1));
            difference += std::abs (
                static_cast<double> (highValue - lowValue));
        }

        context.expect (
            difference > 0.01,
            "Character does not change the real visualization in mode "
                + juce::String (mode + 1));
    }
}

void testDownsampleExtreme (TestContext& context)
{
    const auto midpointText = dd::DistortionEngine::formatDriveValue (
        static_cast<int> (dd::DistortionEngine::Mode::downsample),
        18.0f,
        sampleRate);
    const auto maximumText = dd::DistortionEngine::formatDriveValue (
        static_cast<int> (dd::DistortionEngine::Mode::downsample),
        36.0f,
        sampleRate);
    context.expect (
        midpointText.containsIgnoreCase ("2.0 kHz"),
        "Downsample midpoint does not reach a two-kilohertz sample clock");
    context.expect (
        maximumText.containsIgnoreCase ("20.0 Hz"),
        "Downsample maximum does not reach a twenty-Hertz sample clock");
}

std::vector<double> buildModeSignature (int mode)
{
    std::vector<double> signature;
    const std::array<float, 3> characterValues =
        dd::DistortionEngine::isCharacterBipolar (mode)
            ? std::array<float, 3> { -0.85f, 0.0f, 0.85f }
            : std::array<float, 3> { 0.08f, 0.52f, 0.96f };

    for (const auto character : characterValues)
    {
        for (const auto drive : { 9.0f, 30.0f })
        {
            dd::DistortionEngine engine;
            engine.prepare (sampleRate, blockSize, 1);

            dd::Parameters parameters;
            parameters.mode = mode;
            parameters.driveDb = drive;
            parameters.character = character;
            parameters.asymmetry = 0.19f;
            parameters.stages = drive > 20.0f ? 4 : 1;
            parameters.mix = 1.0f;
            parameters.quality = 0;
            parameters.autoGainMode = 0;

            juce::AudioBuffer<float> buffer (1, blockSize);
            double phase = 0.0;
            for (int block = 0; block < 14; ++block)
            {
                fillSignal (buffer, phase);
                engine.process (buffer, parameters);
            }

            double energy = 0.0;
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = static_cast<double> (
                    buffer.getSample (0, sample));
                energy += value * value;
            }
            const auto rms = std::sqrt (energy / blockSize);
            signature.push_back (0.12 * std::log10 (rms + 1.0e-9));
            const auto normaliser = 1.0 / juce::jmax (1.0e-8, rms);
            for (int sample = 0; sample < blockSize; sample += 4)
                signature.push_back (
                    static_cast<double> (buffer.getSample (0, sample))
                    * normaliser);
        }
    }
    return signature;
}

void testModesArePairwiseDistinct (TestContext& context)
{
    std::array<std::vector<double>, dd::DistortionEngine::modeCount> signatures;
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
        signatures[static_cast<size_t> (mode)] = buildModeSignature (mode);

    auto closestDistance = std::numeric_limits<double>::max();
    int closestA = -1;
    int closestB = -1;
    for (int first = 0; first < dd::DistortionEngine::modeCount; ++first)
        for (int second = first + 1;
             second < dd::DistortionEngine::modeCount;
             ++second)
        {
            const auto& a = signatures[static_cast<size_t> (first)];
            const auto& b = signatures[static_cast<size_t> (second)];
            double squaredDifference = 0.0;
            double squaredMagnitude = 0.0;
            for (size_t feature = 0; feature < a.size(); ++feature)
            {
                const auto difference = a[feature] - b[feature];
                squaredDifference += difference * difference;
                squaredMagnitude += a[feature] * a[feature]
                    + b[feature] * b[feature];
            }
            const auto distance = std::sqrt (
                squaredDifference / juce::jmax (1.0e-12, squaredMagnitude));
            if (distance < closestDistance)
            {
                closestDistance = distance;
                closestA = first;
                closestB = second;
            }
        }

    const auto& names = dd::DistortionEngine::getModeNames();
    context.expect (
        closestDistance > 0.015,
        "Two modes are behaviourally too similar: "
            + names[static_cast<size_t> (closestA)] + " and "
            + names[static_cast<size_t> (closestB)]
            + " (normalised distance "
            + juce::String (closestDistance, 5) + ")");
}

void testAlgorithmIntentInvariants (TestContext& context)
{
    dd::Parameters parameters;
    parameters.driveDb = 6.0f;
    parameters.stages = 1;
    parameters.asymmetry = 0.0f;

    dd::DistortionEngine::Visualization view;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::topologyFold);
    parameters.character = 0.0f;
    dd::DistortionEngine::makeVisualization (parameters, sampleRate, view);
    auto closestPositive = 0;
    auto closestError = std::numeric_limits<float>::max();
    for (int point = 0;
         point < dd::DistortionEngine::Visualization::pointCount;
         ++point)
    {
        const auto error = std::abs (
            view.input[static_cast<size_t> (point)] - 0.5f);
        if (error < closestError)
        {
            closestError = error;
            closestPositive = point;
        }
    }
    context.expect (
        view.output[static_cast<size_t> (closestPositive)] > 0.0f,
        "Topology Fold reverses polarity inside its no-fold interval");

    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::fullWaveRectifier);
    parameters.driveDb = 36.0f;
    parameters.character = 1.0f;
    dd::DistortionEngine::makeVisualization (parameters, sampleRate, view);
    for (const auto value : view.output)
        context.expect (
            value >= -1.0e-6f,
            "Full-Wave Rectifier produced a negative transfer value");

    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::hardClip);
    parameters.driveDb = 0.0f;
    parameters.character = 0.0f;
    dd::DistortionEngine::makeVisualization (parameters, sampleRate, view);
    for (size_t point = 0; point < view.input.size(); ++point)
        context.expect (
            std::abs (view.output[point] - view.input[point]) < 1.0e-6f,
            "Hard Clip visualization clips a full-scale signal at Drive 0");

    parameters.driveDb = 36.0f;
    parameters.character = 1.0f;
    dd::DistortionEngine::makeVisualization (parameters, sampleRate, view);
    for (const auto value : view.output)
        context.expect (
            std::abs (value) <= 1.000001f,
            "Hard Clip knee exceeds its clipping bounds");

    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::spectralClip);
    parameters.driveDb = 24.0f;
    parameters.character = 1.0f;
    dd::DistortionEngine::makeVisualization (parameters, sampleRate, view);
    double spectralInputEnergy = 0.0;
    double spectralOutputEnergy = 0.0;
    double spectralDifference = 0.0;
    for (size_t point = 0; point < view.output.size(); ++point)
    {
        context.expect (
            std::isfinite (view.output[point]),
            "Spectral Clip time-domain visualization is not finite");
        spectralInputEnergy += static_cast<double> (
            view.input[point]) * view.input[point];
        spectralOutputEnergy += static_cast<double> (
            view.output[point]) * view.output[point];
        spectralDifference += std::abs (
            static_cast<double> (
                view.output[point] - view.input[point]));
    }
    context.expect (
        view.timeDomain && ! view.spectralDomain,
        "Spectral Clip visualization is not an actual time-domain signal");
    context.expect (
        spectralOutputEnergy < spectralInputEnergy
            && spectralDifference > 0.1,
        "Spectral Clip time-domain visualization does not show spectral clipping");
    context.expect (
        std::abs (view.input.front()) < 1.0e-6f
            && std::abs (view.input[95] - 1.0f) < 1.0e-6f
            && std::abs (view.input[96] + 1.0f) < 1.0e-6f
            && std::abs (view.input.back()) < 1.0e-6f,
        "Spectral Clip saw phase is not 0 -> +1 / -1 -> 0");
}

void testEveryModeAtMaximum (TestContext& context)
{
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
    {
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, blockSize, 2);

        dd::Parameters parameters;
        parameters.mode = mode;
        parameters.driveDb = 36.0f;
        parameters.character = 1.0f;
        parameters.asymmetry = 0.85f;
        parameters.tone = 1.0f;
        parameters.stages = 8;
        parameters.mix = 1.0f;
        parameters.outputDb = 12.0f;
        parameters.quality = 3;
        parameters.autoGainMode = 1;
        engine.primeAutoGain (parameters);

        juce::AudioBuffer<float> buffer (2, blockSize);
        double phase = 0.0;
        double maximumMagnitude = 0.0;
        for (int block = 0; block < 100; ++block)
        {
            fillSignal (buffer, phase);
            engine.process (buffer, parameters);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    const auto value = static_cast<double> (
                        buffer.getSample (channel, sample));
                    context.expect (
                        std::isfinite (value),
                        "Maximum settings produced non-finite audio in mode "
                            + juce::String (mode + 1));
                    maximumMagnitude = juce::jmax (
                        maximumMagnitude, std::abs (value));
                }
        }
        context.expect (
            maximumMagnitude < 4096.0,
            "Maximum settings produced a runaway level in mode "
                + juce::String (mode + 1)
                + " (peak " + juce::String (maximumMagnitude, 2) + ")");
    }
}

double visualizationDistance (
    const dd::DistortionEngine::Visualization& first,
    const dd::DistortionEngine::Visualization& second)
{
    double difference = 0.0;
    for (size_t point = 0; point < first.output.size(); ++point)
        difference += std::abs (
            static_cast<double> (first.output[point] - second.output[point]));
    return difference / static_cast<double> (first.output.size());
}

void testDriveStartsContinuously (TestContext& context)
{
    const std::array<std::pair<dd::DistortionEngine::Mode, float>, 6> cases {
        std::pair { dd::DistortionEngine::Mode::morphSoftClip, 0.35f },
        std::pair { dd::DistortionEngine::Mode::hardClip, 0.35f },
        std::pair { dd::DistortionEngine::Mode::softFullWaveRectifier, 0.65f },
        std::pair { dd::DistortionEngine::Mode::signSquare, 0.0f },
        std::pair { dd::DistortionEngine::Mode::zeroSquare, 0.75f },
        std::pair { dd::DistortionEngine::Mode::fullWaveRectifier, 0.5f }
    };

    for (const auto& [mode, character] : cases)
    {
        dd::Parameters parameters;
        parameters.mode = static_cast<int> (mode);
        parameters.driveDb = 0.0f;
        parameters.character = character;
        parameters.stages = 1;
        dd::DistortionEngine::Visualization zero;
        dd::DistortionEngine::Visualization tiny;
        dd::DistortionEngine::Visualization medium;
        dd::DistortionEngine::Visualization maximum;
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, zero);
        parameters.driveDb = 0.01f;
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, tiny);
        parameters.driveDb = 12.0f;
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, medium);
        parameters.driveDb = 36.0f;
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, maximum);

        const auto tinyChange = visualizationDistance (zero, tiny);
        const auto mediumChange = visualizationDistance (zero, medium);
        const auto maximumChange = visualizationDistance (zero, maximum);
        context.expect (
            tinyChange < 0.0015,
            "Drive has a discontinuity immediately above zero in mode "
                + juce::String (parameters.mode + 1));
        context.expect (
            mediumChange > tinyChange * 20.0
                && maximumChange > mediumChange * 1.1,
            "Drive does not provide a useful gradual range in mode "
                + juce::String (parameters.mode + 1));
    }

    for (const auto mode : {
             dd::DistortionEngine::Mode::morphSoftClip,
             dd::DistortionEngine::Mode::softFullWaveRectifier })
    {
        dd::Parameters parameters;
        parameters.mode = static_cast<int> (mode);
        parameters.driveDb = 0.0f;
        parameters.character =
            mode == dd::DistortionEngine::Mode::morphSoftClip ? 0.35f : 0.65f;
        dd::DistortionEngine::Visualization view;
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, view);
        double activeEffect = 0.0;
        for (size_t point = 0; point < view.output.size(); ++point)
            activeEffect += std::abs (
                static_cast<double> (
                    view.output[point] - view.input[point]));
        context.expect (
            activeEffect > 0.1,
            "The zero-Drive transfer is unexpectedly bypassed in mode "
                + juce::String (parameters.mode + 1));
    }

    dd::Parameters zeroSquare;
    zeroSquare.mode = static_cast<int> (
        dd::DistortionEngine::Mode::zeroSquare);
    zeroSquare.driveDb = 24.0f;
    zeroSquare.character = 0.0f;
    dd::DistortionEngine::Visualization view;
    dd::DistortionEngine::makeVisualization (
        zeroSquare, sampleRate, view);
    double xx = 0.0;
    double xy = 0.0;
    for (size_t point = 0; point < view.input.size(); ++point)
    {
        xx += static_cast<double> (view.input[point]) * view.input[point];
        xy += static_cast<double> (view.input[point]) * view.output[point];
    }
    const auto bestLinearGain = xy / juce::jmax (1.0e-12, xx);
    double nonlinearResidual = 0.0;
    for (size_t point = 0; point < view.input.size(); ++point)
    {
        const auto error = view.output[point]
            - static_cast<float> (bestLinearGain) * view.input[point];
        nonlinearResidual += static_cast<double> (error) * error;
    }
    context.expect (
        nonlinearResidual > 0.2,
        "Zero-Square at Character 0 behaves like a volume control");
}

void testDigitalClockIgnoresOversampling (TestContext& context)
{
    dd::DistortionEngine baseRate;
    dd::DistortionEngine oversampledSetting;
    baseRate.prepare (sampleRate, blockSize, 1);
    oversampledSetting.prepare (sampleRate, blockSize, 1);

    dd::Parameters parameters;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::downsample);
    parameters.driveDb = 18.0f;
    parameters.character = 0.63f;
    parameters.stages = 2;
    parameters.autoGainMode = 0;

    juce::AudioBuffer<float> first (1, blockSize);
    juce::AudioBuffer<float> second (1, blockSize);
    double phase = 0.0;
    auto maximumDifference = 0.0f;
    for (int block = 0; block < 24; ++block)
    {
        fillSignal (first, phase);
        second.makeCopyOf (first);
        parameters.quality = 0;
        baseRate.process (first, parameters);
        parameters.quality = 3;
        oversampledSetting.process (second, parameters);
        for (int sample = 0; sample < blockSize; ++sample)
            maximumDifference = juce::jmax (
                maximumDifference,
                std::abs (
                    first.getSample (0, sample)
                    - second.getSample (0, sample)));
    }
    context.expect (
        maximumDifference < 1.0e-7f,
        "Downsample clock changes when Oversampling is enabled");
}

double measureTapeRmsAtQuality (int quality, TestContext& context)
{
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 1);

    dd::Parameters parameters;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::tapeHysteresis);
    parameters.driveDb = 36.0f;
    parameters.character = 0.5f;
    parameters.secondary = 0.5f;
    parameters.asymmetry = 0.0f;
    parameters.stages = 1;
    parameters.mix = 1.0f;
    parameters.outputDb = 0.01f;
    parameters.quality = quality;
    parameters.autoGainMode = 0;

    juce::AudioBuffer<float> buffer (1, blockSize);
    double phase = 0.0;
    double energy = 0.0;
    int measuredSamples = 0;
    constexpr int warmupBlocks = 48;
    constexpr int measurementBlocks = 32;
    for (int block = 0; block < warmupBlocks + measurementBlocks; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            buffer.setSample (
                0,
                sample,
                0.35f * static_cast<float> (std::sin (phase)));
            phase += juce::MathConstants<double>::twoPi
                * 997.0 / sampleRate;
        }
        engine.process (buffer, parameters);
        if (block >= warmupBlocks)
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = buffer.getSample (0, sample);
                context.expect (
                    std::isfinite (value),
                    "Tape produced a non-finite sample at quality "
                        + juce::String (quality));
                energy += static_cast<double> (value) * value;
                ++measuredSamples;
            }
    }
    return std::sqrt (energy / juce::jmax (1, measuredSamples));
}

void testTapeOversamplingConsistency (TestContext& context)
{
    std::array<double, 4> rms {};
    for (int quality = 0; quality < static_cast<int> (rms.size()); ++quality)
        rms[static_cast<size_t> (quality)] =
            measureTapeRmsAtQuality (quality, context);

    const auto [minimum, maximum] = std::minmax_element (
        rms.begin(), rms.end());
    const auto spreadDb = 20.0 * std::log10 (
        *maximum / juce::jmax (1.0e-12, *minimum));
    context.expect (
        std::isfinite (spreadDb) && spreadDb <= 1.0,
        "Tape level changes too much across Oversampling settings ("
            + juce::String (spreadDb, 2) + " dB)");
}

struct TapeSteadyStateMetrics
{
    double mean = 0.0;
    double acRms = 0.0;
    double cycleError = 0.0;
    double positivePeak = 0.0;
    double negativePeak = 0.0;
    bool finite = true;
};

TapeSteadyStateMetrics measureTapeSteadyState (float hysteresis,
                                                float bias,
                                                int stages,
                                                float driveDb = 36.0f,
                                                bool autoGain = false)
{
    constexpr int tapeBlockSize = 480;
    constexpr int periodSamples = 48;
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, tapeBlockSize, 1);

    dd::Parameters parameters;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::tapeHysteresis);
    parameters.driveDb = driveDb;
    parameters.character = hysteresis;
    parameters.secondary = bias;
    parameters.asymmetry = 0.0f;
    parameters.stages = stages;
    parameters.mix = 1.0f;
    parameters.outputDb = 0.01f;
    parameters.quality = 1;
    parameters.autoGainMode = autoGain ? 1 : 0;
    engine.primeAutoGain (parameters);

    juce::AudioBuffer<float> buffer (1, tapeBlockSize);
    std::array<float, tapeBlockSize> captured {};
    double phase = 0.0;
    for (int block = 0; block < 160; ++block)
    {
        for (int sample = 0; sample < tapeBlockSize; ++sample)
        {
            buffer.setSample (
                0,
                sample,
                0.35f * static_cast<float> (std::sin (phase)));
            phase += juce::MathConstants<double>::twoPi
                / static_cast<double> (periodSamples);
        }
        engine.process (buffer, parameters);
        if (block == 159)
            std::copy (
                buffer.getReadPointer (0),
                buffer.getReadPointer (0) + tapeBlockSize,
                captured.begin());
    }

    TapeSteadyStateMetrics result;
    for (const auto value : captured)
    {
        result.finite = result.finite && std::isfinite (value);
        result.mean += value;
        result.positivePeak = juce::jmax (
            result.positivePeak, static_cast<double> (value));
        result.negativePeak = juce::jmax (
            result.negativePeak, -static_cast<double> (value));
    }
    result.mean /= static_cast<double> (captured.size());
    for (const auto value : captured)
    {
        const auto centred = static_cast<double> (value) - result.mean;
        result.acRms += centred * centred;
    }
    result.acRms = std::sqrt (
        result.acRms / static_cast<double> (captured.size()));

    double cycleDifference = 0.0;
    for (int sample = tapeBlockSize - periodSamples;
         sample < tapeBlockSize;
         ++sample)
    {
        const auto difference = static_cast<double> (
            captured[static_cast<size_t> (sample)]
            - captured[static_cast<size_t> (sample - periodSamples)]);
        cycleDifference += difference * difference;
    }
    result.cycleError = std::sqrt (
        cycleDifference / static_cast<double> (periodSamples))
        / juce::jmax (1.0e-12, result.acRms);
    return result;
}

void testTapeDoesNotCollapseOrModulate (TestContext& context)
{
    const auto referenceModel = dd::chowtape::makeModel (
        1.0, 1.0, 0.5);
    context.expect (
        std::abs (referenceModel.saturation - 0.5) < 1.0e-12
            && std::abs (
                referenceModel.domainScale - 0.5 / 6.01) < 1.0e-12
            && std::abs (
                referenceModel.reversibility
                    - (std::sqrt (0.5) - 0.01)) < 1.0e-12
            && std::abs (referenceModel.outputMakeup - 2.6) < 1.0e-12,
        "Tape controls no longer map to the CHOW reference model");

    for (const auto hysteresis : { 0.0f, 1.0f })
        for (const auto bias : { 0.5f, 1.0f })
            for (const auto stages : { 1, 8 })
            {
                const auto metrics = measureTapeSteadyState (
                    hysteresis, bias, stages);
                const auto description =
                    " at Hysteresis " + juce::String (hysteresis)
                    + ", Bias " + juce::String (bias)
                    + ", Stages " + juce::String (stages);
                context.expect (
                    metrics.finite && metrics.acRms > 1.0e-3,
                    "Tape collapsed to DC" + description);
                context.expect (
                    std::abs (metrics.mean)
                        <= 0.1 * juce::jmax (1.0e-12, metrics.acRms),
                    "Tape retained excessive DC" + description
                        + " (DC/AC "
                        + juce::String (
                            std::abs (metrics.mean)
                                / juce::jmax (1.0e-12, metrics.acRms),
                            3)
                        + ")");
                context.expect (
                    std::isfinite (metrics.cycleError)
                        && metrics.cycleError < 0.01,
                    "Tape has a non-periodic modulation component"
                        + description + " (cycle error "
                        + juce::String (metrics.cycleError, 4) + ")");
            }

    const auto referenceCascade = measureTapeSteadyState (1.0f, 0.5f, 8);
    context.expect (
        std::abs (referenceCascade.acRms - 1.188) < 0.025,
        "Tape stages no longer retain the inter-stage CHOW oversampling "
        "response (RMS " + juce::String (referenceCascade.acRms, 4) + ")");

    const auto defaultDry = measureTapeSteadyState (
        0.5f, 0.5f, 1, 0.0f, false);
    const auto halfDbDry = measureTapeSteadyState (
        0.5f, 0.5f, 1, 0.5f, false);
    const auto defaultAuto = measureTapeSteadyState (
        0.5f, 0.5f, 1, 0.0f, true);
    constexpr auto inputRms = 0.35 / 1.4142135623730951;
    const auto firstHalfDbJump = 20.0 * std::log10 (
        juce::jmax (1.0e-12, halfDbDry.acRms)
        / juce::jmax (1.0e-12, defaultDry.acRms));
    const auto autoGainError = 20.0 * std::log10 (
        juce::jmax (1.0e-12, defaultAuto.acRms) / inputRms);
    const auto peakImbalance = std::abs (
        defaultDry.positivePeak - defaultDry.negativePeak)
        / juce::jmax (
            1.0e-12,
            juce::jmax (
                defaultDry.positivePeak, defaultDry.negativePeak));
    context.expect (
        defaultDry.acRms >= 0.5 * inputRms,
        "Default Tape collapses when Auto Gain is disabled (RMS "
            + juce::String (defaultDry.acRms, 5) + ")");
    context.expect (
        std::abs (firstHalfDbJump) <= 1.0,
        "Default Tape level jumps within the first 0.5 dB of Drive ("
            + juce::String (firstHalfDbJump, 2) + " dB)");
    context.expect (
        peakImbalance <= 0.05,
        "Default Tape creates an imbalanced positive/negative waveform ("
            + juce::String (100.0 * peakImbalance, 2) + "%)");
    context.expect (
        std::abs (autoGainError) <= 1.0,
        "Default Tape Auto Gain misses the dry RMS by "
            + juce::String (autoGainError, 2) + " dB");
}

double measureAliasComponent (int oversampling)
{
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 1);
    dd::Parameters parameters;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::hardClip);
    parameters.driveDb = 36.0f;
    parameters.character = 0.0f;
    parameters.stages = 1;
    parameters.quality = oversampling;
    parameters.autoGainMode = 0;

    juce::AudioBuffer<float> buffer (1, blockSize);
    std::vector<float> captured;
    double samplePosition = 0.0;
    for (int block = 0; block < 30; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            buffer.setSample (
                0,
                sample,
                0.9f * static_cast<float> (std::sin (
                    juce::MathConstants<double>::twoPi
                    * 9000.0 * samplePosition / sampleRate)));
            samplePosition += 1.0;
        }
        engine.process (buffer, parameters);
        if (block >= 22)
            captured.insert (
                captured.end(),
                buffer.getReadPointer (0),
                buffer.getReadPointer (0) + blockSize);
    }

    double cosine = 0.0;
    double sine = 0.0;
    for (size_t sample = 0; sample < captured.size(); ++sample)
    {
        const auto angle = juce::MathConstants<double>::twoPi
            * 21000.0 * static_cast<double> (sample) / sampleRate;
        cosine += static_cast<double> (captured[sample]) * std::cos (angle);
        sine += static_cast<double> (captured[sample]) * std::sin (angle);
    }
    return 2.0 * std::sqrt (cosine * cosine + sine * sine)
        / static_cast<double> (captured.size());
}

void testOversamplingReducesAliasing (TestContext& context)
{
    const auto baseAlias = measureAliasComponent (0);
    const auto oversampledAlias = measureAliasComponent (3);
    context.expect (
        oversampledAlias < baseAlias * 0.45,
        "8x Oversampling does not materially reduce Hard Clip aliasing "
        "(base " + juce::String (baseAlias, 5)
            + ", 8x " + juce::String (oversampledAlias, 5) + ")");
}

void testSmartAutoGainFreezes (TestContext& context)
{
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 1);

    dd::Parameters parameters;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::hardClip);
    parameters.driveDb = 6.0f;
    parameters.character = 0.45f;
    parameters.autoGainMode = 2;
    engine.primeAutoGain (parameters);

    juce::AudioBuffer<float> buffer (1, blockSize);
    double phase = 0.0;
    auto runAndMeasure = [&] (float level, int blocks)
    {
        double energy = 0.0;
        int count = 0;
        for (int block = 0; block < blocks; ++block)
        {
            fillSignal (buffer, phase);
            buffer.applyGain (level);
            engine.process (buffer, parameters);
            if (block < blocks - 30)
                continue;
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = buffer.getSample (0, sample);
                energy += static_cast<double> (value) * value;
                ++count;
            }
        }
        return std::sqrt (energy / juce::jmax (1, count));
    };

    const auto loud = runAndMeasure (1.0f, 120);
    context.expect (
        engine.isSmartAutoGainLocked()
            && engine.getSmartAutoGainProgress() >= 0.999f,
        "Smart Auto Gain did not finish and expose its loading progress");
    const auto quiet = runAndMeasure (0.2f, 80);
    const auto ratio = quiet / juce::jmax (1.0e-9, loud);
    context.expect (
        ratio > 0.08 && ratio < 0.5,
        "Smart Auto Gain follows programme level like a compressor instead of "
        "freezing its correction (ratio " + juce::String (ratio, 3) + ")");

    parameters.tone = 0.72f;
    fillSignal (buffer, phase);
    engine.process (buffer, parameters);
    context.expect (
        ! engine.isSmartAutoGainLocked()
            && engine.getSmartAutoGainProgress() < 0.999f,
        "Tone does not restart Smart Auto Gain measurement");
}

void testStereoAsymmetryUsesOppositePolarities (TestContext& context)
{
    dd::DistortionEngine stereo;
    dd::DistortionEngine positive;
    dd::DistortionEngine negative;
    stereo.prepare (sampleRate, blockSize, 2);
    positive.prepare (sampleRate, blockSize, 1);
    negative.prepare (sampleRate, blockSize, 1);

    dd::Parameters stereoParameters;
    stereoParameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::diodeClipper);
    stereoParameters.driveDb = 19.0f;
    stereoParameters.character = 0.64f;
    stereoParameters.asymmetry = 0.7f;
    stereoParameters.asymmetryStereo = true;
    stereoParameters.stages = 2;
    stereoParameters.autoGainMode = 0;

    auto positiveParameters = stereoParameters;
    positiveParameters.asymmetryStereo = false;
    auto negativeParameters = positiveParameters;
    negativeParameters.asymmetry = -stereoParameters.asymmetry;

    juce::AudioBuffer<float> stereoBuffer (2, blockSize);
    juce::AudioBuffer<float> positiveBuffer (1, blockSize);
    juce::AudioBuffer<float> negativeBuffer (1, blockSize);
    double phase = 0.0;
    double maximumError = 0.0;
    double channelDifference = 0.0;

    for (int block = 0; block < 40; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto time = phase++ / sampleRate;
            const auto value = static_cast<float> (
                0.72 * std::sin (
                    juce::MathConstants<double>::twoPi * 317.0 * time));
            stereoBuffer.setSample (0, sample, value);
            stereoBuffer.setSample (1, sample, value);
            positiveBuffer.setSample (0, sample, value);
            negativeBuffer.setSample (0, sample, value);
        }

        stereo.process (stereoBuffer, stereoParameters);
        positive.process (positiveBuffer, positiveParameters);
        negative.process (negativeBuffer, negativeParameters);

        if (block < 8)
            continue;
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto left = stereoBuffer.getSample (0, sample);
            const auto right = stereoBuffer.getSample (1, sample);
            maximumError = juce::jmax (
                maximumError,
                std::abs (static_cast<double> (
                    left - positiveBuffer.getSample (0, sample))));
            maximumError = juce::jmax (
                maximumError,
                std::abs (static_cast<double> (
                    right - negativeBuffer.getSample (0, sample))));
            channelDifference += std::abs (
                static_cast<double> (left - right));
        }
    }

    context.expect (
        maximumError < 1.0e-5,
        "Stereo Asym does not match +ASYM left / -ASYM right");
    context.expect (
        channelDifference > 1.0,
        "Stereo Asym produces identical left and right channels");
}

void testNewTopologyAndSafetyInvariants (TestContext& context)
{
    dd::Parameters parameters;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::topologyFold);
    parameters.driveDb = 20.0f;
    parameters.stages = 1;

    std::array<dd::DistortionEngine::Visualization, 3> views;
    const std::array<float, 3> topologyValues { 0.0f, 0.5f, 1.0f };
    for (size_t topology = 0; topology < topologyValues.size(); ++topology)
    {
        parameters.character = topologyValues[topology];
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, views[topology]);
    }
    for (int first = 0; first < 3; ++first)
        for (int second = first + 1; second < 3; ++second)
        {
            const auto firstIndex = static_cast<size_t> (first);
            const auto secondIndex = static_cast<size_t> (second);
            double difference = 0.0;
            for (size_t point = 0;
                 point < views[firstIndex].output.size();
                 ++point)
                difference += std::abs (
                    static_cast<double> (
                        views[firstIndex].output[point]
                        - views[secondIndex].output[point]));
            context.expect (
                difference > 0.5,
                "Two Topology Fold options produce the same transfer");
        }

    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::classBSaturation);
    parameters.driveDb = 30.0f;
    const std::array<float, 3> crossoverWidths { 0.0f, 0.5f, 1.0f };
    for (size_t operation = 0; operation < crossoverWidths.size(); ++operation)
    {
        parameters.character = crossoverWidths[operation];
        dd::DistortionEngine::makeVisualization (
            parameters, sampleRate, views[operation]);
    }
    for (int first = 0; first < 3; ++first)
        for (int second = first + 1; second < 3; ++second)
        {
            const auto difference = visualizationDistance (
                views[static_cast<size_t> (first)],
                views[static_cast<size_t> (second)]);
            context.expect (
                difference > 0.02,
                "Class-B Saturation Character has too little influence");
        }

    dd::DistortionEngine schmitt;
    schmitt.prepare (sampleRate, blockSize, 1);
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::schmittHysteresis);
    parameters.driveDb = 36.0f;
    parameters.character = 1.0f;
    parameters.stages = 8;
    parameters.autoGainMode = 0;
    juce::AudioBuffer<float> buffer (1, blockSize);
    double phase = 0.0;
    auto peak = 0.0f;
    for (int block = 0; block < 32; ++block)
    {
        fillSignal (buffer, phase);
        schmitt.process (buffer, parameters);
        peak = juce::jmax (peak, buffer.getMagnitude (0, 0, blockSize));
    }
    context.expect (
        peak <= 0.981f,
        "Schmitt Hysteresis exceeds 0 dBFS at Output 0 dB");
}

void testOutputCeilingAtZeroDb (TestContext& context)
{
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
    {
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, blockSize, 1);
        dd::Parameters parameters;
        parameters.mode = mode;
        parameters.driveDb = 36.0f;
        parameters.character = 0.8f;
        parameters.asymmetry = 0.7f;
        parameters.tone = 0.8f;
        parameters.stages = 8;
        parameters.outputDb = 0.0f;
        parameters.quality = 3;
        parameters.autoGainMode = 1;
        engine.primeAutoGain (parameters);

        juce::AudioBuffer<float> buffer (1, blockSize);
        double phase = 0.0;
        auto peak = 0.0f;
        for (int block = 0; block < 24; ++block)
        {
            fillSignal (buffer, phase);
            engine.process (buffer, parameters);
            peak = juce::jmax (
                peak, buffer.getMagnitude (0, 0, blockSize));
        }
        context.expect (
            peak <= 1.00001f,
            "Output exceeds 0 dBFS at Output 0 dB in mode "
                + juce::String (mode + 1));
    }
}

void testInstantTableAutoGain (TestContext& context)
{
    auto engine = std::make_unique<dd::DistortionEngine>();
    engine->prepare (sampleRate, blockSize, 2);
    juce::AudioBuffer<float> buffer (2, blockSize);
    dd::Parameters parameters;
    parameters.autoGainMode = 1;
    parameters.mix = 1.0f;
    double phase = 0.0;

    for (int block = 0; block < 96; ++block)
    {
        parameters.mode = block % dd::DistortionEngine::modeCount;
        parameters.driveDb = static_cast<float> ((block * 7) % 37);
        parameters.character = static_cast<float> ((block * 11) % 101) / 100.0f;
        parameters.asymmetry =
            static_cast<float> ((block * 13) % 101) / 100.0f - 0.5f;
        parameters.stages = 1 + block % dd::DistortionEngine::maximumStages;
        fillSignal (buffer, phase);
        engine->process (buffer, parameters);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                context.expect (
                    std::isfinite (buffer.getSample (channel, sample)),
                    "Table Auto Gain produced invalid audio during automation");
    }

    engine.reset();
}

void testRevisedAlgorithmContracts (TestContext& context)
{
    const auto bitCrusherMode = static_cast<int> (
        dd::DistortionEngine::Mode::bitCrusher);
    context.expect (
        dd::DistortionEngine::formatDriveValue (
            bitCrusherMode, 0.0f, sampleRate).contains ("24 bit")
        && dd::DistortionEngine::formatDriveValue (
            bitCrusherMode, 36.0f, sampleRate).contains ("1 bit"),
        "Bit Crusher Drive does not span 24-bit through 1-bit");

    dd::Parameters parameters;
    parameters.mode = bitCrusherMode;
    parameters.driveDb = 36.0f;
    parameters.character = 0.0f;
    parameters.stages = 1;
    dd::DistortionEngine::Visualization rawCrusher;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, rawCrusher);
    parameters.character = 1.0f;
    dd::DistortionEngine::Visualization smoothCrusher;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, smoothCrusher);

    auto roughness = [] (const dd::DistortionEngine::Visualization& view)
    {
        double total = 0.0;
        for (size_t point = 1; point < view.output.size(); ++point)
            total += std::abs (
                static_cast<double> (
                    view.output[point] - view.output[point - 1]));
        return total;
    };
    auto errorRoughness = [] (
        const dd::DistortionEngine::Visualization& view)
    {
        double total = 0.0;
        for (size_t point = 1; point < view.output.size(); ++point)
        {
            const auto previousError =
                view.output[point - 1] - view.input[point - 1];
            const auto error = view.output[point] - view.input[point];
            total += std::abs (
                static_cast<double> (error - previousError));
        }
        return total;
    };
    parameters.character = 0.5f;
    dd::DistortionEngine::Visualization midpointCrusher;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, midpointCrusher);

    const auto rawCrusherRoughness = errorRoughness (rawCrusher);
    const auto smoothCrusherRoughness = errorRoughness (smoothCrusher);
    context.expect (
        smoothCrusherRoughness < 0.8 * rawCrusherRoughness,
        "Bit Crusher Character does not smooth the full-strength quantizer (raw "
            + juce::String (rawCrusherRoughness, 3)
            + ", smooth " + juce::String (smoothCrusherRoughness, 3) + ")");
    context.expect (
        visualizationDistance (rawCrusher, midpointCrusher) > 0.005
            && visualizationDistance (midpointCrusher, smoothCrusher) > 0.02,
        "Bit Crusher Smoothing does not preserve a useful 50-100% range");

    context.expect (
        std::abs (
            dd::DistortionEngine::getDefaultCharacter (
                static_cast<int> (
                    dd::DistortionEngine::Mode::classBSaturation))
            - 0.5f) < 1.0e-6f,
        "Class-B Saturation does not open at 50% Character");
    context.expect (
        std::abs (
            dd::DistortionEngine::getDefaultCharacter (
                static_cast<int> (
                    dd::DistortionEngine::Mode::phaseDistortion))
            - 0.5f) < 1.0e-6f,
        "Phase Distortion modulator Tone does not open at 50%");

    const auto downsampleMode = static_cast<int> (
        dd::DistortionEngine::Mode::downsample);
    context.expect (
        dd::DistortionEngine::formatDriveValue (
            downsampleMode, 18.0f, sampleRate).containsIgnoreCase ("2.0 kHz")
            && dd::DistortionEngine::formatDriveValue (
                downsampleMode, 36.0f, sampleRate).containsIgnoreCase (
                    "20.0 Hz"),
        "Downsample Drive scale does not map 50% to 2 kHz and 100% to 20 Hz");
    parameters.mode = downsampleMode;
    parameters.character = 0.0f;
    dd::DistortionEngine::Visualization rawDownsample;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, rawDownsample);
    parameters.character = 1.0f;
    dd::DistortionEngine::Visualization smoothDownsample;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, smoothDownsample);
    context.expect (
        roughness (smoothDownsample) < roughness (rawDownsample),
        "Downsample Character does not smooth sample-and-hold transitions");

    parameters.driveDb = 18.0f;
    parameters.character = 0.65f;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::fullWaveRectifier);
    dd::DistortionEngine::Visualization fullWave;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, fullWave);
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::softFullWaveRectifier);
    dd::DistortionEngine::Visualization softFullWave;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, softFullWave);
    context.expect (
        visualizationDistance (fullWave, softFullWave) > 0.08,
        "Soft Full-Wave has collapsed into Full-Wave Rectifier");

    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::sineErosion);
    parameters.character = 0.0f;
    dd::DistortionEngine::Visualization zeroHertzErosion;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, zeroHertzErosion);
    parameters.character = 1.0f;
    dd::DistortionEngine::Visualization highFrequencyErosion;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, highFrequencyErosion);
    context.expect (
        visualizationDistance (
            zeroHertzErosion, highFrequencyErosion) > 0.05,
        "Sine Erosion Frequency has too little audible range");
    context.expect (
        dd::DistortionEngine::formatCharacterValue (
            static_cast<int> (dd::DistortionEngine::Mode::sineErosion),
            0.5f).containsIgnoreCase ("1.00 kHz")
            && dd::DistortionEngine::formatCharacterValue (
                static_cast<int> (
                    dd::DistortionEngine::Mode::sineErosion),
                1.0f).containsIgnoreCase ("10.0 kHz"),
        "Sine Erosion Frequency scale does not map 50% to 1 kHz and 100% to 10 kHz");

    parameters.character = 0.5f;
    parameters.secondary = 0.0f;
    dd::DistortionEngine::Visualization sineErosion;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, sineErosion);
    parameters.secondary = 1.0f;
    dd::DistortionEngine::Visualization pinkNoiseErosion;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, pinkNoiseErosion);
    context.expect (
        visualizationDistance (
            sineErosion, pinkNoiseErosion) > 0.04,
        "Sine Erosion Noise does not morph toward filtered pink noise");

    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::signSquare);
    parameters.driveDb = 24.0f;
    parameters.character = -1.0f;
    dd::DistortionEngine::Visualization lowThreshold;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, lowThreshold);
    parameters.character = 1.0f;
    dd::DistortionEngine::Visualization highThreshold;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, highThreshold);
    const auto thresholdDistance = visualizationDistance (
        lowThreshold, highThreshold);
    context.expect (
        thresholdDistance > 0.10 && thresholdDistance < 0.75,
        "Sign/Square Threshold is not useful and bounded (distance "
            + juce::String (thresholdDistance, 3) + ")");

    parameters.driveDb = 18.0f;
    parameters.stages = 2;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::transformerCore);
    parameters.secondary = dd::DistortionEngine::getDefaultSecondary (
        parameters.mode);
    parameters.character = 0.0f;
    dd::DistortionEngine::Visualization transformerLow;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, transformerLow);
    parameters.character = 1.0f;
    dd::DistortionEngine::Visualization transformerHigh;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, transformerHigh);
    const auto transformerDistance =
        visualizationDistance (transformerLow, transformerHigh);
    double transformerLowEnergy = 0.0;
    double transformerHighEnergy = 0.0;
    double transformerCrossEnergy = 0.0;
    for (size_t point = 0; point < transformerLow.output.size(); ++point)
    {
        const auto low = static_cast<double> (
            transformerLow.output[point]);
        const auto high = static_cast<double> (
            transformerHigh.output[point]);
        transformerLowEnergy += low * low;
        transformerHighEnergy += high * high;
        transformerCrossEnergy += low * high;
    }
    const auto transformerBestScale =
        transformerCrossEnergy
        / juce::jmax (1.0e-12, transformerLowEnergy);
    double transformerScaledResidual = 0.0;
    for (size_t point = 0; point < transformerLow.output.size(); ++point)
    {
        const auto error =
            static_cast<double> (transformerHigh.output[point])
            - transformerBestScale
                * static_cast<double> (transformerLow.output[point]);
        transformerScaledResidual += error * error;
    }
    const auto transformerResidualRatio =
        transformerScaledResidual
        / juce::jmax (1.0e-12, transformerHighEnergy);
    context.expect (
        transformerDistance > 0.12
            && transformerResidualRatio > 0.015,
        "Transformer Core still behaves like a volume control (distance "
            + juce::String (transformerDistance, 3)
            + ", residual "
            + juce::String (transformerResidualRatio, 3) + ")");

    parameters.driveDb = 20.0f;
    parameters.stages = 4;
    parameters.character = 0.8f;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::feedbackSaturator);
    dd::DistortionEngine::Visualization feedbackView;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, feedbackView);
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::resonantFeedbackClip);
    dd::DistortionEngine::Visualization resonantView;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, resonantView);
    context.expect (
        visualizationDistance (feedbackView, resonantView) > 0.08,
        "Resonant Feedback Clip is still too close to Feedback Saturator");

    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::phaseDistortion);
    parameters.driveDb = 0.0f;
    parameters.character = 0.5f;
    dd::DistortionEngine::Visualization zeroDepthPhase;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, zeroDepthPhase);
    context.expect (
        visualizationDistance (
            dd::DistortionEngine::Visualization {
                zeroDepthPhase.input,
                zeroDepthPhase.input,
                zeroDepthPhase.timeDomain,
                zeroDepthPhase.spectralDomain },
            zeroDepthPhase) < 1.0e-6,
        "Phase Distortion does not return a true 0 ms delay at Drive 0");

    parameters.driveDb = 30.0f;
    parameters.character = 0.8f;
    dd::DistortionEngine::Visualization phaseView;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, phaseView);
    auto clippedPoints = 0;
    for (const auto value : phaseView.output)
        if (std::abs (std::abs (value) - 1.0f) < 1.0e-4f)
            ++clippedPoints;
    context.expect (
        clippedPoints < 4,
        "Phase Distortion behaves like an amplitude hard clipper");
}

void testRequestedDevelopmentFixes (TestContext& context)
{
    dd::Parameters parameters;
    parameters.driveDb = 36.0f;
    parameters.character = 0.0f;
    parameters.stages = 1;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::morphSoftClip);
    dd::DistortionEngine::Visualization softClip;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, softClip);
    const auto drivenSoftPeak = *std::max_element (
        softClip.output.begin(), softClip.output.end());
    context.expect (
        std::abs (drivenSoftPeak - 1.25f) < 1.0e-5f,
        "Soft Clip visualization does not reach the graph ceiling");
    parameters.driveDb = 0.0f;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, softClip);
    auto upper = size_t { 1 };
    while (upper < softClip.input.size()
           && softClip.input[upper] < 1.0f)
        ++upper;
    upper = juce::jlimit (
        size_t { 1 }, softClip.input.size() - 1, upper);
    const auto lower = upper - 1;
    const auto span = softClip.input[upper] - softClip.input[lower];
    const auto fraction = (1.0f - softClip.input[lower])
        / juce::jmax (1.0e-6f, span);
    const auto outputAtZeroDb = softClip.output[lower]
        + fraction * (softClip.output[upper] - softClip.output[lower]);
    context.expect (
        outputAtZeroDb < 1.24f
            && std::abs (softClip.output.back() - 1.25f) < 1.0e-5f,
        "Soft Clip visualization bends or plateaus before the graph ceiling");

    parameters.driveDb = 18.0f;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::transistorFet);
    parameters.character = -1.0f;
    dd::DistortionEngine::Visualization closedGate;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, closedGate);
    parameters.character = 1.0f;
    dd::DistortionEngine::Visualization openGate;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, openGate);
    context.expect (
        visualizationDistance (closedGate, openGate) > 0.25,
        "Transistor/FET Gate still has too little range");

    parameters.driveDb = 24.0f;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::signSquare);
    parameters.character = -1.0f;
    dd::DistortionEngine::Visualization negativeThreshold;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, negativeThreshold);
    parameters.character = 1.0f;
    dd::DistortionEngine::Visualization positiveThreshold;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, positiveThreshold);
    const auto signThresholdDistance = visualizationDistance (
        negativeThreshold, positiveThreshold);
    context.expect (
        signThresholdDistance > 0.10 && signThresholdDistance < 0.75,
        "Sign/Square Threshold is not useful and bounded after decoupling");
    const auto signMinimum = *std::min_element (
        positiveThreshold.output.begin(), positiveThreshold.output.end());
    const auto signMaximum = *std::max_element (
        positiveThreshold.output.begin(), positiveThreshold.output.end());
    context.expect (
        signMaximum - signMinimum > 1.5f,
        "Sign/Square Threshold removes the signal at high Drive");

    parameters.driveDb = 18.0f;
    parameters.character = 1.0f;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::sineErosion);
    parameters.character = 0.5f;
    dd::DistortionEngine::Visualization sineErosion;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, sineErosion);
    context.expect (
        visualizationDistance (
            dd::DistortionEngine::Visualization {
                sineErosion.input,
                sineErosion.input,
                sineErosion.timeDomain,
                sineErosion.spectralDomain },
            sineErosion) > 0.05,
        "Sine Erosion Drive does not create phase modulation");

    {
        dd::Parameters deltaView;
        deltaView.mode = static_cast<int> (
            dd::DistortionEngine::Mode::deltaCrusher);
        deltaView.driveDb = 18.0f;
        deltaView.stages = 1;
        deltaView.character = 0.0f;
        dd::DistortionEngine::Visualization stepZero;
        dd::DistortionEngine::makeVisualization (
            deltaView, sampleRate, stepZero);
        deltaView.character = 0.25f;
        dd::DistortionEngine::Visualization stepQuarter;
        dd::DistortionEngine::makeVisualization (
            deltaView, sampleRate, stepQuarter);
        deltaView.character = 0.5f;
        dd::DistortionEngine::Visualization stepHalf;
        dd::DistortionEngine::makeVisualization (
            deltaView, sampleRate, stepHalf);
        deltaView.character = 1.0f;
        dd::DistortionEngine::Visualization stepFull;
        dd::DistortionEngine::makeVisualization (
            deltaView, sampleRate, stepFull);
        const auto zeroToQuarter =
            visualizationDistance (stepZero, stepQuarter);
        const auto quarterToHalf =
            visualizationDistance (stepQuarter, stepHalf);
        const auto halfToFull =
            visualizationDistance (stepHalf, stepFull);
        context.expect (
            zeroToQuarter > 0.002
                && quarterToHalf > 0.01
                && halfToFull > 0.03,
            "Delta Crusher Step still wastes the first half of its range ("
                + juce::String (zeroToQuarter, 4) + ", "
                + juce::String (quarterToHalf, 4) + ", "
                + juce::String (halfToFull, 4) + ")");
    }

    {
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, blockSize, 1);
        dd::Parameters delta;
        delta.mode = static_cast<int> (
            dd::DistortionEngine::Mode::deltaCrusher);
        delta.driveDb = 18.0f;
        delta.character = 1.0f;
        delta.stages = 1;
        delta.autoGainMode = 0;
        juce::AudioBuffer<float> buffer (1, blockSize);
        double phase = 0.0;
        double sum = 0.0;
        double absoluteSum = 0.0;
        int count = 0;
        for (int block = 0; block < 80; ++block)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = 0.25118864f * static_cast<float> (
                    std::sin (phase));
                phase += juce::MathConstants<double>::twoPi * 173.0
                    / sampleRate;
                buffer.setSample (0, sample, value);
            }
            engine.process (buffer, delta);
            if (block < 30)
                continue;
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = buffer.getSample (0, sample);
                sum += value;
                absoluteSum += std::abs (value);
                ++count;
            }
        }
        const auto mean = sum / juce::jmax (1, count);
        const auto meanAbsolute = absoluteSum / juce::jmax (1, count);
        context.expect (
            meanAbsolute > 1.0e-3
                && std::abs (mean)
                    < 0.08 * juce::jmax (1.0e-6, meanAbsolute),
            "Delta Crusher reconstruction has collapsed into DC");
    }

    context.expect (
        std::abs (
            dd::DistortionEngine::getDefaultCharacter (
                static_cast<int> (
                    dd::DistortionEngine::Mode::deltaCrusher))
            - 0.5f) < 1.0e-6f,
        "Delta Crusher Step does not default to 50%");

    {
        const std::array<std::pair<dd::DistortionEngine::Mode, float>, 7>
            dcModes {
                std::pair {
                    dd::DistortionEngine::Mode::hardClip, 0.0f },
                std::pair {
                    dd::DistortionEngine::Mode::fullWaveRectifier, 1.0f },
                std::pair {
                    dd::DistortionEngine::Mode::softFullWaveRectifier, 0.7f },
                std::pair {
                    dd::DistortionEngine::Mode::harmonicMorph, -1.0f },
                std::pair {
                    dd::DistortionEngine::Mode::transistorFet, 0.7f },
                std::pair {
                    dd::DistortionEngine::Mode::signSquare, 0.7f },
                std::pair {
                    dd::DistortionEngine::Mode::triodeStage, 0.7f }
            };
        for (const auto [mode, character] : dcModes)
        {
            dd::DistortionEngine engine;
            engine.prepare (sampleRate, blockSize, 1);
            dd::Parameters dc;
            dc.mode = static_cast<int> (mode);
            dc.driveDb = 18.0f;
            dc.character = character;
            dc.asymmetry =
                mode == dd::DistortionEngine::Mode::hardClip ? 0.7f : 0.0f;
            dc.autoGainMode = 0;
            juce::AudioBuffer<float> buffer (1, blockSize);
            float tailPeak = 0.0f;
            for (int block = 0; block < 120; ++block)
            {
                for (int sample = 0; sample < blockSize; ++sample)
                    buffer.setSample (0, sample, 0.35f);
                engine.process (buffer, dc);
                if (block < 80)
                    continue;
                for (int sample = 0; sample < blockSize; ++sample)
                    tailPeak = juce::jmax (
                        tailPeak,
                        std::abs (buffer.getSample (0, sample)));
            }
            context.expect (
                tailPeak < 1.0e-3f,
                "DC blocker is not consistently active in mode "
                    + juce::String (
                        static_cast<int> (mode) + 1));
        }
    }

    {
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, blockSize, 1);
        dd::Parameters hard;
        hard.mode = static_cast<int> (
            dd::DistortionEngine::Mode::hardClip);
        hard.driveDb = 0.0f;
        hard.character = 0.0f;
        hard.autoGainMode = 1;
        hard.mix = 1.0f;
        engine.primeAutoGain (hard);
        juce::AudioBuffer<float> buffer (1, blockSize);
        double phase = 0.0;
        double energy = 0.0;
        int count = 0;
        auto previousOutput = 0.0f;
        auto transitionJump = 0.0f;
        for (int block = 0; block < 100; ++block)
        {
            if (block == 12)
                hard.driveDb = 36.0f;
            for (int sample = 0; sample < blockSize; ++sample)
            {
                buffer.setSample (
                    0,
                    sample,
                    0.25118864f * static_cast<float> (std::sin (phase)));
                phase += juce::MathConstants<double>::twoPi * 173.0
                    / sampleRate;
            }
            engine.process (buffer, hard);
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = buffer.getSample (0, sample);
                if (block >= 12 && block < 30)
                    transitionJump = juce::jmax (
                        transitionJump,
                        std::abs (value - previousOutput));
                previousOutput = value;
            }
            if (block < 70)
                continue;
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = buffer.getSample (0, sample);
                energy += static_cast<double> (value) * value;
                ++count;
            }
        }
        const auto outputRms = std::sqrt (
            energy / juce::jmax (1, count));
        const auto inputRms = 0.25118864 / std::sqrt (2.0);
        const auto errorDb = std::abs (
            juce::Decibels::gainToDecibels (
                static_cast<float> (outputRms / inputRms),
                -100.0f));
        context.expect (
            errorDb < 0.5f,
            "Instant Auto Gain misses a -12 dBFS Hard Clip sine by "
                + juce::String (errorDb, 2) + " dB");
        context.expect (
            transitionJump < 0.12f,
            "Predictive Auto Gain clicks or spikes while Drive moves (jump "
                + juce::String (transitionJump, 4) + ")");
    }

    {
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, blockSize, 1);
        dd::Parameters triode;
        triode.mode = static_cast<int> (
            dd::DistortionEngine::Mode::triodeStage);
        triode.driveDb = 18.0f;
        triode.character = 0.0f;
        triode.autoGainMode = 0;
        juce::AudioBuffer<float> buffer (1, blockSize);
        auto previous = 0.0f;
        for (int block = 0; block < 40; ++block)
        {
            for (int sample = 0; sample < blockSize; ++sample)
                buffer.setSample (0, sample, 0.2f);
            engine.process (buffer, triode);
            previous = buffer.getSample (0, blockSize - 1);
        }

        triode.character = 0.04f;
        auto maximumJump = 0.0f;
        for (int block = 0; block < 12; ++block)
        {
            for (int sample = 0; sample < blockSize; ++sample)
                buffer.setSample (0, sample, 0.2f);
            engine.process (buffer, triode);
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto current = buffer.getSample (0, sample);
                maximumJump = juce::jmax (
                    maximumJump, std::abs (current - previous));
                previous = current;
            }
        }
        context.expect (
            maximumJump < 0.08f,
            "Triode DC-block crossfade still produces a click (jump "
                + juce::String (maximumJump, 5) + ")");
    }

    {
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, blockSize, 1);
        dd::Parameters phaseParameters;
        phaseParameters.mode = static_cast<int> (
            dd::DistortionEngine::Mode::phaseDistortion);
        phaseParameters.driveDb = 30.0f;
        phaseParameters.character = 0.5f;
        phaseParameters.stages = 8;
        phaseParameters.autoGainMode = 0;
        juce::AudioBuffer<float> buffer (1, blockSize);
        const auto activeSamples = juce::roundToInt (0.18 * sampleRate);
        const auto fadeSamples = juce::roundToInt (0.006 * sampleRate);
        const auto totalSamples = juce::roundToInt (0.75 * sampleRate);
        const auto latency = engine.getLatencySamples();
        auto tailPeak = 0.0f;
        int written = 0;
        while (written < totalSamples)
        {
            const auto samplesThisBlock = juce::jmin (
                blockSize, totalSamples - written);
            buffer.setSize (1, samplesThisBlock, false, false, true);
            for (int sample = 0; sample < samplesThisBlock; ++sample)
            {
                const auto position = written + sample;
                auto envelope = 1.0f;
                if (position >= activeSamples - fadeSamples)
                    envelope = position < activeSamples
                        ? static_cast<float> (activeSamples - position)
                            / static_cast<float> (fadeSamples)
                        : 0.0f;
                const auto input = 0.22f * envelope * static_cast<float> (
                    std::sin (
                        juce::MathConstants<double>::twoPi * 220.0
                        * static_cast<double> (position) / sampleRate));
                buffer.setSample (0, sample, input);
            }
            engine.process (buffer, phaseParameters);
            for (int sample = 0; sample < samplesThisBlock; ++sample)
            {
                const auto position = written + sample;
                if (position > activeSamples + latency
                        + juce::roundToInt (0.09 * sampleRate))
                    tailPeak = juce::jmax (
                        tailPeak, std::abs (buffer.getSample (0, sample)));
            }
            written += samplesThisBlock;
        }
        context.expect (
            tailPeak < 1.0e-4f,
            "Phase Distortion leaves a delayed click after release");
    }
}

std::vector<float> renderSecondaryVariant (dd::Parameters parameters)
{
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 1);
    juce::AudioBuffer<float> buffer (1, blockSize);
    std::vector<float> result;
    double phase = 0.0;
    for (int block = 0; block < 28; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto time = phase++ / sampleRate;
            buffer.setSample (0, sample, static_cast<float> (
                0.42 * std::sin (
                    juce::MathConstants<double>::twoPi * 173.0 * time)
                + 0.11 * std::sin (
                    juce::MathConstants<double>::twoPi * 1753.0 * time)));
        }
        engine.process (buffer, parameters);
        if (block >= 20)
            result.insert (
                result.end(),
                buffer.getReadPointer (0),
                buffer.getReadPointer (0) + blockSize);
    }
    return result;
}

double normalisedRenderDistance (const std::vector<float>& first,
                                 const std::vector<float>& second)
{
    double differenceEnergy = 0.0;
    double referenceEnergy = 0.0;
    for (size_t index = 0; index < juce::jmin (first.size(), second.size()); ++index)
    {
        const auto difference = static_cast<double> (first[index] - second[index]);
        differenceEnergy += difference * difference;
        referenceEnergy += static_cast<double> (first[index]) * first[index];
    }
    return std::sqrt (
        differenceEnergy / juce::jmax (1.0e-12, referenceEnergy));
}

void testSecondaryToneControlsAndSineRelease (TestContext& context)
{
    dd::Parameters parameters;
    parameters.driveDb = 24.0f;
    parameters.character = 0.72f;
    parameters.stages = 2;
    parameters.autoGainMode = 0;

    const auto expectSecondaryChange = [&] (
        dd::DistortionEngine::Mode mode,
        auto setMinimum,
        auto setMaximum,
        const juce::String& name)
    {
        parameters.mode = static_cast<int> (mode);
        auto minimum = parameters;
        auto maximum = parameters;
        setMinimum (minimum);
        setMaximum (maximum);
        const auto distance = normalisedRenderDistance (
            renderSecondaryVariant (minimum),
            renderSecondaryVariant (maximum));
        context.expect (
            std::isfinite (distance) && distance > 0.025,
            name + " secondary slider has too little audible effect ("
                + juce::String (distance, 4) + ")");
    };

    expectSecondaryChange (
        dd::DistortionEngine::Mode::tapeHysteresis,
        [] (dd::Parameters& p) { p.secondary = 0.5f; },
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Tape Bias");
    expectSecondaryChange (
        dd::DistortionEngine::Mode::transformerCore,
        [] (dd::Parameters& p) { p.secondary = 0.0f; },
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Transformer Air Gap");
    expectSecondaryChange (
        dd::DistortionEngine::Mode::downsample,
        [] (dd::Parameters& p) { p.secondary = 0.0f; },
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Downsample Jitter");
    expectSecondaryChange (
        dd::DistortionEngine::Mode::bitCrusher,
        [] (dd::Parameters& p) { p.secondary = 0.0f; },
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Bit Crusher Dither");
    expectSecondaryChange (
        dd::DistortionEngine::Mode::schmittHysteresis,
        [] (dd::Parameters& p) { p.secondary = 0.0f; },
        [] (dd::Parameters& p) { p.secondary = 1.0f; },
        "Schmitt Slew");

    dd::Parameters schmittViewParameters;
    schmittViewParameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::schmittHysteresis);
    schmittViewParameters.driveDb = 24.0f;
    schmittViewParameters.character = 0.65f;
    dd::DistortionEngine::Visualization schmittWithoutSlew;
    dd::DistortionEngine::makeVisualization (
        schmittViewParameters, sampleRate, schmittWithoutSlew);
    schmittViewParameters.secondary = 1.0f;
    dd::DistortionEngine::Visualization schmittWithSlew;
    dd::DistortionEngine::makeVisualization (
        schmittViewParameters, sampleRate, schmittWithSlew);
    context.expect (
        ! schmittWithoutSlew.timeDomain
            && ! schmittWithSlew.timeDomain
            && ! schmittWithoutSlew.spectralDomain
            && ! schmittWithSlew.spectralDomain,
        "Schmitt Hysteresis visualization changes domain when Slew moves");

    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 1);
    dd::Parameters sine;
    sine.mode = static_cast<int> (dd::DistortionEngine::Mode::sineErosion);
    sine.driveDb = 36.0f;
    sine.character = 0.35f;
    sine.stages = 4;
    sine.autoGainMode = 0;
    juce::AudioBuffer<float> buffer (1, blockSize);
    double phase = 0.0;
    float previous = 0.0f;
    auto steadyMaximumJump = 0.0f;
    for (int block = 0; block < 20; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = 0.25118864f * static_cast<float> (
                std::sin (
                    juce::MathConstants<double>::twoPi * 55.0
                    * phase++ / sampleRate));
            buffer.setSample (0, sample, value);
        }
        engine.process (buffer, sine);
        if (block >= 16)
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = buffer.getSample (0, sample);
                steadyMaximumJump = juce::jmax (
                    steadyMaximumJump, std::abs (value - previous));
                previous = value;
            }
        previous = buffer.getSample (0, blockSize - 1);
    }

    sine.driveDb = 0.0f;
    auto maximumJump = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
            buffer.setSample (0, sample, 0.25118864f * static_cast<float> (
                std::sin (
                    juce::MathConstants<double>::twoPi * 55.0
                    * phase++ / sampleRate)));
        engine.process (buffer, sine);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = buffer.getSample (0, sample);
            maximumJump = juce::jmax (maximumJump, std::abs (value - previous));
            previous = value;
        }
    }
    context.expect (
        maximumJump <= steadyMaximumJump * 1.15f + 0.01f,
        "Sine Erosion Drive release clicks (maximum adjacent jump "
            + juce::String (maximumJump, 4)
            + ", steady-state maximum "
            + juce::String (steadyMaximumJump, 4) + ")");
}

void testSharedSecondaryContract (TestContext& context)
{
    const std::array<std::pair<dd::DistortionEngine::Mode, juce::String>, 6>
        controls {
            std::pair { dd::DistortionEngine::Mode::sineErosion, "NOISE" },
            std::pair { dd::DistortionEngine::Mode::tapeHysteresis, "BIAS" },
            std::pair { dd::DistortionEngine::Mode::transformerCore, "AIR GAP" },
            std::pair { dd::DistortionEngine::Mode::downsample, "JITTER" },
            std::pair { dd::DistortionEngine::Mode::bitCrusher, "DITHER" },
            std::pair { dd::DistortionEngine::Mode::schmittHysteresis, "SLEW" }
        };

    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
    {
        const auto match = std::find_if (
            controls.begin(), controls.end(),
            [mode] (const auto& control)
            {
                return static_cast<int> (control.first) == mode;
            });
        const auto expected = match != controls.end();
        context.expect (
            dd::DistortionEngine::hasSecondaryControl (mode) == expected,
            "Shared Secondary visibility is wrong for mode "
                + juce::String (mode + 1));
        context.expect (
            dd::DistortionEngine::getSecondaryName (mode)
                == (expected ? match->second : juce::String {}),
            "Shared Secondary label is wrong for mode "
                + juce::String (mode + 1));
    }

    context.expect (
        std::abs (dd::DistortionEngine::getDefaultSecondary (
            static_cast<int> (
                dd::DistortionEngine::Mode::tapeHysteresis)) - 0.5f)
            < 1.0e-6f,
        "Tape Bias does not reset Shared Secondary to 50%");
    for (const auto& control : controls)
        if (control.first != dd::DistortionEngine::Mode::tapeHysteresis)
            context.expect (
                std::abs (dd::DistortionEngine::getDefaultSecondary (
                    static_cast<int> (control.first))) < 1.0e-6f,
                control.second + " does not reset Shared Secondary to 0%");
}

void testVersionNineNeutralDynamicAndToneFilters (TestContext& context)
{
    constexpr int testSamples = 256;
    const auto slow = dd::DistortionEngine::dynamicsTimingForSpeed (0.0f);
    const auto medium = dd::DistortionEngine::dynamicsTimingForSpeed (50.0f);
    const auto fast = dd::DistortionEngine::dynamicsTimingForSpeed (100.0f);
    context.expect (
        std::abs (slow.first - 100.0f) < 1.0e-6f
            && std::abs (slow.second - 1000.0f) < 1.0e-6f
            && std::abs (medium.first - 10.0f) < 1.0e-6f
            && std::abs (medium.second - 100.0f) < 1.0e-6f
            && std::abs (fast.first - 0.1f) < 1.0e-6f
            && std::abs (fast.second - 15.0f) < 1.0e-6f,
        "SPEED does not reproduce the approved default_eq timing anchors");
    dd::Parameters neutral;
    context.expect (neutral.route == 0, "0.9 Route default is not M/S");
    context.expect (
        neutral.placementPercent == 0.0f,
        "0.9 Placement default is not neutral");
    context.expect (
        neutral.dynamicPercent == 0.0f,
        "0.9 Dynamic default is not neutral");
    context.expect (
        neutral.inputHpHz == 0.0f,
        "0.9 Input HP default is not OFF");
    context.expect (
        neutral.outputLpHz == 20000.0f,
        "0.9 Output LP default is not OFF");

    dd::DistortionEngine reference;
    dd::DistortionEngine withDetector;
    reference.prepare (sampleRate, testSamples, 2);
    withDetector.prepare (sampleRate, testSamples, 2);
    juce::AudioBuffer<float> first (2, testSamples);
    juce::AudioBuffer<float> second (2, testSamples);
    juce::AudioBuffer<float> detector (2, testSamples);
    double phase = 0.0;
    for (int block = 0; block < 12; ++block)
    {
        fillSignal (first, phase);
        second.makeCopyOf (first);
        detector.makeCopyOf (first);
        reference.process (first, neutral);
        withDetector.process (second, neutral, &detector);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < testSamples; ++sample)
                context.expect (
                    std::bit_cast<std::uint32_t> (
                        first.getSample (channel, sample))
                        == std::bit_cast<std::uint32_t> (
                            second.getSample (channel, sample)),
                    "DYNAMIC=0 changes the neutral 0.8 signal path");
    }

    const auto measure = [] (dd::Parameters parameters,
                             double frequency,
                             float amplitude,
                             const juce::AudioBuffer<float>* externalDetector)
    {
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, testSamples, 2);
        parameters.autoGainMode = 0;
        juce::AudioBuffer<float> audio (2, testSamples);
        juce::AudioBuffer<float> localDetector (2, testSamples);
        double oscillator = 0.0;
        double energy = 0.0;
        int count = 0;
        for (int block = 0; block < 160; ++block)
        {
            for (int sample = 0; sample < testSamples; ++sample)
            {
                const auto value = amplitude * static_cast<float> (
                    std::sin (oscillator));
                oscillator += juce::MathConstants<double>::twoPi
                    * frequency / sampleRate;
                for (int channel = 0; channel < 2; ++channel)
                {
                    audio.setSample (channel, sample, value);
                    localDetector.setSample (channel, sample, value);
                }
            }
            engine.process (
                audio,
                parameters,
                externalDetector != nullptr ? externalDetector : &localDetector);
            if (block >= 120)
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < testSamples; ++sample)
                    {
                        const auto value = audio.getSample (channel, sample);
                        energy += static_cast<double> (value) * value;
                        ++count;
                    }
        }
        return std::sqrt (energy / juce::jmax (1, count));
    };

    dd::Parameters dynamic;
    dynamic.mode = static_cast<int> (
        dd::DistortionEngine::Mode::morphSoftClip);
    dynamic.driveDb = 18.0f;
    dynamic.mix = 1.0f;
    dynamic.dynamicPercent = 100.0f;
    const auto positive = measure (dynamic, 997.0, 0.7f, nullptr);
    dynamic.dynamicPercent = -100.0f;
    const auto negative = measure (dynamic, 997.0, 0.7f, nullptr);
    context.expect (
        positive > negative * 1.05,
        "Positive Dynamic does not drive loud input harder than negative Dynamic");

    dd::Parameters filter;
    filter.mix = 0.0f;
    filter.inputHpHz = 200.0f;
    const auto hpLow = measure (filter, 20.0, 0.2f, nullptr);
    const auto hpCutoff = measure (filter, 200.0, 0.2f, nullptr);
    const auto hpHigh = measure (filter, 2000.0, 0.2f, nullptr);
    context.expect (
        hpLow < hpHigh * 0.08,
        "18 dB/oct Input HP does not reject low frequencies");
    context.expect (
        hpCutoff / hpHigh > 0.62 && hpCutoff / hpHigh < 0.80,
        "Input HP is not approximately -3 dB at its Butterworth cutoff");

    filter.inputHpHz = 0.0f;
    filter.outputLpHz = 2000.0f;
    const auto lpLow = measure (filter, 500.0, 0.2f, nullptr);
    const auto lpCutoff = measure (filter, 2000.0, 0.2f, nullptr);
    const auto lpHigh = measure (filter, 10000.0, 0.2f, nullptr);
    context.expect (
        lpHigh < lpLow * 0.08,
        "18 dB/oct Output LP does not reject high frequencies");
    context.expect (
        lpCutoff / lpLow > 0.62 && lpCutoff / lpLow < 0.80,
        "Output LP is not approximately -3 dB at its Butterworth cutoff");

    dd::Parameters returning = dynamic;
    returning.speedPercent = 100.0f;
    dd::Parameters neutralDynamic = returning;
    neutralDynamic.dynamicPercent = 0.0f;
    dd::DistortionEngine returningEngine;
    dd::DistortionEngine neutralEngine;
    returningEngine.prepare (sampleRate, testSamples, 2);
    neutralEngine.prepare (sampleRate, testSamples, 2);
    juce::AudioBuffer<float> returningAudio (2, testSamples);
    juce::AudioBuffer<float> neutralAudio (2, testSamples);
    juce::AudioBuffer<float> loudDetector (2, testSamples);
    juce::AudioBuffer<float> silentDetector (2, testSamples);
    loudDetector.clear();
    silentDetector.clear();
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill (
            loudDetector.getWritePointer (channel), 1.0f, testSamples);
    phase = 0.0;
    auto returnError = 0.0f;
    for (int block = 0; block < 180; ++block)
    {
        fillSignal (returningAudio, phase);
        neutralAudio.makeCopyOf (returningAudio, true);
        returningEngine.process (
            returningAudio,
            returning,
            block < 20 ? &loudDetector : &silentDetector);
        neutralEngine.process (
            neutralAudio, neutralDynamic, &silentDetector);
        if (block >= 170)
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < testSamples; ++sample)
                    returnError = juce::jmax (
                        returnError,
                        std::abs (returningAudio.getSample (channel, sample)
                                  - neutralAudio.getSample (channel, sample)));
    }
    context.expect (
        returnError < 2.0e-4f,
        "Dynamic Drive did not return to base Drive after detector silence");

    const auto renderDynamic = [] (int renderBlockSize)
    {
        constexpr int totalSamples = 17 * 127 * 12;
        dd::DistortionEngine engine;
        engine.prepare (sampleRate, renderBlockSize, 2);
        dd::Parameters parameters;
        parameters.mode = static_cast<int> (
            dd::DistortionEngine::Mode::morphSoftClip);
        parameters.driveDb = 12.0f;
        parameters.dynamicPercent = 100.0f;
        parameters.speedPercent = 50.0f;
        parameters.autoGainMode = 0;
        juce::AudioBuffer<float> audio (2, renderBlockSize);
        juce::AudioBuffer<float> dynamicDetector (2, renderBlockSize);
        std::vector<float> rendered (totalSamples, 0.0f);
        for (int offset = 0; offset < totalSamples; offset += renderBlockSize)
        {
            for (int sample = 0; sample < renderBlockSize; ++sample)
            {
                const auto absolute = offset + sample;
                const auto carrier = static_cast<float> (std::sin (
                    juce::MathConstants<double>::twoPi * 997.0
                    * static_cast<double> (absolute) / sampleRate));
                const auto modulator = 0.15f + 0.75f * static_cast<float> (
                    0.5 + 0.5 * std::sin (
                        juce::MathConstants<double>::twoPi * 2.3
                        * static_cast<double> (absolute) / sampleRate));
                const auto value = carrier * modulator;
                audio.setSample (0, sample, value);
                audio.setSample (1, sample, -0.73f * value);
                dynamicDetector.setSample (0, sample, value);
                dynamicDetector.setSample (1, sample, -0.73f * value);
            }
            engine.process (audio, parameters, &dynamicDetector);
            for (int sample = 0; sample < renderBlockSize; ++sample)
                rendered[static_cast<size_t> (offset + sample)] =
                    audio.getSample (0, sample);
        }
        return rendered;
    };
    const auto dynamic17 = renderDynamic (17);
    const auto dynamic127 = renderDynamic (127);
    auto blockSizeError = 0.0f;
    for (size_t sample = 4096; sample < dynamic17.size(); ++sample)
        blockSizeError = juce::jmax (
            blockSizeError,
            std::abs (dynamic17[sample] - dynamic127[sample]));
    context.expect (
        blockSizeError < 2.0e-4f,
        "Dynamic Drive is block-size dependent after control settling");

    dd::DistortionEngine smartDynamicEngine;
    auto smartDynamic = dynamic;
    smartDynamic.autoGainMode = 2;
    smartDynamicEngine.prepare (sampleRate, testSamples, 2);
    smartDynamicEngine.primeAutoGain (smartDynamic);
    phase = 0.0;
    for (int block = 0; block < 200; ++block)
    {
        fillSignal (first, phase);
        detector.makeCopyOf (first, true);
        smartDynamicEngine.process (first, smartDynamic, &detector);
    }
    context.expect (
        smartDynamicEngine.isSmartAutoGainLocked()
            && smartDynamicEngine.getSmartAutoGainProgress() >= 0.999f,
        "Smart Auto Gain did not retain its finite measurement lifecycle "
        "with Dynamic enabled");
}

void testVersionNinePlacementRouting (TestContext& context)
{
    constexpr int routeBlock = 256;
    dd::TransientSplitter splitter;
    splitter.prepare (sampleRate, routeBlock, 2);
    splitter.setParameters (100.0f, 0.0f, 100.0f, 50.0f);
    juce::AudioBuffer<float> input (2, routeBlock);
    juce::AudioBuffer<float> transient (2, routeBlock);
    juce::AudioBuffer<float> sustain (2, routeBlock);
    std::vector<float> history;
    history.reserve (static_cast<size_t> (routeBlock * 48));
    double phase = 0.0;
    auto maximumError = 0.0f;
    for (int block = 0; block < 48; ++block)
    {
        for (int sample = 0; sample < routeBlock; ++sample)
        {
            const auto value = 0.35f * static_cast<float> (std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * 733.0 / sampleRate;
            input.setSample (0, sample, value);
            input.setSample (1, sample, value * 0.71f);
            history.push_back (value);
        }
        splitter.process (input, transient, sustain, routeBlock);
        for (int sample = 0; sample < routeBlock; ++sample)
        {
            const auto absolute = block * routeBlock + sample;
            const auto delayed = absolute >= splitter.latency()
                ? history[static_cast<size_t> (absolute - splitter.latency())]
                : 0.0f;
            maximumError = juce::jmax (
                maximumError,
                std::abs (transient.getSample (0, sample)
                          + sustain.getSample (0, sample) - delayed));
        }
    }
    context.expect (
        maximumError < 2.0e-6f,
        "T/S splitter outputs are not complementary");

    dd::Parameters routed;
    routed.mode = static_cast<int> (dd::DistortionEngine::Mode::hardClip);
    routed.driveDb = 30.0f;
    routed.autoGainMode = 0;
    routed.mix = 1.0f;
    routed.route = 1;
    routed.placementPercent = 0.0f;
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, routeBlock, 2);
    const auto neutralLatency = engine.getLatencySamples();
    juce::AudioBuffer<float> audio (2, routeBlock);
    audio.clear();
    engine.process (audio, routed);
    context.expect (
        engine.getLatencySamples() == neutralLatency,
        "T/S Route at Placement 0 adds latency or FFT work");

    routed.placementPercent = -100.0f;
    engine.process (audio, routed);
    context.expect (
        engine.getLatencySamples() > neutralLatency,
        "Active T/S routing does not report its FFT latency");

    dd::Parameters midOnly = routed;
    midOnly.route = 0;
    midOnly.placementPercent = -100.0f;
    dd::Parameters sideOnly = midOnly;
    sideOnly.placementPercent = 100.0f;
    dd::DistortionEngine midEngine;
    dd::DistortionEngine sideEngine;
    midEngine.prepare (sampleRate, routeBlock, 2);
    sideEngine.prepare (sampleRate, routeBlock, 2);
    double midEnergy = 0.0;
    double sideEnergy = 0.0;
    phase = 0.0;
    for (int block = 0; block < 64; ++block)
    {
        for (int sample = 0; sample < routeBlock; ++sample)
        {
            const auto value = 0.31f * static_cast<float> (std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * 997.0 / sampleRate;
            audio.setSample (0, sample, value);
            audio.setSample (1, sample, value);
        }
        auto sideAudio = audio;
        midEngine.process (audio, midOnly);
        sideEngine.process (sideAudio, sideOnly);
        if (block >= 48)
            for (int sample = 0; sample < routeBlock; ++sample)
            {
                midEnergy += std::pow (
                    static_cast<double> (audio.getSample (0, sample)), 2.0);
                sideEnergy += std::pow (
                    static_cast<double> (sideAudio.getSample (0, sample)), 2.0);
            }
    }
    context.expect (
        midEnergy > sideEnergy * 1.1,
        "M/S endpoints do not isolate Mid processing on a dual-mono signal");

    dd::Parameters intermediate = midOnly;
    intermediate.placementPercent = 50.0f;
    dd::DistortionEngine intermediateEngine;
    intermediateEngine.prepare (sampleRate, routeBlock, 2);
    auto intermediateEnergy = 0.0;
    phase = 0.0;
    for (int block = 0; block < 64; ++block)
    {
        for (int sample = 0; sample < routeBlock; ++sample)
        {
            const auto value = 0.31f * static_cast<float> (std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * 997.0 / sampleRate;
            audio.setSample (0, sample, value);
            audio.setSample (1, sample, value);
        }
        intermediateEngine.process (audio, intermediate);
        if (block >= 48)
            for (int sample = 0; sample < routeBlock; ++sample)
                intermediateEnergy += std::pow (
                    static_cast<double> (audio.getSample (0, sample)), 2.0);
    }
    context.expect (
        intermediateEnergy > juce::jmin (midEnergy, sideEnergy)
            && intermediateEnergy < juce::jmax (midEnergy, sideEnergy),
        "M/S intermediate Placement does not interpolate between endpoints");

    dd::DistortionEngine antiMidEngine;
    dd::DistortionEngine antiSideEngine;
    antiMidEngine.prepare (sampleRate, routeBlock, 2);
    antiSideEngine.prepare (sampleRate, routeBlock, 2);
    auto antiMidEnergy = 0.0;
    auto antiSideEnergy = 0.0;
    phase = 0.0;
    for (int block = 0; block < 64; ++block)
    {
        for (int sample = 0; sample < routeBlock; ++sample)
        {
            const auto value = 0.31f * static_cast<float> (std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * 997.0 / sampleRate;
            audio.setSample (0, sample, value);
            audio.setSample (1, sample, -value);
        }
        auto sideAudio = audio;
        antiMidEngine.process (audio, midOnly);
        antiSideEngine.process (sideAudio, sideOnly);
        if (block >= 48)
            for (int sample = 0; sample < routeBlock; ++sample)
            {
                antiMidEnergy += std::pow (
                    static_cast<double> (audio.getSample (0, sample)), 2.0);
                antiSideEnergy += std::pow (
                    static_cast<double> (sideAudio.getSample (0, sample)), 2.0);
            }
    }
    context.expect (
        antiSideEnergy > antiMidEnergy * 1.1,
        "M/S endpoints do not isolate Side processing on anti-phase input");

    dd::DistortionEngine monoSideEngine;
    dd::DistortionEngine monoDryEngine;
    monoSideEngine.prepare (sampleRate, routeBlock, 1);
    monoDryEngine.prepare (sampleRate, routeBlock, 1);
    auto monoDry = sideOnly;
    monoDry.placementPercent = 0.0f;
    monoDry.mix = 0.0f;
    juce::AudioBuffer<float> monoSideAudio (1, routeBlock);
    juce::AudioBuffer<float> monoDryAudio (1, routeBlock);
    phase = 0.0;
    auto monoError = 0.0f;
    for (int block = 0; block < 48; ++block)
    {
        for (int sample = 0; sample < routeBlock; ++sample)
        {
            const auto value = 0.27f * static_cast<float> (std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * 701.0 / sampleRate;
            monoSideAudio.setSample (0, sample, value);
        }
        monoDryAudio.makeCopyOf (monoSideAudio, true);
        monoSideEngine.process (monoSideAudio, sideOnly);
        monoDryEngine.process (monoDryAudio, monoDry);
        if (block >= 40)
            for (int sample = 0; sample < routeBlock; ++sample)
                monoError = juce::jmax (
                    monoError,
                    std::abs (monoSideAudio.getSample (0, sample)
                              - monoDryAudio.getSample (0, sample)));
    }
    context.expect (
        monoError < 2.0e-6f,
        "M/S Side-only placement changed a mono signal");

    for (const auto splitterRate : { 44100.0, 192000.0 })
    {
        constexpr int splitterBlock = 127;
        dd::TransientSplitter rateSplitter;
        rateSplitter.prepare (splitterRate, splitterBlock, 2);
        rateSplitter.setParameters (100.0f, 0.0f, 100.0f, 50.0f);
        juce::AudioBuffer<float> rateInput (2, splitterBlock);
        juce::AudioBuffer<float> rateTransient (2, splitterBlock);
        juce::AudioBuffer<float> rateSustain (2, splitterBlock);
        std::array<std::vector<float>, 2> rateHistory;
        for (auto& channel : rateHistory)
            channel.reserve (splitterBlock * 96);
        auto rateError = 0.0f;
        phase = 0.0;
        for (int block = 0; block < 96; ++block)
        {
            for (int sample = 0; sample < splitterBlock; ++sample)
            {
                const auto absolute = block * splitterBlock + sample;
                const auto impulse = absolute % 997 == 0 ? 0.7f : 0.0f;
                const auto tone = 0.13f * static_cast<float> (std::sin (phase));
                phase += juce::MathConstants<double>::twoPi
                    * 733.0 / splitterRate;
                rateInput.setSample (0, sample, impulse + tone);
                rateInput.setSample (1, sample, impulse - 0.61f * tone);
                rateHistory[0].push_back (rateInput.getSample (0, sample));
                rateHistory[1].push_back (rateInput.getSample (1, sample));
            }
            rateSplitter.process (
                rateInput, rateTransient, rateSustain, splitterBlock);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < splitterBlock; ++sample)
                {
                    const auto absolute = block * splitterBlock + sample;
                    const auto delayed = absolute >= rateSplitter.latency()
                        ? rateHistory[static_cast<size_t> (channel)]
                            [static_cast<size_t> (
                                absolute - rateSplitter.latency())]
                        : 0.0f;
                    rateError = juce::jmax (
                        rateError,
                        std::abs (
                            rateTransient.getSample (channel, sample)
                            + rateSustain.getSample (channel, sample)
                            - delayed));
                }
        }
        context.expect (
            rateError < 2.0e-6f,
            "T/S complementarity changed at "
                + juce::String (splitterRate) + " Hz");

        rateSplitter.reset();
        rateInput.clear();
        auto resetMagnitude = 0.0f;
        for (int block = 0; block < 8; ++block)
        {
            rateSplitter.process (
                rateInput, rateTransient, rateSustain, splitterBlock);
            resetMagnitude = juce::jmax (
                resetMagnitude,
                rateTransient.getMagnitude (0, splitterBlock));
            resetMagnitude = juce::jmax (
                resetMagnitude,
                rateSustain.getMagnitude (0, splitterBlock));
        }
        context.expect (
            resetMagnitude < 1.0e-8f,
            "T/S reset retained audio at "
                + juce::String (splitterRate) + " Hz");
    }
}

void testVersionNineAutomationMatrix (TestContext& context)
{
    constexpr std::array<double, 4> rates {
        44100.0, 48000.0, 96000.0, 192000.0
    };
    constexpr std::array<int, 3> blockSizes { 17, 127, 512 };
    for (const auto rate : rates)
        for (const auto size : blockSizes)
        {
            dd::DistortionEngine engine;
            engine.prepare (rate, size, 2);
            dd::Parameters parameters;
            parameters.mode = static_cast<int> (
                dd::DistortionEngine::Mode::hardClip);
            parameters.driveDb = 18.0f;
            parameters.autoGainMode = 0;
            juce::AudioBuffer<float> audio (2, size);
            double phase = 0.0;
            auto previous = 0.0f;
            auto maximumStep = 0.0f;
            for (int block = 0; block < 36; ++block)
            {
                parameters.dynamicPercent = std::array<float, 3> {
                    -100.0f, 0.0f, 100.0f
                }[static_cast<size_t> (block % 3)];
                parameters.speedPercent = std::array<float, 3> {
                    0.0f, 50.0f, 100.0f
                }[static_cast<size_t> ((block / 3) % 3)];
                parameters.inputHpHz = std::array<float, 3> {
                    0.0f, 1.0f, 200.0f
                }[static_cast<size_t> ((block / 2) % 3)];
                parameters.outputLpHz = std::array<float, 3> {
                    20000.0f, 19999.0f, 2000.0f
                }[static_cast<size_t> ((block / 4) % 3)];
                for (int sample = 0; sample < size; ++sample)
                {
                    const auto value = 0.22f * static_cast<float> (
                        std::sin (phase)) + 0.03f;
                    phase += juce::MathConstants<double>::twoPi * 997.0 / rate;
                    audio.setSample (0, sample, value);
                    audio.setSample (1, sample, value * 0.79f);
                }
                engine.process (audio, parameters);
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < size; ++sample)
                    {
                        const auto value = audio.getSample (channel, sample);
                        context.expect (
                            std::isfinite (value),
                            "0.9 automation matrix produced NaN/Inf");
                        if (channel == 0)
                        {
                            maximumStep = juce::jmax (
                                maximumStep, std::abs (value - previous));
                            previous = value;
                        }
                    }
            }
            context.expect (
                maximumStep < 0.35f,
                "HP/LP/Dynamic automation produced an unbounded step at "
                    + juce::String (rate) + " Hz / " + juce::String (size)
                    + ": " + juce::String (maximumStep));

            for (int block = 0; block < 36; ++block)
            {
                parameters.dynamicPercent = -100.0f
                    + 200.0f * static_cast<float> (block) / 35.0f;
                parameters.speedPercent = 37.0f;
                parameters.inputHpHz = 83.0f;
                parameters.outputLpHz = 7310.0f;
                for (int sample = 0; sample < size; ++sample)
                {
                    const auto value = 0.22f * static_cast<float> (
                        std::sin (phase));
                    phase += juce::MathConstants<double>::twoPi * 997.0 / rate;
                    audio.setSample (0, sample, value);
                    audio.setSample (1, sample, -0.79f * value);
                }
                engine.process (audio, parameters);
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < size; ++sample)
                        context.expect (
                            std::isfinite (audio.getSample (channel, sample)),
                            "Dynamic ramp automation produced NaN/Inf at "
                                + juce::String (rate) + " Hz / "
                                + juce::String (size));
            }
        }

    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
        for (int stages = 1; stages <= dd::DistortionEngine::maximumStages; ++stages)
            for (const auto speed : { 0.0f, 50.0f, 100.0f })
            {
                dd::DistortionEngine engine;
                engine.prepare (48000.0, 64, 2);
                dd::Parameters parameters;
                parameters.mode = mode;
                parameters.driveDb = (mode + stages) % 2 == 0 ? 0.0f : 36.0f;
                parameters.character = dd::DistortionEngine::isCharacterBipolar (
                    mode) ? -0.37f : 0.63f;
                parameters.stages = stages;
                parameters.speedPercent = speed;
                parameters.autoGainMode = 0;
                juce::AudioBuffer<float> audio (2, 64);
                double phase = 0.0;
                for (const auto dynamic : { -100.0f, 0.0f, 100.0f })
                {
                    parameters.dynamicPercent = dynamic;
                    for (int block = 0; block < 3; ++block)
                    {
                        for (int sample = 0; sample < 64; ++sample)
                        {
                            const auto value = 0.71f * static_cast<float> (
                                std::sin (phase));
                            phase += juce::MathConstants<double>::twoPi
                                * 1301.0 / 48000.0;
                            audio.setSample (0, sample, value);
                            audio.setSample (1, sample, -0.73f * value);
                        }
                        engine.process (audio, parameters);
                        for (int channel = 0; channel < 2; ++channel)
                            for (int sample = 0; sample < 64; ++sample)
                                context.expect (
                                    std::isfinite (audio.getSample (channel, sample)),
                                    "Dynamic matrix failed for mode/stage/speed "
                                        + juce::String (mode + 1) + "/"
                                        + juce::String (stages) + "/"
                                        + juce::String (speed));
                    }
                }
            }

    for (const auto linked : { false, true })
        for (const auto linearPhase : { false, true })
            for (const auto bands : { 2, 4 })
            {
                dd::MultibandProcessor processor;
                processor.prepare (48000.0, 128, 2);
                dd::Parameters master;
                master.route = 1;
                master.placementPercent = -100.0f;
                master.driveDb = 18.0f;
                master.dynamicPercent = 100.0f;
                master.quality = 3;
                master.autoGainMode = 0;
                dd::MultibandParameters multiband;
                multiband.enabled = true;
                multiband.linked = linked;
                multiband.bandCount = bands;
                multiband.phaseMode = linearPhase ? 1 : 0;
                for (auto& band : multiband.bands)
                {
                    band.saturation = master;
                    band.saturation.placementPercent = 100.0f;
                }
                juce::AudioBuffer<float> audio (2, 128);
                juce::AudioBuffer<float> detector (2, 128);
                double phase = 0.0;
                for (int block = 0; block < 12; ++block)
                {
                    fillSignal (audio, phase);
                    detector.makeCopyOf (audio);
                    processor.process (
                        audio, master, multiband, -1, &detector);
                    for (int channel = 0; channel < 2; ++channel)
                        for (int sample = 0; sample < 128; ++sample)
                            context.expect (
                                std::isfinite (audio.getSample (channel, sample)),
                                "Combined T/S multiband path produced NaN/Inf");
                }
                context.expect (
                    processor.getLatencySamples (linearPhase)
                        == processor.getMaximumLatencySamples (linearPhase),
                    "Combined T/S multiband latency is not fully reported");
            }
}

void testVersionNineAudioThreadAllocationBoundary (TestContext& context)
{
    constexpr int samples = 512;
    dd::Parameters parameters;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::spectralClip);
    parameters.driveDb = 24.0f;
    parameters.character = 0.61f;
    parameters.stages = 8;
    parameters.quality = 3;
    parameters.autoGainMode = 2;
    parameters.route = 1;
    parameters.placementPercent = -73.0f;
    parameters.dynamicPercent = 100.0f;
    parameters.speedPercent = 100.0f;
    parameters.inputHpHz = 200.0f;
    parameters.outputLpHz = 2000.0f;

    dd::DistortionEngine engine;
    engine.prepare (48000.0, samples, 2);
    juce::AudioBuffer<float> audio (2, samples);
    juce::AudioBuffer<float> detector (2, samples);
    double phase = 0.0;
    fillSignal (audio, phase);
    detector.makeCopyOf (audio);
    engine.process (audio, parameters, &detector);

    allocation_probe::count = 0;
    allocation_probe::enabled = true;
    for (int block = 0; block < 8; ++block)
    {
        fillSignal (audio, phase);
        detector.makeCopyOf (audio, true);
        engine.process (audio, parameters, &detector);
    }
    allocation_probe::enabled = false;
    const auto engineAllocations = allocation_probe::count;

    dd::MultibandProcessor multibandProcessor;
    multibandProcessor.prepare (48000.0, samples, 2);
    dd::MultibandParameters multiband;
    multiband.enabled = true;
    multiband.linked = false;
    multiband.bandCount = 4;
    multiband.phaseMode = 1;
    for (auto& band : multiband.bands)
        band.saturation = parameters;
    fillSignal (audio, phase);
    detector.makeCopyOf (audio);
    multibandProcessor.process (
        audio, parameters, multiband, -1, &detector);

    allocation_probe::count = 0;
    allocation_probe::enabled = true;
    for (int block = 0; block < 4; ++block)
    {
        fillSignal (audio, phase);
        detector.makeCopyOf (audio, true);
        multibandProcessor.process (
            audio, parameters, multiband, -1, &detector);
    }
    allocation_probe::enabled = false;
    const auto multibandAllocations = allocation_probe::count;

    dd::SpectrumFIFO analyzer;
    juce::AudioBuffer<float> analyzerAudio (2, samples);
    double analyzerPhase = 0.0;
    allocation_probe::count = 0;
    allocation_probe::enabled = true;
    for (int block = 0; block < 20; ++block)
    {
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto value = 0.25f * static_cast<float> (
                std::sin (analyzerPhase));
            analyzerPhase += juce::MathConstants<double>::twoPi
                * 1000.0 / 48000.0;
            analyzerAudio.setSample (0, sample, value);
            analyzerAudio.setSample (1, sample, value);
        }
        analyzer.pushBlock (analyzerAudio);
    }
    allocation_probe::enabled = false;
    const auto analyzerAudioThreadAllocations = allocation_probe::count;
    const auto analyzerReady = analyzer.processIfReady();
    const auto* magnitudes = analyzer.getMagnitudes();
    const auto analyzerPeak = std::max_element (
        magnitudes + 1, magnitudes + dd::SpectrumFIFO::numBins);
    const auto analyzerPeakBin = static_cast<int> (
        std::distance (magnitudes, analyzerPeak));

    context.expect (
        engineAllocations == 0,
        "Single-band 0.9 processing allocated on the audio thread: "
            + juce::String (engineAllocations));
    context.expect (
        multibandAllocations == 0,
        "Multiband 0.9 processing allocated on the audio thread: "
            + juce::String (multibandAllocations));
    context.expect (
        analyzerAudioThreadAllocations == 0,
        "RTA transport allocated on the audio thread: "
            + juce::String (analyzerAudioThreadAllocations));
    context.expect (
        analyzerReady && std::abs (analyzerPeakBin - 171) <= 1,
        "RTA 8192-point reference frame has the wrong peak bin");
}
} // namespace

static void dumpAutoGainTable()
{
    constexpr std::array<double, 4> rates {
        44100.0, 48000.0, 96000.0, 192000.0
    };
    constexpr std::array<float, 5> drives {
        0.0f, 9.0f, 18.0f, 27.0f, 36.0f
    };
    constexpr std::array<float, 7> characters {
        -1.0f, -0.5f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f
    };
    constexpr std::array<float, 3> asymmetries {
        -1.0f, 0.0f, 1.0f
    };

    std::cout
        << "#pragma once\n\n"
        << "#include <array>\n\n"
        << "namespace dd::auto_gain_table\n{\n"
        << "inline constexpr std::array<double, 4> sampleRates { "
        << "44100.0, 48000.0, 96000.0, 192000.0 };\n"
        << "inline constexpr std::array<float, 5> drives { "
        << "0.0f, 9.0f, 18.0f, 27.0f, 36.0f };\n"
        << "inline constexpr std::array<float, 7> characters { "
        << "-1.0f, -0.5f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };\n"
        << "inline constexpr std::array<float, 3> asymmetries { "
        << "-1.0f, 0.0f, 1.0f };\n"
        << "inline constexpr std::array<float, "
        << dd::DistortionEngine::modeCount * rates.size()
            * dd::DistortionEngine::maximumStages * asymmetries.size()
            * characters.size() * drives.size()
        << "> gains {\n";

    std::cout << std::showpoint << std::setprecision (9);
    int valuesOnLine = 0;
    for (int mode = 0; mode < dd::DistortionEngine::modeCount; ++mode)
        for (const auto rate : rates)
            for (int stages = 1;
                 stages <= dd::DistortionEngine::maximumStages;
                 ++stages)
                for (const auto asymmetry : asymmetries)
                    for (const auto character : characters)
                        for (const auto drive : drives)
                        {
                            dd::Parameters parameters;
                            parameters.mode = mode;
                            parameters.driveDb = drive;
                            parameters.character = character;
                            parameters.asymmetry = asymmetry;
                            parameters.stages = stages;
                            const auto gain =
                                dd::DistortionEngine::calculateReferenceAutoGain (
                                    parameters, rate);
                            if (valuesOnLine == 0)
                                std::cout << "    ";
                            std::cout << gain << "f,";
                            ++valuesOnLine;
                            if (valuesOnLine >= 8)
                            {
                                std::cout << '\n';
                                valuesOnLine = 0;
                            }
                            else
                            {
                                std::cout << ' ';
                            }
                        }
    if (valuesOnLine != 0)
        std::cout << '\n';
    std::cout << "};\n} // namespace dd::auto_gain_table\n";
}

static void dumpSpectralAutoGainTable()
{
    constexpr std::array<float, 13> drives {
        0.0f, 3.0f, 6.0f, 9.0f, 12.0f, 15.0f, 18.0f,
        21.0f, 24.0f, 27.0f, 30.0f, 33.0f, 36.0f
    };
    constexpr std::array<float, 9> characters {
        0.0f, 0.125f, 0.25f, 0.375f, 0.5f,
        0.625f, 0.75f, 0.875f, 1.0f
    };
    std::cout
        << "#pragma once\n\n"
        << "#include <array>\n\n"
        << "namespace dd::spectral_auto_gain_table\n{\n"
        << "inline constexpr std::array<float, 13> drives { "
        << "0.0f, 3.0f, 6.0f, 9.0f, 12.0f, 15.0f, 18.0f, "
        << "21.0f, 24.0f, 27.0f, 30.0f, 33.0f, 36.0f };\n"
        << "inline constexpr std::array<float, 9> characters { "
        << "0.0f, 0.125f, 0.25f, 0.375f, 0.5f, "
        << "0.625f, 0.75f, 0.875f, 1.0f };\n"
        << "inline constexpr std::array<float, "
        << dd::DistortionEngine::maximumStages
            * drives.size() * characters.size()
        << "> gains {\n"
        << std::showpoint << std::setprecision (9);

    int valuesOnLine = 0;
    for (int stages = 1;
         stages <= dd::DistortionEngine::maximumStages;
         ++stages)
        for (const auto character : characters)
            for (const auto drive : drives)
            {
                dd::Parameters parameters;
                parameters.mode = static_cast<int> (
                    dd::DistortionEngine::Mode::spectralClip);
                parameters.driveDb = drive;
                parameters.character = character;
                parameters.stages = stages;
                const auto gain =
                    dd::DistortionEngine::calculateReferenceAutoGain (
                        parameters, sampleRate);
                if (valuesOnLine == 0)
                    std::cout << "    ";
                std::cout << gain << "f,";
                ++valuesOnLine;
                if (valuesOnLine >= 8)
                {
                    std::cout << '\n';
                    valuesOnLine = 0;
                }
                else
                {
                    std::cout << ' ';
                }
            }
    if (valuesOnLine != 0)
        std::cout << '\n';
    std::cout << "};\n} // namespace dd::spectral_auto_gain_table\n";
}

static void dumpSineErosionAutoGainTable()
{
    constexpr std::array<double, 4> rates {
        44100.0, 48000.0, 96000.0, 192000.0
    };
    constexpr std::array<float, 5> drives {
        0.0f, 9.0f, 18.0f, 27.0f, 36.0f
    };
    constexpr std::array<float, 5> characters {
        0.0f, 0.25f, 0.5f, 0.75f, 1.0f
    };
    constexpr std::array<float, 5> secondaryValues {
        0.0f, 0.25f, 0.5f, 0.75f, 1.0f
    };
    constexpr std::array<float, 3> asymmetries {
        -1.0f, 0.0f, 1.0f
    };

    std::cout
        << "#pragma once\n\n"
        << "#include <array>\n\n"
        << "namespace dd::sine_erosion_auto_gain_table\n{\n"
        << "inline constexpr std::array<double, 4> sampleRates { "
        << "44100.0, 48000.0, 96000.0, 192000.0 };\n"
        << "inline constexpr std::array<float, 5> drives { "
        << "0.0f, 9.0f, 18.0f, 27.0f, 36.0f };\n"
        << "inline constexpr std::array<float, 5> characters { "
        << "0.0f, 0.25f, 0.5f, 0.75f, 1.0f };\n"
        << "inline constexpr std::array<float, 5> secondaryValues { "
        << "0.0f, 0.25f, 0.5f, 0.75f, 1.0f };\n"
        << "inline constexpr std::array<float, 3> asymmetries { "
        << "-1.0f, 0.0f, 1.0f };\n"
        << "inline constexpr std::array<float, "
        << rates.size() * dd::DistortionEngine::maximumStages
            * asymmetries.size() * characters.size()
            * secondaryValues.size() * drives.size()
        << "> gains {\n"
        << std::showpoint << std::setprecision (9);

    int valuesOnLine = 0;
    for (const auto rate : rates)
        for (int stages = 1;
             stages <= dd::DistortionEngine::maximumStages;
             ++stages)
            for (const auto asymmetry : asymmetries)
                for (const auto character : characters)
                    for (const auto secondary : secondaryValues)
                        for (const auto drive : drives)
                        {
                            dd::Parameters parameters;
                            parameters.mode = static_cast<int> (
                                dd::DistortionEngine::Mode::sineErosion);
                            parameters.driveDb = drive;
                            parameters.character = character;
                            parameters.secondary = secondary;
                            parameters.asymmetry = asymmetry;
                            parameters.stages = stages;
                            const auto gain =
                                dd::DistortionEngine::calculateReferenceAutoGain (
                                    parameters, rate);
                            if (valuesOnLine == 0)
                                std::cout << "    ";
                            std::cout << gain << "f,";
                            ++valuesOnLine;
                            if (valuesOnLine >= 8)
                            {
                                std::cout << '\n';
                                valuesOnLine = 0;
                            }
                            else
                            {
                                std::cout << ' ';
                            }
                        }
    if (valuesOnLine != 0)
        std::cout << '\n';
    std::cout << "};\n} // namespace dd::sine_erosion_auto_gain_table\n";
}

static void dumpSecondaryAutoGainTable()
{
    constexpr std::array<int, 5> modes {
        static_cast<int> (dd::DistortionEngine::Mode::tapeHysteresis),
        static_cast<int> (dd::DistortionEngine::Mode::transformerCore),
        static_cast<int> (dd::DistortionEngine::Mode::downsample),
        static_cast<int> (dd::DistortionEngine::Mode::bitCrusher),
        static_cast<int> (dd::DistortionEngine::Mode::schmittHysteresis)
    };
    constexpr std::array<double, 4> rates {
        44100.0, 48000.0, 96000.0, 192000.0
    };
    constexpr std::array<float, 5> drives {
        0.0f, 9.0f, 18.0f, 27.0f, 36.0f
    };
    constexpr std::array<float, 5> characters {
        0.0f, 0.25f, 0.5f, 0.75f, 1.0f
    };
    constexpr std::array<float, 7> asymmetries {
        -1.0f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 1.0f
    };
    constexpr std::array<float, 5> secondaryValues {
        0.0f, 0.25f, 0.5f, 0.75f, 1.0f
    };

    std::cout
        << "#pragma once\n\n"
        << "#include <array>\n\n"
        << "namespace dd::secondary_auto_gain_table\n{\n"
        << "inline constexpr std::array<int, 5> modes { "
        << "5, 13, 21, 22, 26 };\n"
        << "inline constexpr std::array<double, 4> sampleRates { "
        << "44100.0, 48000.0, 96000.0, 192000.0 };\n"
        << "inline constexpr std::array<float, 5> drives { "
        << "0.0f, 9.0f, 18.0f, 27.0f, 36.0f };\n"
        << "inline constexpr std::array<float, 5> characters { "
        << "0.0f, 0.25f, 0.5f, 0.75f, 1.0f };\n"
        << "inline constexpr std::array<float, 7> asymmetries { "
        << "-1.0f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 1.0f };\n"
        << "inline constexpr std::array<float, 5> secondaryValues { "
        << "0.0f, 0.25f, 0.5f, 0.75f, 1.0f };\n"
        << "inline constexpr std::array<float, "
        << modes.size() * rates.size()
            * dd::DistortionEngine::maximumStages
            * asymmetries.size() * characters.size()
            * secondaryValues.size() * drives.size()
        << "> gains {\n"
        << std::showpoint << std::setprecision (9);

    int valuesOnLine = 0;
    for (const auto mode : modes)
        for (const auto rate : rates)
            for (int stages = 1;
                 stages <= dd::DistortionEngine::maximumStages;
                 ++stages)
                for (const auto asymmetry : asymmetries)
                    for (const auto character : characters)
                        for (const auto secondary : secondaryValues)
                            for (const auto drive : drives)
                            {
                                dd::Parameters parameters;
                                parameters.mode = mode;
                                parameters.driveDb = drive;
                                parameters.character = character;
                                parameters.asymmetry = asymmetry;
                                parameters.stages = stages;
                                parameters.secondary = secondary;
                                const auto gain =
                                    dd::DistortionEngine::calculateReferenceAutoGain (
                                        parameters, rate);
                                if (valuesOnLine == 0)
                                    std::cout << "    ";
                                std::cout << gain << "f,";
                                ++valuesOnLine;
                                if (valuesOnLine >= 8)
                                {
                                    std::cout << '\n';
                                    valuesOnLine = 0;
                                }
                                else
                                {
                                    std::cout << ' ';
                                }
                            }
    if (valuesOnLine != 0)
        std::cout << '\n';
    std::cout << "};\n} // namespace dd::secondary_auto_gain_table\n";
}

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "--dump-regression")
    {
        dumpRegressionFingerprints();
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--dump-visualization")
    {
        dumpVisualizationFingerprints();
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--dump-auto-gain-table")
    {
        dumpAutoGainTable();
        return 0;
    }
    if (argc > 1 && juce::String (argv[1])
            == "--dump-spectral-auto-gain-table")
    {
        dumpSpectralAutoGainTable();
        return 0;
    }
    if (argc > 1 && juce::String (argv[1])
            == "--dump-sine-erosion-auto-gain-table")
    {
        dumpSineErosionAutoGainTable();
        return 0;
    }
    if (argc > 1 && juce::String (argv[1])
            == "--dump-secondary-auto-gain-table")
    {
        dumpSecondaryAutoGainTable();
        return 0;
    }

    TestContext context;
    testNewDefaultsAndGlobalBypass (context);
    testModeMetadata (context);
    testVersionEightVisualizationSnapshots (context);
    testMultibandCrossoversAndSmartGain (context);
    testCanonicalClipCeilings (context);
    testVitalClipTransfers (context);
    testClipMorphEndpointsAndHardPlateau (context);
    testEveryMode (context);
    testStageCascadeChangesAudio (context);
#if JUCE_MAC
    testReaperSizedAudioThreadStack (context);
#endif
    testOversamplingPaths (context);
    testAutoGainForEveryMode (context);
    testSpectralClipAutoGain (context);
    testSineErosionNoiseAutoGain (context);
    testSecondaryControlAutoGain (context);
    testEveryCharacterHasARealVisualization (context);
    testDownsampleExtreme (context);
    testModesArePairwiseDistinct (context);
    testAlgorithmIntentInvariants (context);
    testEveryModeAtMaximum (context);
    testDriveStartsContinuously (context);
    testDigitalClockIgnoresOversampling (context);
    testTapeOversamplingConsistency (context);
    testTapeDoesNotCollapseOrModulate (context);
    testOversamplingReducesAliasing (context);
    testSmartAutoGainFreezes (context);
    testStereoAsymmetryUsesOppositePolarities (context);
    testNewTopologyAndSafetyInvariants (context);
    testRevisedAlgorithmContracts (context);
    testRequestedDevelopmentFixes (context);
    testSecondaryToneControlsAndSineRelease (context);
    testSharedSecondaryContract (context);
    testOutputCeilingAtZeroDb (context);
    testInstantTableAutoGain (context);
    testVersionNineNeutralDynamicAndToneFilters (context);
    testVersionNinePlacementRouting (context);
    testVersionNineAutomationMatrix (context);
    testVersionNineAudioThreadAllocationBoundary (context);

    if (context.failures == 0)
    {
        std::cout << "All default_distortion DSP tests passed.\n";
        return 0;
    }

    std::cerr << context.failures << " test assertion(s) failed.\n";
    return 1;
}
