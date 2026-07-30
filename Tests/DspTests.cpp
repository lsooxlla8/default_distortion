#include "../Source/DistortionEngine.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <set>
#include <vector>

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
        "Half-Wave Rectifier",
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
                    ? 1.02f
                    : 1.00001f;
            context.expect (
                peak >= 0.999f && peak <= maximumPeak,
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
            const auto expected = vitalTanhReference (input);
            context.expect (
                std::abs (view.output[point] - expected) < 2.0e-6f,
                dd::DistortionEngine::getModeNames()[
                    static_cast<size_t> (parameters.mode)]
                    + " no longer matches the copied Vital transfer");
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
                    ? cubicReference (view.input[point])
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

double measureModeRms (int mode, bool autoGain, float mix)
{
    dd::DistortionEngine engine;
    engine.prepare (sampleRate, blockSize, 2);

    dd::Parameters parameters;
    parameters.mode = mode;
    parameters.driveDb = 20.0f;
    parameters.character =
        dd::DistortionEngine::isCharacterBipolar (mode) ? 0.35f : 0.70f;
    parameters.asymmetry = 0.12f;
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
        fillSignal (buffer, phase);
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
    const auto text = dd::DistortionEngine::formatDriveValue (
        static_cast<int> (dd::DistortionEngine::Mode::downsample),
        36.0f,
        sampleRate);
    context.expect (
        text.containsIgnoreCase ("1.0 Hz"),
        "Downsample maximum does not reach a one-Hertz sample clock");
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
            downsampleMode, 36.0f, sampleRate).containsIgnoreCase ("1.0 Hz"),
        "Downsample Drive does not reach a one-Hertz sample clock");
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
        dd::DistortionEngine::Mode::halfWaveRectifier);
    parameters.character = 0.0f;
    dd::DistortionEngine::Visualization leakyHalfWave;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, leakyHalfWave);
    parameters.character = 1.0f;
    dd::DistortionEngine::Visualization precisionHalfWave;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, precisionHalfWave);
    context.expect (
        visualizationDistance (leakyHalfWave, precisionHalfWave) > 0.05,
        "Half-Wave conduction control has too little audible range");

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
        thresholdDistance > 0.30 && thresholdDistance < 1.35,
        "Sign/Square Threshold is not useful and bounded (distance "
            + juce::String (thresholdDistance, 3) + ")");

    parameters.driveDb = 18.0f;
    parameters.stages = 2;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::transformerCore);
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
    context.expect (
        transformerDistance > 0.12,
        "Transformer Core Character still has too little influence (distance "
            + juce::String (transformerDistance, 3) + ")");

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
    for (const auto value : softClip.output)
        context.expect (
            std::abs (value) <= 1.000001f,
            "Soft Clip visualization exceeds the revised 0 dBFS ceiling");

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
        signThresholdDistance > 0.30 && signThresholdDistance < 1.35,
        "Sign/Square Threshold is not useful and bounded after decoupling");

    parameters.driveDb = 18.0f;
    parameters.character = 1.0f;
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::fullWaveRectifier);
    dd::DistortionEngine::Visualization fullWave;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, fullWave);
    parameters.mode = static_cast<int> (
        dd::DistortionEngine::Mode::halfWaveRectifier);
    dd::DistortionEngine::Visualization halfWave;
    dd::DistortionEngine::makeVisualization (
        parameters, sampleRate, halfWave);
    context.expect (
        visualizationDistance (fullWave, halfWave) > 0.28,
        "Half-Wave remains too close to Full-Wave at full conduction");

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
            std::abs (mean) < 0.08 * juce::jmax (1.0e-6, meanAbsolute),
            "Delta Crusher reconstruction has collapsed into DC");
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

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "--dump-regression")
    {
        dumpRegressionFingerprints();
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

    TestContext context;
    testModeMetadata (context);
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
    testEveryCharacterHasARealVisualization (context);
    testDownsampleExtreme (context);
    testModesArePairwiseDistinct (context);
    testAlgorithmIntentInvariants (context);
    testEveryModeAtMaximum (context);
    testDriveStartsContinuously (context);
    testDigitalClockIgnoresOversampling (context);
    testOversamplingReducesAliasing (context);
    testSmartAutoGainFreezes (context);
    testStereoAsymmetryUsesOppositePolarities (context);
    testNewTopologyAndSafetyInvariants (context);
    testRevisedAlgorithmContracts (context);
    testRequestedDevelopmentFixes (context);
    testOutputCeilingAtZeroDb (context);
    testInstantTableAutoGain (context);

    if (context.failures == 0)
    {
        std::cout << "All default_distortion DSP tests passed.\n";
        return 0;
    }

    std::cerr << context.failures << " test assertion(s) failed.\n";
    return 1;
}
