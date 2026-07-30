#include "DistortionEngine.h"
#include "AutoGainTable.h"
#include "SpectralAutoGainTable.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace dd
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

float clampBipolar (float value) noexcept
{
    return juce::jlimit (-1.0f, 1.0f, value);
}

float lerp (float a, float b, float amount) noexcept
{
    return a + amount * (b - a);
}

float unipolarCharacter (float value) noexcept
{
    return juce::jlimit (0.0f, 1.0f, value);
}

float bipolarTo01 (float value) noexcept
{
    return 0.5f * (juce::jlimit (-1.0f, 1.0f, value) + 1.0f);
}

float smoothStep01 (float value) noexcept
{
    const auto bounded = juce::jlimit (0.0f, 1.0f, value);
    return bounded * bounded * (3.0f - 2.0f * bounded);
}

// Scalar adaptation of Vital's GPLv3 futils::tanh approximation and the
// Soft/Hard Clip functions that call it. Original copyright 2013-2019
// Matt Tytel. See THIRD_PARTY_NOTICES.md.
float vitalTanh (float value) noexcept
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
}

float vitalSoftClip (float value, float drive) noexcept
{
    return vitalTanh (value * drive);
}

float vitalHardClip (float value, float drive) noexcept
{
    return juce::jlimit (-1.0f, 1.0f, value * drive);
}

float driveDepth (float driveDb) noexcept
{
    const auto normalised = juce::jlimit (0.0f, 1.0f, driveDb / 36.0f);
    return normalised * normalised * (3.0f - 2.0f * normalised);
}

float smoothTowards (float current, float target, double rate, double timeSeconds) noexcept
{
    const auto coefficient = std::exp (-1.0 / juce::jmax (1.0, rate * timeSeconds));
    return target + static_cast<float> (coefficient) * (current - target);
}

float smoothingCoefficient (double rate, double timeSeconds) noexcept
{
    return static_cast<float> (
        std::exp (-1.0 / juce::jmax (1.0, rate * timeSeconds)));
}

float foldPrimitive (float input, float threshold) noexcept
{
    const auto safeThreshold = juce::jmax (0.001f, threshold);
    auto phase = std::fmod (std::abs (input) / safeThreshold, 4.0f);
    if (phase < 0.0f)
        phase += 4.0f;

    float primitive = 0.0f;
    if (phase <= 1.0f)
        primitive = 0.5f * phase * phase;
    else if (phase <= 3.0f)
        primitive = 2.0f * phase - 0.5f * phase * phase - 1.0f;
    else
        primitive = 0.5f * phase * phase - 4.0f * phase + 8.0f;
    return safeThreshold * primitive;
}

float foldDirect (float input, float threshold) noexcept
{
    const auto safeThreshold = juce::jmax (0.001f, threshold);
    const auto period = 4.0f * safeThreshold;
    auto wrapped = std::fmod (input + safeThreshold, period);
    if (wrapped < 0.0f)
        wrapped += period;
    const auto triangle = safeThreshold
        - std::abs (wrapped - 2.0f * safeThreshold);
    return triangle / safeThreshold;
}

float topologyFoldDirect (float input, int topology) noexcept
{
    if (topology <= 0)
        return foldDirect (input, 1.0f);
    if (topology == 1)
        return 0.62f * foldDirect (input, 1.0f)
            + 0.38f * foldDirect (input, 0.53f);

    // A parallel, multi-cell transfer inspired by West Coast analogue
    // wavefolders. The thresholds are deliberately non-uniform, which makes
    // successive folds enter at different levels instead of behaving like
    // another recursive triangle folder.
    return 0.52f * foldDirect (input, 1.0f)
        + 0.31f * foldDirect (input, 0.58f)
        + 0.17f * foldDirect (input, 0.31f);
}

float topologyFoldPrimitive (float input, int topology) noexcept
{
    if (topology <= 0)
        return foldPrimitive (input, 1.0f);
    if (topology == 1)
        return 0.62f * foldPrimitive (input, 1.0f)
            + 0.38f * foldPrimitive (input, 0.53f);
    return 0.52f * foldPrimitive (input, 1.0f)
        + 0.31f * foldPrimitive (input, 0.58f)
        + 0.17f * foldPrimitive (input, 0.31f);
}

std::uint64_t hashDeterministicGainParameters (
    const Parameters& parameters) noexcept
{
    auto hash = UINT64_C (1469598103934665603);
    const auto add = [&hash] (std::uint32_t value)
    {
        hash ^= value;
        hash *= UINT64_C (1099511628211);
    };
    add (static_cast<std::uint32_t> (parameters.mode));
    add (std::bit_cast<std::uint32_t> (parameters.driveDb));
    add (std::bit_cast<std::uint32_t> (parameters.character));
    add (std::bit_cast<std::uint32_t> (parameters.asymmetry));
    add (static_cast<std::uint32_t> (parameters.asymmetryStereo));
    add (static_cast<std::uint32_t> (parameters.stages));
    return hash;
}

std::uint64_t hashSmartGainParameters (const Parameters& parameters) noexcept
{
    auto hash = hashDeterministicGainParameters (parameters);
    const auto add = [&hash] (std::uint32_t value)
    {
        hash ^= value;
        hash *= UINT64_C (1099511628211);
    };
    add (std::bit_cast<std::uint32_t> (parameters.tone));
    add (static_cast<std::uint32_t> (parameters.quality));
    return hash;
}

float wetOutputScale (
    DistortionEngine::Mode mode,
    float stageGain) noexcept
{
    const auto scalePreserving =
        mode == DistortionEngine::Mode::fullWaveRectifier
        || mode == DistortionEngine::Mode::softFullWaveRectifier
        || mode == DistortionEngine::Mode::halfWaveRectifier
        || mode == DistortionEngine::Mode::slewLimiter;
    return scalePreserving
        ? 1.0f / juce::jmax (1.0f, stageGain)
        : 1.0f;
}

float finishDirectStage (
    float value,
    DistortionEngine::Mode mode,
    float stageGain) noexcept
{
    if (mode == DistortionEngine::Mode::slewLimiter)
        return value * wetOutputScale (mode, stageGain);

    // The two canonical clip modes define their ceiling directly in the
    // transfer function. Do not pull that ceiling below 0 dBFS afterwards.
    if (mode == DistortionEngine::Mode::morphSoftClip
        || mode == DistortionEngine::Mode::hardClip)
        return value;

    value *= std::pow (juce::jmax (1.0f, stageGain), -0.22f);
    if (mode == DistortionEngine::Mode::softFullWaveRectifier)
        value = juce::jlimit (-64.0f, 64.0f, value);
    return value;
}

float dcBlockingAmount (DistortionEngine::Mode mode,
                        float character,
                        float asymmetry) noexcept
{
    if (mode == DistortionEngine::Mode::fullWaveRectifier
        || mode == DistortionEngine::Mode::softFullWaveRectifier
        || mode == DistortionEngine::Mode::halfWaveRectifier
        || mode == DistortionEngine::Mode::harmonicMorph
        || mode == DistortionEngine::Mode::transistorFet)
        return 1.0f;

    const auto characterCreatesDc =
        mode == DistortionEngine::Mode::signSquare
        || mode == DistortionEngine::Mode::triodeStage;
    const auto canGenerateDc =
        characterCreatesDc || std::abs (asymmetry) > 1.0e-6f;
    if (! canGenerateDc)
        return 0.0f;

    // Keep the blocker running at all times, then fade in its contribution
    // through the first five percent of any DC-producing bias control. This
    // avoids switching a stateful filter into the signal path abruptly.
    const auto amount = characterCreatesDc
        ? juce::jmax (std::abs (character), std::abs (asymmetry))
        : std::abs (asymmetry);
    return smoothStep01 (amount / 0.05f);
}

template <typename Value, size_t Size>
struct TablePosition
{
    size_t lower = 0;
    size_t upper = 0;
    float fraction = 0.0f;
};

template <typename Value, size_t Size>
TablePosition<Value, Size> tablePosition (
    const std::array<Value, Size>& values,
    Value requested) noexcept
{
    if (requested <= values.front())
        return {};
    if (requested >= values.back())
        return { Size - 1, Size - 1, 0.0f };

    for (size_t upper = 1; upper < Size; ++upper)
        if (requested <= values[upper])
        {
            const auto lower = upper - 1;
            const auto span = values[upper] - values[lower];
            return {
                lower,
                upper,
                static_cast<float> (
                    (requested - values[lower]) / span)
            };
        }
    return { Size - 1, Size - 1, 0.0f };
}
} // namespace

DistortionEngine::DistortionEngine()
{
    for (int i = 0; i < fftSize; ++i)
        spectralWindow[static_cast<size_t> (i)] =
            0.5f - 0.5f * std::cos (2.0f * pi * static_cast<float> (i)
                                    / static_cast<float> (fftSize));

}

DistortionEngine::~DistortionEngine() = default;

const std::array<juce::String, DistortionEngine::modeCount>& DistortionEngine::getModeNames()
{
    static const std::array<juce::String, modeCount> names {
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
    return names;
}

const std::array<juce::String, DistortionEngine::modeCount>& DistortionEngine::getCharacterNames()
{
    static const std::array<juce::String, modeCount> names {
        "CURVE",
        "SOFTNESS",
        "TOPOLOGY",
        "BIAS",
        "GATE",
        "HYSTERESIS",
        "EVEN / ODD",
        "TONE",
        "KNEE",
        "THRESHOLD",
        "DEAD ZONE",
        "RECTIFY",
        "SOFTNESS",
        "CORE",
        "CONDUCTION",
        "BIAS GAP",
        "TOPOLOGY",
        "REFLECTION",
        "CURVATURE",
        "ORDER",
        "PERIOD",
        "SMOOTHING",
        "SMOOTHING",
        "ROTATION",
        "STEP",
        "RATE",
        "LOOP WIDTH",
        "FEEDBACK",
        "COLOR",
        "RECOVERY"
    };
    return names;
}

bool DistortionEngine::isCharacterBipolar (int mode) noexcept
{
    const auto selected = static_cast<Mode> (
        juce::jlimit (0, modeCount - 1, mode));
    return selected == Mode::signSquare
        || selected == Mode::harmonicMorph
        || selected == Mode::triodeStage
        || selected == Mode::transistorFet
        || selected == Mode::feedbackSaturator;
}

bool DistortionEngine::isCharacterStepped (int mode) noexcept
{
    const auto selected = static_cast<Mode> (
        juce::jlimit (0, modeCount - 1, mode));
    return selected == Mode::topologyFold;
}

float DistortionEngine::getDefaultCharacter (int mode) noexcept
{
    const auto selected = static_cast<Mode> (
        juce::jlimit (0, modeCount - 1, mode));
    return selected == Mode::fullWaveRectifier
            || selected == Mode::classBSaturation
            || selected == Mode::phaseDistortion
            || selected == Mode::deltaCrusher
        ? 0.5f
        : 0.0f;
}

juce::String DistortionEngine::formatCharacterValue (int mode,
                                                     float rawValue,
                                                     double displaySampleRate)
{
    juce::ignoreUnused (displaySampleRate);
    const auto selected = static_cast<Mode> (
        juce::jlimit (0, modeCount - 1, mode));
    const auto unipolar = unipolarCharacter (rawValue);
    const auto bipolar = juce::jlimit (-1.0f, 1.0f, rawValue);

    const auto percentage = isCharacterBipolar (mode)
        ? juce::roundToInt (100.0f * bipolar)
        : juce::roundToInt (100.0f * unipolar);
    const auto prefix = juce::String (percentage) + "%";

    if (selected == Mode::chebyshevFold)
        return prefix + " / " + juce::String (2.0f + 6.0f * unipolar, 1);

    if (selected == Mode::topologyFold)
        return prefix + " / " + (unipolar < 0.25f ? "SINGLE"
            : (unipolar > 0.75f ? "WEST COAST" : "DUAL"));

    if (selected == Mode::bitRotation)
        return prefix + " / "
            + juce::String (juce::roundToInt (15.0f * unipolar)) + " bit";

    return prefix;
}

juce::String DistortionEngine::formatDriveValue (int mode,
                                                 float driveDb,
                                                 double displaySampleRate)
{
    const auto selected = static_cast<Mode> (
        juce::jlimit (0, modeCount - 1, mode));
    const auto normalised = juce::jlimit (0.0f, 1.0f, driveDb / 36.0f);

    if (selected == Mode::bitCrusher)
    {
        const auto bits = juce::jlimit (
            1, 24, 24 - juce::roundToInt (23.0f * normalised));
        return juce::String (bits) + " bit";
    }

    if (selected == Mode::downsample)
    {
        const auto rate = downsampleTargetRate (
            normalised, displaySampleRate);
        return rate >= 1000.0f
            ? juce::String (rate / 1000.0f, rate >= 10000.0f ? 0 : 1)
                + " kHz"
            : juce::String (rate, rate >= 100.0f ? 0 : 1) + " Hz";
    }

    const auto clean = std::abs (driveDb) < 0.005f ? 0.0f : driveDb;
    return juce::String (clean, 1) + " dB";
}

void DistortionEngine::makeVisualization (const Parameters& parameters,
                                          double displaySampleRate,
                                          Visualization& visualization)
{
    const auto mode = static_cast<Mode> (
        juce::jlimit (0, modeCount - 1, parameters.mode));
    const auto stages = juce::jlimit (1, maximumStages, parameters.stages);
    const auto driveNormalised = juce::jlimit (
        0.0f, 1.0f, parameters.driveDb / 36.0f);
    const auto stageDriveDb = parameters.driveDb;
    const auto stageGain = juce::Decibels::decibelsToGain (stageDriveDb);
    const auto stageDepth = driveDepth (parameters.driveDb);

    visualization.timeDomain =
        mode == Mode::halfWaveRectifier
        || mode == Mode::downsample
        || mode == Mode::deltaCrusher
        || mode == Mode::tapeHysteresis
        || mode == Mode::transformerCore
        || mode == Mode::slewLimiter
        || mode == Mode::schmittHysteresis
        || mode == Mode::dynamicSag
        || mode == Mode::feedbackSaturator
        || mode == Mode::resonantFeedbackClip
        || mode == Mode::phaseDistortion;
    visualization.spectralDomain = false;

    if (mode == Mode::spectralClip)
    {
        visualization.timeDomain = true;
        SpectralState previewState;
        juce::dsp::FFT previewFft { fftOrder };
        std::array<float, fftSize> previewWindow {};
        std::array<float, fftSize> inputDelay {};
        for (int i = 0; i < fftSize; ++i)
            previewWindow[static_cast<size_t> (i)] =
                0.5f - 0.5f * std::cos (
                    2.0f * pi * static_cast<float> (i)
                    / static_cast<float> (fftSize));

        constexpr auto warmupSamples = fftSize * 6;
        constexpr auto totalSamples =
            warmupSamples + Visualization::pointCount;
        auto delayPosition = 0;
        for (int sample = 0; sample < totalSamples; ++sample)
        {
            // Offset the periodic source by fftSize modulo pointCount so the
            // latency-aligned input shown below begins at exactly zero. The
            // captured period rises 0 -> +1, resets at its centre to -1, then
            // rises back to 0 at the right edge.
            const auto sawIndex =
                (sample + (fftSize % Visualization::pointCount))
                % Visualization::pointCount;
            const auto input =
                sawIndex < Visualization::pointCount / 2
                    ? static_cast<float> (sawIndex)
                        / static_cast<float> (
                            Visualization::pointCount / 2 - 1)
                    : -1.0f
                        + static_cast<float> (
                            sawIndex - Visualization::pointCount / 2)
                            / static_cast<float> (
                                Visualization::pointCount / 2 - 1);
            const auto alignedInput =
                inputDelay[static_cast<size_t> (delayPosition)];
            inputDelay[static_cast<size_t> (delayPosition)] = input;
            delayPosition = (delayPosition + 1) % fftSize;
            const auto output = processSpectralSampleCore (
                input,
                previewState,
                parameters,
                previewFft,
                previewWindow);

            if (sample >= warmupSamples)
            {
                const auto point = static_cast<size_t> (
                    sample - warmupSamples);
                visualization.input[point] = alignedInput;
                visualization.output[point] = output;
            }
        }
        return;
    }

    std::array<StageState, maximumStages> states {};
    const auto simulationSampleRate =
        mode == Mode::downsample
            ? juce::jmin (
                displaySampleRate,
                juce::jmax (
                    64.0,
                    24.0 * static_cast<double> (
                        downsampleTargetRate (
                            driveNormalised,
                            displaySampleRate))))
            : (mode == Mode::phaseDistortion
                ? juce::jmin (displaySampleRate, 1600.0)
                : displaySampleRate);
    if (mode == Mode::phaseDistortion)
    {
        const auto requiredDelaySamples = juce::roundToInt (
            0.05 * simulationSampleRate) + 2;
        for (auto& state : states)
            state.ensurePhaseDelaySize (requiredDelaySamples);
    }
    const auto modeContext = makeModeContext (
        mode,
        parameters.character,
        parameters.asymmetry,
        driveNormalised,
        simulationSampleRate,
        displaySampleRate);
    float softClipPositiveReference = 1.0f;
    float softClipNegativeReference = -1.0f;
    if (mode == Mode::morphSoftClip)
    {
        std::array<StageState, maximumStages> positiveStates {};
        std::array<StageState, maximumStages> negativeStates {};
        softClipPositiveReference = processCascadeSample (
            1.0f,
            modeContext,
            stageGain,
            stageDepth,
            stages,
            positiveStates);
        softClipNegativeReference = processCascadeSample (
            -1.0f,
            modeContext,
            stageGain,
            stageDepth,
            stages,
            negativeStates);
    }

    for (int point = 0; point < Visualization::pointCount; ++point)
    {
        const auto position = static_cast<float> (point)
            / static_cast<float> (Visualization::pointCount - 1);
        const auto input = visualization.timeDomain
            ? 0.92f * std::sin (position * 6.0f * pi)
            : lerp (-1.5f, 1.5f, position);
        visualization.input[static_cast<size_t> (point)] = input;

        if (! visualization.timeDomain)
            for (auto& state : states)
                state.reset();

        auto displayedOutput = processCascadeSample (
                input,
                modeContext,
                stageGain,
                stageDepth,
                stages,
                states);
        // The Soft Clip graph uses the same 0 dB reference as Hard Clip:
        // a +1/-1 input point is drawn exactly on the +1/-1 output line.
        // This is display-only normalisation; the Vital transfer used by the
        // audio path remains untouched.
        if (mode == Mode::morphSoftClip)
        {
            const auto reference = displayedOutput >= 0.0f
                ? std::abs (softClipPositiveReference)
                : std::abs (softClipNegativeReference);
            displayedOutput = clampBipolar (
                displayedOutput / juce::jmax (1.0e-6f, reference));
        }
        visualization.output[static_cast<size_t> (point)] = displayedOutput;
    }
}

void DistortionEngine::prepare (double newSampleRate, int maximumBlockSize, int channels)
{
    sampleRate = newSampleRate;
    preparedBlockSize = juce::jmax (1, maximumBlockSize);
    preparedChannels = juce::jlimit (1, maximumChannels, channels);
    const auto requiredPhaseDelaySamples =
        juce::roundToInt (0.05 * sampleRate) + 2;
    for (auto& channel : stageStates)
        for (auto& stage : channel)
            stage.ensurePhaseDelaySize (requiredPhaseDelaySamples);
    int maximumOversamplingLatency = 0;
    for (int index = 0; index < 3; ++index)
    {
        oversamplers[static_cast<size_t> (index)] =
            std::make_unique<Oversampler> (
                static_cast<size_t> (preparedChannels),
                static_cast<size_t> (index + 1),
                Oversampler::filterHalfBandPolyphaseIIR,
                true,
                true);

        auto& oversampler = *oversamplers[static_cast<size_t> (index)];
        oversampler.initProcessing (static_cast<size_t> (preparedBlockSize));
        oversampler.reset();

        oversamplingLatencies[static_cast<size_t> (index)] =
            juce::roundToInt (oversampler.getLatencyInSamples());
        maximumOversamplingLatency =
            juce::jmax (maximumOversamplingLatency,
                        oversamplingLatencies[static_cast<size_t> (index)]);
    }

    fixedLatencySamples = fftSize + maximumOversamplingLatency;
    const auto delayCapacity = static_cast<size_t> (
        juce::jmax (4096, fixedLatencySamples + preparedBlockSize * 2));

    for (int channel = 0; channel < maximumChannels; ++channel)
    {
        dryDelayBuffers[static_cast<size_t> (channel)].assign (delayCapacity, 0.0f);
        wetDelayBuffers[static_cast<size_t> (channel)].assign (delayCapacity, 0.0f);
    }

    dryBuffer.setSize (preparedChannels, preparedBlockSize, false, false, true);
    lastToneCoefficientAmount = std::numeric_limits<float>::quiet_NaN();
    updateToneFilters (0.0f);
    prepareKWeightingFilters();
    reset();
}

void DistortionEngine::primeAutoGain (const Parameters& parameters)
{
    const auto signature = hashDeterministicGainParameters (parameters);
    deterministicGainLinear = parameters.autoGainMode > 0
        ? lookupDeterministicGain (parameters, sampleRate)
        : 1.0f;
    normalGainLinear = deterministicGainLinear;
    autoGainLinear = parameters.autoGainMode == 1
        ? normalGainLinear
        : deterministicGainLinear;
    lastGainSignature = signature;
    lastSmartGainSignature = hashSmartGainParameters (parameters);
    lastAutoGainMode = parameters.autoGainMode;
    normalGainTracking = false;
    normalGainTrackingSamples = 0;
    resetSmartAutoGain();
}

void DistortionEngine::reset()
{
    for (auto& channel : stageStates)
        for (auto& stage : channel)
            stage.reset();

    for (auto& state : spectralStates)
        state.reset();

    for (auto& filters : toneFilters)
        filters.reset();

    for (auto& oversampler : oversamplers)
        if (oversampler != nullptr)
            oversampler->reset();

    for (auto& buffer : dryDelayBuffers)
        std::fill (buffer.begin(), buffer.end(), 0.0f);
    for (auto& buffer : wetDelayBuffers)
        std::fill (buffer.begin(), buffer.end(), 0.0f);

    dcPreviousInput.fill (0.0f);
    dcPreviousOutput.fill (0.0f);
    dcMixState.fill (0.0f);
    dryDelayPositions.fill (0);
    wetDelayPositions.fill (0);
    autoGainLinear = 1.0f;
    deterministicGainLinear = 1.0f;
    normalGainLinear = 1.0f;
    normalGainTracking = false;
    normalGainTrackingSamples = 0;
    lastGainSignature = 0;
    lastSmartGainSignature = 0;
    resetSmartAutoGain();
    lastMode = -1;
    lastAutoGainMode = -1;
}

void DistortionEngine::resetSmartAutoGain() noexcept
{
    smartGainLinear = deterministicGainLinear;
    smartWetPeak = 0.0;
    smartDrySliceEnergy = 0.0;
    smartWetSliceEnergy = 0.0;
    smartDryRecentSlices.fill (0.0);
    smartWetRecentSlices.fill (0.0);
    smartDryLoudnessBlocks.fill (0.0);
    smartWetLoudnessBlocks.fill (0.0);
    smartSliceSamples = 0;
    smartSliceWritePosition = 0;
    smartCompletedSlices = 0;
    smartLoudnessBlockCount = 0;
    for (auto& filter : smartDryKWeighting)
        filter.reset();
    for (auto& filter : smartWetKWeighting)
        filter.reset();
    smartStableSamples = 0;
    smartMeasuredSamples = 0;
    smartGainLocked = false;
    smartProgress.store (0.0f, std::memory_order_relaxed);
    smartLockedForUi.store (false, std::memory_order_relaxed);
}

void DistortionEngine::prepareKWeightingFilters()
{
    // ITU-R BS.1770 K-weighting: the 4 dB head-model shelf followed by
    // revised low-frequency B-weighting. JUCE regenerates the biquads for the
    // actual host sample rate instead of reusing the 48 kHz reference values.
    constexpr auto shelfFrequency = 1681.974450955533;
    constexpr auto shelfQ = 0.7071752369554196f;
    constexpr auto shelfGainDb = 3.999843853973347f;
    constexpr auto highPassFrequency = 38.13547087602444;
    constexpr auto highPassQ = 0.5003270373238773f;

    const auto shelf = juce::IIRCoefficients::makeHighShelf (
        sampleRate,
        shelfFrequency,
        shelfQ,
        juce::Decibels::decibelsToGain (shelfGainDb));
    const auto highPass = juce::IIRCoefficients::makeHighPass (
        sampleRate,
        highPassFrequency,
        highPassQ);

    for (auto* bank : { &smartDryKWeighting, &smartWetKWeighting })
        for (auto& filter : *bank)
        {
            filter.shelf.setCoefficients (shelf);
            filter.highPass.setCoefficients (highPass);
            filter.reset();
        }
}

void DistortionEngine::accumulateLoudnessSample (
    float dry,
    float wet,
    int channel) noexcept
{
    const auto index = static_cast<size_t> (
        juce::jlimit (0, maximumChannels - 1, channel));
    const auto weightedDry = smartDryKWeighting[index].process (dry);
    const auto weightedWet = smartWetKWeighting[index].process (wet);
    smartDrySliceEnergy += static_cast<double> (weightedDry) * weightedDry;
    smartWetSliceEnergy += static_cast<double> (weightedWet) * weightedWet;
}

void DistortionEngine::finishLoudnessSlice() noexcept
{
    const auto safeSamples = juce::jmax (1, smartSliceSamples);
    const auto position = static_cast<size_t> (smartSliceWritePosition);
    smartDryRecentSlices[position] =
        smartDrySliceEnergy / static_cast<double> (safeSamples);
    smartWetRecentSlices[position] =
        smartWetSliceEnergy / static_cast<double> (safeSamples);
    smartSliceWritePosition = (smartSliceWritePosition + 1) % 4;
    ++smartCompletedSlices;

    if (smartCompletedSlices >= 4
        && smartLoudnessBlockCount
            < static_cast<int> (smartDryLoudnessBlocks.size()))
    {
        double dryBlockEnergy = 0.0;
        double wetBlockEnergy = 0.0;
        for (size_t slice = 0; slice < smartDryRecentSlices.size(); ++slice)
        {
            dryBlockEnergy += smartDryRecentSlices[slice];
            wetBlockEnergy += smartWetRecentSlices[slice];
        }
        const auto block = static_cast<size_t> (smartLoudnessBlockCount++);
        smartDryLoudnessBlocks[block] = 0.25 * dryBlockEnergy;
        smartWetLoudnessBlocks[block] = 0.25 * wetBlockEnergy;
    }

    smartDrySliceEnergy = 0.0;
    smartWetSliceEnergy = 0.0;
    smartSliceSamples = 0;
}

double DistortionEngine::calculateGatedLoudnessEnergy (
    const std::array<double, 8>& blocks,
    int blockCount) noexcept
{
    constexpr auto absoluteGateLufs = -70.0;
    constexpr auto loudnessOffset = -0.691;
    const auto count = juce::jlimit (
        0, static_cast<int> (blocks.size()), blockCount);

    double absoluteEnergy = 0.0;
    int absoluteCount = 0;
    for (int block = 0; block < count; ++block)
    {
        const auto energy = blocks[static_cast<size_t> (block)];
        if (energy <= 0.0)
            continue;
        const auto loudness = loudnessOffset + 10.0 * std::log10 (energy);
        if (loudness > absoluteGateLufs)
        {
            absoluteEnergy += energy;
            ++absoluteCount;
        }
    }

    if (absoluteCount == 0)
        return 0.0;

    const auto absoluteMean =
        absoluteEnergy / static_cast<double> (absoluteCount);
    const auto relativeGate =
        loudnessOffset + 10.0 * std::log10 (absoluteMean) - 10.0;
    const auto gate = juce::jmax (absoluteGateLufs, relativeGate);

    double gatedEnergy = 0.0;
    int gatedCount = 0;
    for (int block = 0; block < count; ++block)
    {
        const auto energy = blocks[static_cast<size_t> (block)];
        if (energy <= 0.0)
            continue;
        const auto loudness = loudnessOffset + 10.0 * std::log10 (energy);
        if (loudness > gate)
        {
            gatedEnergy += energy;
            ++gatedCount;
        }
    }

    return gatedCount > 0
        ? gatedEnergy / static_cast<double> (gatedCount)
        : 0.0;
}

void DistortionEngine::process (juce::AudioBuffer<float>& buffer,
                                const Parameters& parameters)
{
    const auto channels = juce::jmin (preparedChannels, buffer.getNumChannels());
    const auto samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    dryBuffer.setSize (channels, samples, false, false, true);
    for (int channel = 0; channel < channels; ++channel)
        dryBuffer.copyFrom (channel, 0, buffer, channel, 0, samples);

    const auto requestedMode = juce::jlimit (0, modeCount - 1, parameters.mode);
    if (requestedMode != lastMode)
    {
        for (auto& channel : stageStates)
            for (auto& stage : channel)
                stage.reset();
        for (auto& state : spectralStates)
            state.reset();
        lastMode = requestedMode;
    }

    const auto gainSignature = hashDeterministicGainParameters (parameters);
    const auto smartGainSignature = hashSmartGainParameters (parameters);
    const auto autoGainWasEnabled = lastAutoGainMode > 0;
    const auto autoGainIsEnabled = parameters.autoGainMode > 0;
    const auto requiresGainCalibration =
        gainSignature != lastGainSignature
        || (autoGainIsEnabled && ! autoGainWasEnabled);
    if (requiresGainCalibration)
    {
        lastGainSignature = gainSignature;
        normalGainTracking = parameters.autoGainMode == 1;
        normalGainTrackingSamples = 0;
        resetSmartAutoGain();
    }
    else if (parameters.autoGainMode == 2 && lastAutoGainMode != 2)
    {
        resetSmartAutoGain();
    }
    if (smartGainSignature != lastSmartGainSignature)
    {
        lastSmartGainSignature = smartGainSignature;
        if (parameters.autoGainMode == 2)
            resetSmartAutoGain();
    }
    lastAutoGainMode = parameters.autoGainMode;

    const auto parameterUpdateRate =
        sampleRate / static_cast<double> (juce::jmax (1, samples));
    smoothedDriveDb = smoothTowards (
        smoothedDriveDb, parameters.driveDb, parameterUpdateRate, 0.015);
    smoothedCharacter = smoothTowards (
        smoothedCharacter, parameters.character, parameterUpdateRate, 0.015);
    smoothedAsymmetry = smoothTowards (
        smoothedAsymmetry, parameters.asymmetry, parameterUpdateRate, 0.015);
    smoothedTone = smoothTowards (
        smoothedTone, parameters.tone, parameterUpdateRate, 0.025);
    smoothedMix = smoothTowards (
        smoothedMix, parameters.mix, parameterUpdateRate, 0.015);
    smoothedOutputDb = smoothTowards (
        smoothedOutputDb, parameters.outputDb, parameterUpdateRate, 0.015);

    Parameters smoothed = parameters;
    smoothed.driveDb = smoothedDriveDb;
    smoothed.character = smoothedCharacter;
    smoothed.asymmetry = smoothedAsymmetry;
    smoothed.tone = smoothedTone;
    smoothed.mix = smoothedMix;
    smoothed.outputDb = smoothedOutputDb;
    deterministicGainLinear = parameters.autoGainMode > 0
        ? lookupDeterministicGain (smoothed, sampleRate)
        : 1.0f;
    if (parameters.autoGainMode == 1
        && normalGainTracking
        && normalGainTrackingSamples == 0)
        normalGainLinear = deterministicGainLinear;

    updateToneFilters (smoothed.tone);
    processTonePre (buffer);

    int intrinsicWetLatency = 0;
    const auto mode = static_cast<Mode> (requestedMode);

    if (mode == Mode::spectralClip)
    {
        processSpectralBlock (buffer, smoothed);
        intrinsicWetLatency = fftSize;
    }
    else
    {
        const auto quality = juce::jlimit (0, 3, smoothed.quality);
        juce::dsp::AudioBlock<float> block (buffer);

        if (quality == 0 || ! usesOversampling (mode))
        {
            processNonlinearBlock (block, smoothed, sampleRate, sampleRate);
        }
        else
        {
            auto& oversampler = *oversamplers[static_cast<size_t> (quality - 1)];
            auto oversampled = oversampler.processSamplesUp (block);
            const auto factor = static_cast<double> (1 << quality);
            processNonlinearBlock (
                oversampled, smoothed, sampleRate * factor, sampleRate);
            oversampler.processSamplesDown (block);
            intrinsicWetLatency =
                oversamplingLatencies[static_cast<size_t> (quality - 1)];
        }
    }

    processTonePost (buffer);

    const auto wetDelay = juce::jmax (0, fixedLatencySamples - intrinsicWetLatency);
    const auto outputGain = juce::Decibels::decibelsToGain (smoothed.outputDb);
    const auto mix = juce::jlimit (0.0f, 1.0f, smoothed.mix);
    double dryEnergy = 0.0;
    double wetEnergy = 0.0;
    double wetPeak = 0.0;
    const auto smartMeasurementActive =
        parameters.autoGainMode == 2
        && ! smartGainLocked;
    const auto normalMeasurementActive =
        parameters.autoGainMode == 1
        && normalGainTracking;
    const auto levelMeasurementActive =
        smartMeasurementActive || normalMeasurementActive;

    for (int channel = 0; channel < channels; ++channel)
    {
        auto* wet = buffer.getWritePointer (channel);
        auto* dry = dryBuffer.getWritePointer (channel);

        for (int sample = 0; sample < samples; ++sample)
        {
            const auto alignedDry = delaySample (
                dry[sample], channel, fixedLatencySamples,
                dryDelayBuffers, dryDelayPositions);
            const auto alignedWet = delaySample (
                wet[sample], channel, wetDelay,
                wetDelayBuffers, wetDelayPositions);

            dry[sample] = alignedDry;
            wet[sample] = alignedWet;
            if (levelMeasurementActive)
            {
                dryEnergy += static_cast<double> (alignedDry) * alignedDry;
                wetEnergy += static_cast<double> (alignedWet) * alignedWet;
                wetPeak = juce::jmax (
                    wetPeak, std::abs (static_cast<double> (alignedWet)));
            }
        }
    }

    constexpr auto silenceEnergy = 1.0e-10;
    if (normalMeasurementActive)
    {
        normalGainTrackingSamples += samples;
        const auto hasAlignedAudio =
            normalGainTrackingSamples >= fixedLatencySamples + samples;
        if (hasAlignedAudio
            && dryEnergy > silenceEnergy
            && wetEnergy > silenceEnergy)
        {
            normalGainLinear = juce::jlimit (
                juce::Decibels::decibelsToGain (-72.0f),
                juce::Decibels::decibelsToGain (48.0f),
                static_cast<float> (std::sqrt (dryEnergy / wetEnergy)));
        }

        const auto parametersSettled =
            std::abs (smoothedDriveDb - parameters.driveDb) < 0.005f
            && std::abs (smoothedCharacter - parameters.character) < 0.0005f
            && std::abs (smoothedAsymmetry - parameters.asymmetry) < 0.0005f;
        if (hasAlignedAudio && parametersSettled)
            normalGainTracking = false;
    }

    if (smartMeasurementActive)
    {
        constexpr auto settleSeconds = 0.22;
        constexpr auto measurementSeconds = 0.55;
        const auto settleSampleCount = juce::roundToInt (
            sampleRate * settleSeconds);
        const auto measurementSampleCount = juce::roundToInt (
            sampleRate * measurementSeconds);
        const auto wasSettled = smartStableSamples >= settleSampleCount;
        smartStableSamples += samples;
        if (smartStableSamples < settleSampleCount)
        {
            smartProgress.store (
                0.45f * static_cast<float> (smartStableSamples)
                    / static_cast<float> (juce::jmax (1, settleSampleCount)),
                std::memory_order_relaxed);
        }
        else
        {
            smartProgress.store (
                0.45f + 0.55f * static_cast<float> (smartMeasuredSamples)
                    / static_cast<float> (
                        juce::jmax (1, measurementSampleCount)),
                std::memory_order_relaxed);
        }

        const auto validMeasurementBlock =
            wasSettled
            && dryEnergy > silenceEnergy
            && wetEnergy > silenceEnergy;

        // Keep both K-weighting filters warm during the settling interval.
        // Once settled, accumulate 400 ms BS.1770 gating blocks at 75%
        // overlap (one new block every 100 ms).
        const auto sliceLength = juce::jmax (
            1, juce::roundToInt (sampleRate * 0.1));
        for (int sample = 0; sample < samples; ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto drySample =
                    dryBuffer.getSample (channel, sample);
                const auto wetSample =
                    buffer.getSample (channel, sample);
                if (validMeasurementBlock)
                {
                    accumulateLoudnessSample (
                        drySample, wetSample, channel);
                }
                else
                {
                    const auto index = static_cast<size_t> (channel);
                    juce::ignoreUnused (
                        smartDryKWeighting[index].process (drySample),
                        smartWetKWeighting[index].process (wetSample));
                }
            }

            if (validMeasurementBlock)
            {
                ++smartSliceSamples;
                if (smartSliceSamples >= sliceLength)
                    finishLoudnessSlice();
            }
        }

        if (validMeasurementBlock)
        {
            smartWetPeak = juce::jmax (smartWetPeak, wetPeak);
            smartMeasuredSamples += samples;
            smartProgress.store (
                juce::jlimit (
                    0.45f,
                    1.0f,
                    0.45f + 0.55f
                        * static_cast<float> (smartMeasuredSamples)
                        / static_cast<float> (
                            juce::jmax (1, measurementSampleCount))),
                std::memory_order_relaxed);

            if (smartMeasuredSamples >= measurementSampleCount)
            {
                const auto dryLoudnessEnergy =
                    calculateGatedLoudnessEnergy (
                        smartDryLoudnessBlocks,
                        smartLoudnessBlockCount);
                const auto wetLoudnessEnergy =
                    calculateGatedLoudnessEnergy (
                        smartWetLoudnessBlocks,
                        smartLoudnessBlockCount);
                auto measured =
                    dryLoudnessEnergy > silenceEnergy
                            && wetLoudnessEnergy > silenceEnergy
                        ? static_cast<float> (std::sqrt (
                            dryLoudnessEnergy / wetLoudnessEnergy))
                        : deterministicGainLinear;
                measured = juce::jlimit (
                    juce::Decibels::decibelsToGain (-72.0f),
                    juce::Decibels::decibelsToGain (18.0f),
                    measured);
                if (smartWetPeak > 1.0e-6)
                    measured = juce::jmin (
                        measured,
                        static_cast<float> (0.98 / smartWetPeak));
                smartGainLinear = measured;
                smartGainLocked = true;
                smartProgress.store (1.0f, std::memory_order_relaxed);
                smartLockedForUi.store (true, std::memory_order_relaxed);
            }
        }
    }

    const auto targetAutoGain = parameters.autoGainMode <= 0
        ? 1.0f
        : (parameters.autoGainMode == 1
            ? normalGainLinear
            : (! smartGainLocked
                ? deterministicGainLinear
                : smartGainLinear));
    autoGainLinear = targetAutoGain;

    for (int channel = 0; channel < channels; ++channel)
    {
        auto* wet = buffer.getWritePointer (channel);
        const auto* dry = dryBuffer.getReadPointer (channel);
        for (int sample = 0; sample < samples; ++sample)
        {
            auto mixed = lerp (
                dry[sample], wet[sample] * autoGainLinear, mix)
                * outputGain;
            if (smoothed.outputDb <= 0.001f)
                mixed = juce::jlimit (-1.0f, 1.0f, mixed);
            wet[sample] = std::isfinite (mixed) ? mixed : 0.0f;
        }
    }
}

void DistortionEngine::updateToneFilters (float toneAmount)
{
    const auto tone = juce::jlimit (-1.0f, 1.0f, toneAmount);
    toneFiltersBypassed = std::abs (tone) < 1.0e-6f;
    if (std::isfinite (lastToneCoefficientAmount)
        && std::abs (tone - lastToneCoefficientAmount) < 0.0005f)
        return;
    lastToneCoefficientAmount = tone;

    const auto lowDb = -6.0f * tone;
    const auto highDb = 6.0f * tone;
    constexpr auto pivot = 1000.0;
    constexpr auto q = 0.70710678f;

    for (int channel = 0; channel < preparedChannels; ++channel)
    {
        auto& filters = toneFilters[static_cast<size_t> (channel)];
        filters.preLow.setCoefficients (
            juce::IIRCoefficients::makeLowShelf (
                sampleRate, pivot, q, juce::Decibels::decibelsToGain (lowDb)));
        filters.preHigh.setCoefficients (
            juce::IIRCoefficients::makeHighShelf (
                sampleRate, pivot, q, juce::Decibels::decibelsToGain (highDb)));
        filters.postLow.setCoefficients (
            juce::IIRCoefficients::makeLowShelf (
                sampleRate, pivot, q, juce::Decibels::decibelsToGain (-lowDb)));
        filters.postHigh.setCoefficients (
            juce::IIRCoefficients::makeHighShelf (
                sampleRate, pivot, q, juce::Decibels::decibelsToGain (-highDb)));
    }
}

void DistortionEngine::processTonePre (juce::AudioBuffer<float>& buffer)
{
    if (toneFiltersBypassed)
        return;

    for (int channel = 0; channel < juce::jmin (preparedChannels, buffer.getNumChannels());
         ++channel)
    {
        auto* data = buffer.getWritePointer (channel);
        auto& filters = toneFilters[static_cast<size_t> (channel)];
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            data[sample] = filters.preHigh.processSingleSampleRaw (
                filters.preLow.processSingleSampleRaw (data[sample]));
    }
}

void DistortionEngine::processTonePost (juce::AudioBuffer<float>& buffer)
{
    if (toneFiltersBypassed)
        return;

    for (int channel = 0; channel < juce::jmin (preparedChannels, buffer.getNumChannels());
         ++channel)
    {
        auto* data = buffer.getWritePointer (channel);
        auto& filters = toneFilters[static_cast<size_t> (channel)];
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            data[sample] = filters.postHigh.processSingleSampleRaw (
                filters.postLow.processSingleSampleRaw (data[sample]));
    }
}

float DistortionEngine::processCascadeSample (
    float input,
    const ModeContext& context,
    float stageGain,
    float stageDepth,
    int stages,
    std::array<StageState, maximumStages>& states)
{
    auto value = input;
    const auto mode = context.mode;
    const auto legacyDrivePath = usesLegacyDrivePath (mode);
    const auto algorithmDrive = usesDriveAsAlgorithmParameter (mode);
    const auto outputScale = wetOutputScale (mode, stageGain);
    for (int stage = 0; stage < stages; ++stage)
    {
        auto& state = states[static_cast<size_t> (stage)];
        if (legacyDrivePath)
        {
            value = processModeSample (
                value * stageGain,
                context,
                state);
            value = finishDirectStage (value, mode, stageGain);
            continue;
        }

        const auto dry = value;
        const auto shaped = processModeSample (
            algorithmDrive
                ? dry
                : dry
                    * (mode == Mode::signSquare
                        ? 1.0f
                        : (mode == Mode::deltaCrusher
                            ? context.coefficients[1]
                            : stageGain)),
            context,
            state);
        value = algorithmDrive
            ? shaped
            : lerp (
                dry,
                shaped * outputScale,
                stageDepth);
    }

    return std::isfinite (value) ? value : 0.0f;
}

void DistortionEngine::processNonlinearBlock (juce::dsp::AudioBlock<float> block,
                                              const Parameters& parameters,
                                              double processingSampleRate,
                                              double hostSampleRate)
{
    const auto mode = static_cast<Mode> (
        juce::jlimit (0, modeCount - 1, parameters.mode));
    const auto stages = juce::jlimit (1, maximumStages, parameters.stages);
    const auto driveNormalised = juce::jlimit (
        0.0f, 1.0f, parameters.driveDb / 36.0f);
    const auto stageDriveDb = parameters.driveDb;
    const auto stageGain = juce::Decibels::decibelsToGain (stageDriveDb);
    const auto stageDepth = driveDepth (parameters.driveDb);
    const auto dcCoefficient = static_cast<float> (std::exp (
        -2.0 * juce::MathConstants<double>::pi * 5.0
        / juce::jmax (1.0, processingSampleRate)));
    const auto dcMixCoefficient = smoothingCoefficient (
        processingSampleRate, 0.005);
    for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto channelAsymmetry =
            parameters.asymmetryStereo && channel == 1
                ? -parameters.asymmetry
                : parameters.asymmetry;
        const auto dcMix = dcBlockingAmount (
            mode, parameters.character, channelAsymmetry);
        const auto modeContext = makeModeContext (
            mode,
            parameters.character,
            channelAsymmetry,
            driveNormalised,
            processingSampleRate,
            hostSampleRate);
        auto* data = block.getChannelPointer (channel);

        for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto value = processCascadeSample (
                data[sample],
                modeContext,
                stageGain,
                stageDepth,
                stages,
                stageStates[channel]);

            const auto previousInput = dcPreviousInput[channel];
            const auto previousOutput = dcPreviousOutput[channel];
            const auto filtered =
                value - previousInput + dcCoefficient * previousOutput;
            dcPreviousInput[channel] = value;
            dcPreviousOutput[channel] = filtered;
            dcMixState[channel] =
                dcMix + dcMixCoefficient * (dcMixState[channel] - dcMix);
            const auto dcBlocked = lerp (
                value, filtered, dcMixState[channel]);
            const auto safeOutput = mode == Mode::schmittHysteresis
                ? juce::jlimit (-0.98f, 0.98f, dcBlocked)
                : dcBlocked;
            data[sample] = std::isfinite (safeOutput) ? safeOutput : 0.0f;
        }
    }
}

DistortionEngine::ModeContext DistortionEngine::makeModeContext (
    Mode mode,
    float character,
    float asymmetry,
    float driveNormalised,
    double processingSampleRate,
    double hostSampleRate)
{
    ModeContext context;
    context.mode = mode;
    context.character = juce::jlimit (-1.0f, 1.0f, character);
    context.character01 = unipolarCharacter (context.character);
    context.asymmetry = juce::jlimit (-1.0f, 1.0f, asymmetry);
    context.driveNormalised =
        juce::jlimit (0.0f, 1.0f, driveNormalised);
    context.processingSampleRate = processingSampleRate;
    context.hostSampleRate = hostSampleRate;

    const auto c = context.character;
    const auto c01 = context.character01;
    const auto drive = context.driveNormalised;
    auto& p = context.coefficients;
    auto& integer = context.integers;

    switch (mode)
    {
        case Mode::morphSoftClip:
        case Mode::hardClip:
        case Mode::diodeClipper:
        case Mode::fullWaveRectifier:
        case Mode::spectralClip:
            break;

        case Mode::signSquare:
            // Threshold and edge hardness are deliberately independent:
            // Character moves a deliberately bounded switching point while
            // Drive sharpens it. The range narrows at high Drive so Threshold
            // cannot push ordinary signals into a flat, DC-only state.
            p[0] = lerp (0.24f, 0.12f, smoothStep01 (drive)) * c;
            p[1] = lerp (0.85f, 32.0f, drive);
            break;

        case Mode::zeroSquare:
            p[0] = 0.62f * c01;
            p[1] = juce::jmax (0.08f, 1.0f - p[0]);
            p[2] = lerp (1.2f, 4.0f, c01);
            break;

        case Mode::softFullWaveRectifier:
            p[0] = lerp (0.85f, 0.008f, c01);
            break;

        case Mode::halfWaveRectifier:
            p[0] = lerp (0.42f, 0.0f, smoothStep01 (c01));
            p[1] = lerp (0.10f, 0.002f, c01);
            p[2] = 0.55f * std::pow (1.0f - c01, 1.6f);
            p[3] = smoothStep01 (c01);
            break;

        case Mode::topologyFold:
            integer[0] = c01 < 0.25f ? 0 : (c01 > 0.75f ? 2 : 1);
            break;

        case Mode::recursiveFoldback:
            p[0] = lerp (0.28f, 1.0f, c01);
            break;

        case Mode::sineFold:
            p[0] = lerp (1.8f, 0.42f, c01);
            break;

        case Mode::chebyshevFold:
        {
            const auto orderValue = 2.0f + 6.0f * c01;
            integer[0] = juce::jlimit (
                2, 8, static_cast<int> (std::floor (orderValue)));
            integer[1] = juce::jmin (8, integer[0] + 1);
            p[0] = orderValue - static_cast<float> (integer[0]);
            p[1] = lerp (0.35f, 1.0f, c01);
            break;
        }

        case Mode::moduloWrap:
            p[0] = 0.25f + 1.75f * (1.0f - c01);
            break;

        case Mode::harmonicMorph:
            p[0] = bipolarTo01 (c);
            break;

        case Mode::bitCrusher:
        {
            integer[0] = juce::jlimit (
                1, 24,
                24 - static_cast<int> (std::round (23.0f * drive)));
            p[0] = static_cast<float> (
                (static_cast<std::uint32_t> (1) << integer[0]) - 1u);
            p[1] = 0.495f * c01 * c01 * c01;
            break;
        }

        case Mode::classBSaturation:
            p[0] = lerp (0.008f, 0.78f, c01 * c01);
            p[1] = lerp (0.09f, 0.004f, c01);
            p[2] = drive * drive * (3.0f - 2.0f * drive);
            break;

        case Mode::bitRotation:
            integer[0] = juce::jlimit (
                0, 15, static_cast<int> (std::round (15.0f * c01)));
            break;

        case Mode::downsample:
        {
            p[0] = downsampleTargetRate (drive, hostSampleRate);
            p[1] = p[0]
                / static_cast<float> (juce::jmax (1.0, processingSampleRate));
            const auto maximumGlideSeconds = juce::jmin (
                0.25,
                0.5 / juce::jmax (1.0, static_cast<double> (p[0])));
            const auto glideSeconds = juce::jmax (
                1.0 / juce::jmax (1.0, processingSampleRate),
                maximumGlideSeconds * static_cast<double> (c01 * c01));
            p[2] = smoothingCoefficient (
                processingSampleRate, glideSeconds);
            break;
        }

        case Mode::deltaCrusher:
            p[0] = std::pow (
                10.0f,
                lerp (-4.0f, -0.125f, std::pow (c01, 0.25f)));
            p[1] = lerp (1.0f, 4.0f, smoothStep01 (drive));
            break;

        case Mode::triodeStage:
            p[0] = 0.58f * c + 0.18f * context.asymmetry;
            p[1] = std::tanh (
                1.08f * p[0]
                + 0.34f * p[0] * p[0]
                - 0.055f * p[0] * p[0] * p[0]);
            p[2] = juce::jmax (0.1f, 1.0f - std::abs (p[1]));
            break;

        case Mode::transistorFet:
            p[0] = -0.12f + 1.18f * c;
            p[1] = juce::jmax (0.0f, p[0]);
            p[1] *= p[1];
            p[2] = 1.0f / (1.0f + 0.55f * std::abs (c));
            break;

        case Mode::tapeHysteresis:
            p[0] = juce::jlimit (0.0f, 1.0f, 0.08f + 0.92f * drive);
            break;

        case Mode::transformerCore:
        {
            const auto curve = smoothStep01 (c01);
            p[0] = smoothingCoefficient (
                processingSampleRate, lerp (0.00035f, 0.025f, curve));
            p[1] = lerp (0.05f, 0.65f, curve);
            p[2] = lerp (1.0f, 2.8f, curve);
            p[3] = 1.0f / std::tanh (p[2] * (1.0f + p[1]));
            p[4] = curve;
            p[5] = lerp (1.2f, 2.0f, curve);
            break;
        }

        case Mode::slewLimiter:
        {
            const auto maximumDelta = std::pow (
                10.0f, lerp (-4.5f, -0.35f, 1.0f - c01));
            p[0] = maximumDelta * (1.0f + 0.75f * context.asymmetry);
            p[1] = maximumDelta * (1.0f - 0.75f * context.asymmetry);
            break;
        }

        case Mode::schmittHysteresis:
        {
            const auto width = 0.005f + 1.30f * std::pow (c01, 1.35f);
            p[0] = width * (1.0f - 0.45f * context.asymmetry);
            p[1] = -width * (1.0f + 0.45f * context.asymmetry);
            break;
        }

        case Mode::dynamicSag:
            p[0] = smoothingCoefficient (processingSampleRate, 0.003);
            p[1] = smoothingCoefficient (
                processingSampleRate, 0.04 + 0.8 * c01);
            p[2] = 0.5f + 3.5f * c01;
            break;

        case Mode::feedbackSaturator:
            p[0] = 0.92f * c;
            break;

        case Mode::resonantFeedbackClip:
        {
            const auto cutoff = 90.0f * std::pow (90.0f, c01);
            p[0] = juce::jlimit (
                0.001f, 0.72f,
                2.0f * std::sin (
                    pi * static_cast<float> (
                        cutoff / processingSampleRate)));
            const auto curve = std::pow (c01, 1.45f);
            p[1] = lerp (1.45f, 0.045f, curve);
            p[2] = lerp (0.8f, 5.0f, curve);
            p[3] = lerp (0.25f, 2.9f, curve);
            break;
        }

        case Mode::phaseDistortion:
        {
            const auto nyquistSafe = static_cast<float> (
                juce::jmax (40.0, 0.45 * processingSampleRate));
            const auto toneHz =
                40.0f * std::pow (nyquistSafe / 40.0f, c01);
            p[0] = c01 >= 0.999f
                ? 0.0f
                : static_cast<float> (std::exp (
                    -2.0 * juce::MathConstants<double>::pi * toneHz
                    / processingSampleRate));
            p[1] = 50.0f * drive;
            p[2] = 0.5f * context.asymmetry;
            break;
        }

        default:
            break;
    }

    return context;
}

float DistortionEngine::processModeSample (float input,
                                           const ModeContext& context,
                                           StageState& state)
{
    const auto mode = context.mode;
    const auto c01 = context.character01;
    const auto asym = context.asymmetry;
    const auto processingSampleRate = context.processingSampleRate;
    const auto& p = context.coefficients;
    const auto& integer = context.integers;
    auto x = applyAsymmetry (input, asym);
    switch (mode)
    {
        case Mode::morphSoftClip:
        {
            return lerp (
                vitalSoftClip (x, 1.0f),
                cubicSoftClip (x),
                c01);
        }

        case Mode::hardClip:
        {
            return clampBipolar (lerp (
                vitalHardClip (x, 1.0f),
                vitalSoftClip (x, 1.0f),
                c01));
        }

        case Mode::signSquare:
        {
            return std::tanh (p[1] * (x - p[0]));
        }

        case Mode::zeroSquare:
        {
            const auto magnitude = std::abs (x);
            const auto aboveZero = juce::jmax (0.0f, magnitude - p[0]);
            const auto scaled = aboveZero / p[1];
            const auto squared = 1.0f - std::exp (
                -p[2] * scaled * scaled);
            return std::copysign (squared, x);
        }

        case Mode::fullWaveRectifier:
            return lerp (x, std::abs (x), c01);

        case Mode::softFullWaveRectifier:
        {
            return x * std::tanh (x / p[0]);
        }

        case Mode::halfWaveRectifier:
        {
            const auto relative = x - p[0];
            const auto junction = 0.5f
                * (relative
                   + std::sqrt (relative * relative + p[1] * p[1]));
            const auto zeroRelative = -p[0];
            const auto zeroOffset = 0.5f
                * (zeroRelative
                   + std::sqrt (
                       zeroRelative * zeroRelative + p[1] * p[1]));
            const auto forward = juce::jmax (0.0f, junction - zeroOffset);
            const auto leaky = forward + p[2] * juce::jmin (0.0f, x);
            const auto primitive = x > 0.0f ? 0.5f * x * x : 0.0f;
            auto precise = juce::jmax (0.0f, x);
            if (state.counter > 0
                && std::abs (x - state.previousInput) > 1.0e-5f)
                precise = (primitive - state.previousOutput)
                    / (x - state.previousInput);
            state.previousInput = x;
            state.previousOutput = primitive;
            state.counter = 1;
            return lerp (leaky, precise, p[3]);
        }

        case Mode::topologyFold:
        {
            const auto topology = integer[0];
            const auto primitive = topologyFoldPrimitive (x, topology);
            auto folded = topologyFoldDirect (x, topology);
            if (state.counter > 0
                && std::abs (x - state.previousInput) > 1.0e-5f)
                folded = (primitive - state.previousOutput)
                    / (x - state.previousInput);
            state.previousInput = x;
            state.previousOutput = primitive;
            state.counter = 1;
            return folded;
        }

        case Mode::recursiveFoldback:
        {
            auto value = x;
            constexpr auto threshold = 1.0f;
            for (int iteration = 0; iteration < 12; ++iteration)
            {
                if (value > threshold)
                    value = threshold - p[0] * (value - threshold);
                else if (value < -threshold)
                    value = -threshold - p[0] * (value + threshold);
                else
                    break;
            }
            return clampBipolar (value);
        }

        case Mode::sineFold:
        {
            const auto sine = std::sin (pi * 0.5f * x);
            return std::copysign (
                std::pow (juce::jmin (1.0f, std::abs (sine)), p[0]),
                sine);
        }

        case Mode::chebyshevFold:
        {
            const auto bounded = std::tanh (x);
            const auto harmonic = lerp (
                chebyshev (bounded, integer[0]),
                chebyshev (bounded, integer[1]),
                p[0]);
            return lerp (
                bounded,
                harmonic,
                p[1]);
        }

        case Mode::moduloWrap:
        {
            return wrapBipolar (x, p[0]);
        }

        case Mode::harmonicMorph:
        {
            const auto bounded = std::tanh (x);
            const auto second = chebyshev (bounded, 2);
            const auto third = chebyshev (bounded, 3);
            return lerp (second, third, p[0]);
        }

        case Mode::bitCrusher:
        {
            const auto unipolar = 0.5f * (clampBipolar (x) + 1.0f);
            const auto scaled = unipolar * p[0];
            if (scaled >= p[0])
                return 1.0f;

            const auto lowerCode = std::floor (scaled);
            const auto fraction = scaled - lowerCode;
            const auto transitionHalfWidth = p[1];
            float transition = fraction >= 0.5f ? 1.0f : 0.0f;
            if (transitionHalfWidth > 1.0e-6f)
            {
                const auto start = 0.5f - transitionHalfWidth;
                const auto end = 0.5f + transitionHalfWidth;
                const auto position = juce::jlimit (
                    0.0f, 1.0f, (fraction - start) / (end - start));
                transition = position * position * (3.0f - 2.0f * position);
            }
            const auto softenedCode = lowerCode + transition;
            return 2.0f * softenedCode / p[0] - 1.0f;
        }

        case Mode::classBSaturation:
        {
            const auto magnitude = std::abs (x);
            const auto relative = magnitude - p[0];
            const auto conducted = 0.5f
                * (relative
                   + std::sqrt (relative * relative + p[1] * p[1]));
            const auto crossover = std::copysign (
                conducted * (1.0f + 0.65f * p[0]),
                x);
            return lerp (x, crossover, p[2]);
        }

        case Mode::bitRotation:
        {
            const auto bounded = clampBipolar (x);
            const auto quantised = static_cast<std::int16_t> (
                std::round (bounded * 32767.0f));
            const auto unsignedValue = std::bit_cast<std::uint16_t> (quantised);
            const auto rotated = std::rotl (
                unsignedValue, integer[0] % 16);
            return static_cast<float> (std::bit_cast<std::int16_t> (rotated))
                / 32768.0f;
        }

        case Mode::downsample:
        {
            if (state.counter == 0 || state.phase >= 1.0)
            {
                state.heldSample = x;
                state.phase -= std::floor (state.phase);
                state.counter = 1;
            }
            state.phase += p[1];
            if (c01 <= 0.0001f)
            {
                state.previousOutput = state.heldSample;
                return state.heldSample;
            }

            state.previousOutput = state.heldSample
                + p[2] * (state.previousOutput - state.heldSample);
            return state.previousOutput;
        }

        case Mode::deltaCrusher:
        {
            const auto predictionError = x - state.memory;
            state.secondary += predictionError;
            const auto requestedDelta =
                std::round (state.secondary / p[0]) * p[0];
            const auto nextMemory = juce::jlimit (
                -p[1], p[1], state.memory + requestedDelta);
            const auto appliedDelta = nextMemory - state.memory;
            state.memory = nextMemory;
            state.secondary = juce::jlimit (
                -2.0f * p[0],
                2.0f * p[0],
                state.secondary - appliedDelta);
            state.previousInput = x;
            return state.memory / p[1];
        }

        case Mode::diodeClipper:
        {
            const auto softFeedback = std::atan (1.8f * x)
                / std::atan (1.8f);
            const auto diodeGround = std::copysign (
                1.0f - std::exp (-2.8f * std::abs (x)), x);
            return lerp (softFeedback, diodeGround, c01);
        }

        case Mode::triodeStage:
        {
            const auto grid = x + p[0];
            const auto curved = 1.08f * grid
                + 0.34f * grid * grid
                - 0.055f * grid * grid * grid;
            return clampBipolar ((std::tanh (curved) - p[1]) / p[2]);
        }

        case Mode::transistorFet:
        {
            const auto gate = juce::jmax (0.0f, x + p[0]);
            const auto squareLaw = gate * gate;
            const auto centred = squareLaw - p[1];
            return std::tanh (2.2f * p[2] * centred);
        }

        case Mode::tapeHysteresis:
            return chowtape::processSample (
                x,
                p[0],
                c01,
                processingSampleRate,
                state.tape);

        case Mode::transformerCore:
        {
            state.memory = x + p[0] * (state.memory - x);
            const auto cleanCore =
                std::tanh (x) / std::tanh (1.0f);
            const auto magnetised =
                std::tanh (p[2] * (x + p[1] * state.memory)) * p[3];
            const auto remanence =
                std::tanh (p[5] * state.memory) / std::tanh (p[5]);
            const auto remanenceMix = 1.2f * p[4];
            constexpr auto coreSoftness = 1.1f;
            const auto magneticCore =
                std::tanh (
                    coreSoftness
                    * (magnetised + remanenceMix * remanence))
                / std::tanh (
                    coreSoftness * (1.0f + remanenceMix));
            return lerp (cleanCore, magneticCore, p[4]);
        }

        case Mode::slewLimiter:
        {
            const auto delta = x - state.previousOutput;
            state.previousOutput += juce::jlimit (-p[1], p[0], delta);
            return state.previousOutput;
        }

        case Mode::schmittHysteresis:
        {
            if (! state.gateHigh && x >= p[0])
                state.gateHigh = true;
            else if (state.gateHigh && x <= p[1])
                state.gateHigh = false;
            return state.gateHigh ? 0.98f : -0.98f;
        }

        case Mode::dynamicSag:
        {
            const auto absInput = std::abs (x);
            const auto coefficient =
                absInput > state.envelope ? p[0] : p[1];
            state.envelope =
                absInput + coefficient * (state.envelope - absInput);
            const auto sag = 1.0f / (1.0f + p[2] * state.envelope);
            return std::tanh (x * sag);
        }

        case Mode::feedbackSaturator:
        {
            const auto output = std::tanh (x + p[0] * state.previousOutput);
            state.previousOutput = output;
            return output;
        }

        case Mode::resonantFeedbackClip:
        {
            const auto high = std::tanh (
                p[2] * (x - state.memory - p[1] * state.secondary));
            state.secondary += p[0] * high;
            state.memory += p[0] * state.secondary;
            state.memory = juce::jlimit (-2.5f, 2.5f, state.memory);
            state.secondary = juce::jlimit (-2.5f, 2.5f, state.secondary);
            return std::tanh (
                0.35f * x
                + p[3]
                    * (0.45f * state.memory + 1.35f * state.secondary));
        }

        case Mode::phaseDistortion:
        {
            const auto delaySize =
                static_cast<int> (state.phaseDelay.size());
            if (delaySize < 2)
                return x;

            state.envelope =
                (1.0f - p[0]) * x + p[0] * state.envelope;

            const auto modulator = juce::jlimit (
                0.0f, 1.0f, state.envelope + p[2]);
            const auto delaySamples = juce::jlimit (
                0.0f,
                static_cast<float> (delaySize - 2),
                0.001f * (p[1] * modulator)
                    * static_cast<float> (processingSampleRate));

            const auto write = state.phaseWritePosition;
            state.phaseDelay[static_cast<size_t> (write)] = x;
            auto readPosition = static_cast<float> (write) - delaySamples;
            while (readPosition < 0.0f)
                readPosition += static_cast<float> (delaySize);
            const auto first = static_cast<int> (std::floor (readPosition))
                % delaySize;
            const auto second = (first + 1) % delaySize;
            const auto fraction = readPosition
                - static_cast<float> (first);
            const auto delayed = lerp (
                state.phaseDelay[static_cast<size_t> (first)],
                state.phaseDelay[static_cast<size_t> (second)],
                fraction);
            state.phaseWritePosition =
                (write + 1) % delaySize;

            constexpr auto silenceThreshold = 3.16227766e-5f; // -90 dBFS
            const auto silenceHold = juce::jmax (
                8, juce::roundToInt (0.001 * processingSampleRate));
            if (std::abs (x) <= silenceThreshold)
                ++state.silenceSamples;
            else
            {
                state.silenceSamples = 0;
                state.tailGain = 1.0f;
            }

            if (state.silenceSamples >= silenceHold)
            {
                const auto releaseStep = static_cast<float> (
                    1.0 / juce::jmax (1.0, 0.006 * processingSampleRate));
                state.tailGain = juce::jmax (
                    0.0f, state.tailGain - releaseStep);
            }

            return delayed * state.tailGain;
        }

        case Mode::spectralClip:
            break;
    }

    return x;
}

void DistortionEngine::processSpectralBlock (juce::AudioBuffer<float>& buffer,
                                             const Parameters& parameters)
{
    for (int channel = 0; channel < juce::jmin (preparedChannels, buffer.getNumChannels());
         ++channel)
    {
        auto channelParameters = parameters;
        if (parameters.asymmetryStereo && channel == 1)
            channelParameters.asymmetry = -parameters.asymmetry;
        auto* data = buffer.getWritePointer (channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            data[sample] = processSpectralSample (
                data[sample], channel, channelParameters);
    }
}

float DistortionEngine::processSpectralSample (float input,
                                               int channel,
                                               const Parameters& parameters)
{
    return processSpectralSampleCore (
        input,
        spectralStates[static_cast<size_t> (channel)],
        parameters,
        fft,
        spectralWindow);
}

float DistortionEngine::processSpectralSampleCore (
    float input,
    SpectralState& state,
    const Parameters& parameters,
    juce::dsp::FFT& fftToUse,
    const std::array<float, fftSize>& window)
{
    const auto position = state.position;
    state.input[static_cast<size_t> (position)] = input;
    const auto output = state.output[static_cast<size_t> (position)];
    state.output[static_cast<size_t> (position)] = 0.0f;

    state.position = (position + 1) % fftSize;
    if (++state.hopCounter >= fftHop)
    {
        state.hopCounter = 0;
        processSpectralFrameCore (
            state, parameters, fftToUse, window);
    }

    return std::isfinite (output) ? output : 0.0f;
}

void DistortionEngine::processSpectralFrameCore (
    SpectralState& state,
    const Parameters& parameters,
    juce::dsp::FFT& fftToUse,
    const std::array<float, fftSize>& window)
{
    state.fftData.fill (0.0f);

    for (int i = 0; i < fftSize; ++i)
    {
        const auto source = (state.position + i) % fftSize;
        state.fftData[static_cast<size_t> (i)] =
            state.input[static_cast<size_t> (source)]
            * window[static_cast<size_t> (i)];
    }

    fftToUse.performRealOnlyForwardTransform (state.fftData.data());

    const auto c01 = unipolarCharacter (parameters.character);
    const auto spectralDepth = driveDepth (parameters.driveDb);
    const auto threshold = static_cast<float> (fftSize) * 0.42f
        * juce::Decibels::decibelsToGain (
            -parameters.driveDb * lerp (0.35f, 1.0f, c01));
    const auto stages = juce::jlimit (1, maximumStages, parameters.stages);

    for (int bin = 0; bin <= fftSize / 2; ++bin)
    {
        const auto realIndex = bin == 0 ? 0 : bin * 2;
        const auto imaginaryIndex = bin == 0 ? 1 : bin * 2 + 1;
        const auto real = state.fftData[static_cast<size_t> (realIndex)];
        const auto imaginary = state.fftData[static_cast<size_t> (imaginaryIndex)];
        const auto magnitude = std::sqrt (real * real + imaginary * imaginary);
        // sin (atan2 (imaginary, real)) is exactly imaginary / magnitude.
        // Avoiding atan2+sin here preserves the transfer while removing two
        // transcendental calls from every processed FFT bin.
        const auto phaseSine =
            magnitude > 1.0e-9f ? imaginary / magnitude : 0.0f;
        auto phaseThreshold = threshold
            * (1.0f + 0.28f * parameters.asymmetry * phaseSine);
        phaseThreshold = juce::jmax (1.0e-5f, phaseThreshold);

        const auto clippedMagnitude = shapeSpectralMagnitude (
            magnitude, phaseThreshold, c01, stages);
        const auto shapedMagnitude = lerp (
            magnitude, clippedMagnitude, spectralDepth);

        const auto scale = magnitude > 1.0e-9f ? shapedMagnitude / magnitude : 0.0f;
        state.fftData[static_cast<size_t> (realIndex)] = real * scale;
        state.fftData[static_cast<size_t> (imaginaryIndex)] = imaginary * scale;
    }

    fftToUse.performRealOnlyInverseTransform (state.fftData.data());

    constexpr auto overlapNormalisation = 2.0f / 3.0f;
    for (int i = 0; i < fftSize; ++i)
    {
        const auto destination = (state.position + i) % fftSize;
        state.output[static_cast<size_t> (destination)] +=
            state.fftData[static_cast<size_t> (i)]
            * window[static_cast<size_t> (i)]
            * overlapNormalisation;
    }
}

float DistortionEngine::delaySample (
    float input,
    int channel,
    int delaySamples,
    std::array<std::vector<float>, maximumChannels>& delayBuffers,
    std::array<int, maximumChannels>& positions) noexcept
{
    auto& delayBuffer = delayBuffers[static_cast<size_t> (channel)];
    if (delayBuffer.empty())
        return input;

    auto& writePosition = positions[static_cast<size_t> (channel)];
    delayBuffer[static_cast<size_t> (writePosition)] = input;

    const auto capacity = static_cast<int> (delayBuffer.size());
    auto readPosition = writePosition - juce::jlimit (0, capacity - 1, delaySamples);
    if (readPosition < 0)
        readPosition += capacity;
    const auto output = delayBuffer[static_cast<size_t> (readPosition)];

    writePosition = (writePosition + 1) % capacity;
    return output;
}

float DistortionEngine::applyAsymmetry (float input, float asymmetry) noexcept
{
    const auto signedAmount = input >= 0.0f ? asymmetry : -asymmetry;
    return input * (1.0f + 0.72f * signedAmount);
}

float DistortionEngine::cubicSoftClip (float input) noexcept
{
    if (input >= 1.0f)
        return 1.0f;
    if (input <= -1.0f)
        return -1.0f;
    return 1.5f * (input - input * input * input / 3.0f);
}

float DistortionEngine::quinticSoftClip (float input) noexcept
{
    if (input >= 1.0f)
        return 1.0f;
    if (input <= -1.0f)
        return -1.0f;

    const auto squared = input * input;
    return (input - 0.5f * input * squared + 0.1f * input * squared * squared)
        / 0.6f;
}

float DistortionEngine::shapeSpectralMagnitude (float magnitude,
                                                float threshold,
                                                float character,
                                                int stages) noexcept
{
    const auto knee = unipolarCharacter (character);
    auto shaped = juce::jmax (0.0f, magnitude);
    const auto safeThreshold = juce::jmax (1.0e-6f, threshold);
    for (int remaining = juce::jlimit (1, maximumStages, stages);
         remaining > 0;
         --remaining)
    {
        const auto hard = juce::jmin (shaped, safeThreshold);
        const auto soft = safeThreshold * std::tanh (shaped / safeThreshold);
        shaped = lerp (soft, hard, knee);
    }
    return shaped;
}

float DistortionEngine::downsampleTargetRate (float amountNormalised,
                                              double hostSampleRate) noexcept
{
    const auto amount = unipolarCharacter (amountNormalised);
    const auto safeRate = static_cast<float> (juce::jmax (1.0, hostSampleRate));
    const auto target = safeRate * std::pow (1.0f / safeRate, amount);
    return juce::jmax (1.0f, target);
}

bool DistortionEngine::usesLegacyDrivePath (Mode mode) noexcept
{
    return mode == Mode::morphSoftClip
        || mode == Mode::softFullWaveRectifier
        || mode == Mode::slewLimiter;
}

bool DistortionEngine::usesDriveAsAlgorithmParameter (Mode mode) noexcept
{
    return mode == Mode::bitCrusher
        || mode == Mode::classBSaturation
        || mode == Mode::downsample
        || mode == Mode::phaseDistortion;
}

bool DistortionEngine::usesOversampling (Mode mode) noexcept
{
    return mode != Mode::bitCrusher
        && mode != Mode::bitRotation
        && mode != Mode::downsample
        && mode != Mode::deltaCrusher
        && mode != Mode::phaseDistortion
        && mode != Mode::spectralClip;
}

float DistortionEngine::lookupDeterministicGain (
    const Parameters& parameters,
    double displaySampleRate) noexcept
{
    const auto selectedMode =
        juce::jlimit (0, modeCount - 1, parameters.mode);
    if (selectedMode == static_cast<int> (Mode::spectralClip))
    {
        using namespace spectral_auto_gain_table;
        const auto stage = static_cast<size_t> (
            juce::jlimit (1, maximumStages, parameters.stages) - 1);
        const auto drivePosition = tablePosition (
            drives, juce::jlimit (0.0f, 36.0f, parameters.driveDb));
        const auto characterPosition = tablePosition (
            characters, juce::jlimit (0.0f, 1.0f, parameters.character));
        const auto valueAt = [&] (size_t character, size_t drive)
        {
            return gains[
                (stage * characters.size() + character) * drives.size()
                    + drive];
        };
        const auto interpolateAtCharacter = [&] (size_t character)
        {
            return lerp (
                valueAt (character, drivePosition.lower),
                valueAt (character, drivePosition.upper),
                drivePosition.fraction);
        };
        return lerp (
            interpolateAtCharacter (characterPosition.lower),
            interpolateAtCharacter (characterPosition.upper),
            characterPosition.fraction);
    }

    using namespace auto_gain_table;

    constexpr auto rateCount = sampleRates.size();
    constexpr auto stageCount = static_cast<size_t> (maximumStages);
    constexpr auto asymmetryCount = asymmetries.size();
    constexpr auto characterCount = characters.size();
    constexpr auto driveCount = drives.size();

    const auto mode = static_cast<size_t> (
        selectedMode);
    const auto stage = static_cast<size_t> (
        juce::jlimit (1, maximumStages, parameters.stages) - 1);
    const auto ratePosition = tablePosition (
        sampleRates, juce::jmax (1.0, displaySampleRate));
    const auto drivePosition = tablePosition (
        drives, juce::jlimit (0.0f, 36.0f, parameters.driveDb));
    const auto characterPosition = tablePosition (
        characters, juce::jlimit (-1.0f, 1.0f, parameters.character));
    const auto asymmetryPosition = tablePosition (
        asymmetries, juce::jlimit (-1.0f, 1.0f, parameters.asymmetry));

    const auto valueAt = [&] (size_t rate,
                              size_t asymmetry,
                              size_t character,
                              size_t drive)
    {
        const auto index =
            (((((mode * rateCount + rate) * stageCount + stage)
                * asymmetryCount + asymmetry)
               * characterCount + character)
              * driveCount + drive);
        return gains[index];
    };

    const auto interpolateAtRate = [&] (size_t rate)
    {
        const auto interpolateAtAsymmetry = [&] (size_t asymmetry)
        {
            const auto interpolateAtCharacter = [&] (size_t character)
            {
                return lerp (
                    valueAt (
                        rate,
                        asymmetry,
                        character,
                        drivePosition.lower),
                    valueAt (
                        rate,
                        asymmetry,
                        character,
                        drivePosition.upper),
                    drivePosition.fraction);
            };
            return lerp (
                interpolateAtCharacter (characterPosition.lower),
                interpolateAtCharacter (characterPosition.upper),
                characterPosition.fraction);
        };
        return lerp (
            interpolateAtAsymmetry (asymmetryPosition.lower),
            interpolateAtAsymmetry (asymmetryPosition.upper),
            asymmetryPosition.fraction);
    };

    const auto gain = lerp (
        interpolateAtRate (ratePosition.lower),
        interpolateAtRate (ratePosition.upper),
        ratePosition.fraction);
    return juce::jlimit (
        juce::Decibels::decibelsToGain (-72.0f),
        juce::Decibels::decibelsToGain (48.0f),
        std::isfinite (gain) ? gain : 1.0f);
}

float DistortionEngine::calculateReferenceAutoGain (
    const Parameters& parameters,
    double displaySampleRate)
{
    const auto mode = static_cast<Mode> (
        juce::jlimit (0, modeCount - 1, parameters.mode));
    if (mode == Mode::spectralClip)
    {
        const auto c01 = unipolarCharacter (parameters.character);
        const auto stages = juce::jlimit (
            1, maximumStages, parameters.stages);
        const auto threshold =
            0.78f * juce::Decibels::decibelsToGain (
                -parameters.driveDb * lerp (0.35f, 1.0f, c01));
        const auto depth = driveDepth (parameters.driveDb);
        double inputEnergy = 0.0;
        double outputEnergy = 0.0;
        float outputPeak = 0.0f;
        for (int point = 0;
             point < Visualization::pointCount;
             ++point)
        {
            const auto input = 1.5f * static_cast<float> (point)
                / static_cast<float> (Visualization::pointCount - 1);
            const auto output = lerp (
                input,
                shapeSpectralMagnitude (
                    input, threshold, c01, stages),
                depth);
            inputEnergy += static_cast<double> (
                input) * input;
            outputEnergy += static_cast<double> (
                output) * output;
            outputPeak = juce::jmax (
                outputPeak, std::abs (output));
        }
        if (inputEnergy <= 1.0e-9 || outputEnergy <= 1.0e-9)
            return 1.0f;
        auto gain = static_cast<float> (
            std::sqrt (inputEnergy / outputEnergy));
        // The transfer-view calibration slightly overestimates makeup for the
        // overlapped Hann analysis/resynthesis path. This fixed correction was
        // measured against the actual four-times-overlapped processor.
        gain *= 0.965f;
        gain = juce::jlimit (
            juce::Decibels::decibelsToGain (-72.0f),
            juce::Decibels::decibelsToGain (18.0f),
            gain);
        if (outputPeak > 1.0e-6f)
            gain = juce::jmin (gain, 0.98f / outputPeak);
        return gain;
    }

    const auto longMemoryCalibration =
        mode == Mode::downsample
        || mode == Mode::slewLimiter
        || mode == Mode::phaseDistortion;
    const auto calibrationSamples =
        mode == Mode::phaseDistortion
            ? 65536
            : (longMemoryCalibration ? 32768 : 4096);
    const auto warmupSamples =
        mode == Mode::phaseDistortion
            ? 8192
            : (longMemoryCalibration ? 4096 : 512);
    std::array<
        std::array<StageState, maximumStages>,
        maximumChannels> calibrationStates {};
    if (mode == Mode::phaseDistortion)
    {
        const auto requiredDelaySamples =
            juce::roundToInt (0.05 * displaySampleRate) + 2;
        for (auto& channel : calibrationStates)
            for (auto& state : channel)
                state.ensurePhaseDelaySize (requiredDelaySamples);
    }
    const auto stages = juce::jlimit (1, maximumStages, parameters.stages);
    const auto driveNormalised = juce::jlimit (
        0.0f, 1.0f, parameters.driveDb / 36.0f);
    const auto stageDriveDb = parameters.driveDb;
    const auto stageGain = juce::Decibels::decibelsToGain (stageDriveDb);
    const auto stageDepth = driveDepth (parameters.driveDb);
    const auto dcCoefficient = static_cast<float> (std::exp (
        -2.0 * juce::MathConstants<double>::pi * 5.0
        / juce::jmax (1.0, displaySampleRate)));
    const auto calibrationChannels =
        parameters.asymmetryStereo ? maximumChannels : 1;
    std::array<float, maximumChannels> dcInput {};
    std::array<float, maximumChannels> dcOutput {};
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    float outputPeak = 0.0f;
    for (int sample = 0; sample < calibrationSamples; ++sample)
    {
        const auto time = static_cast<double> (sample) / displaySampleRate;
        const auto input = static_cast<float> (
            0.61 * std::sin (
                juce::MathConstants<double>::twoPi * 173.0 * time)
            + 0.19 * std::sin (
                juce::MathConstants<double>::twoPi * 997.0 * time));
        for (int channel = 0; channel < calibrationChannels; ++channel)
        {
            const auto channelAsymmetry =
                parameters.asymmetryStereo && channel == 1
                    ? -parameters.asymmetry
                    : parameters.asymmetry;
            const auto dcMix = dcBlockingAmount (
                mode, parameters.character, channelAsymmetry);
            const auto modeContext = makeModeContext (
                mode,
                parameters.character,
                channelAsymmetry,
                driveNormalised,
                displaySampleRate,
                displaySampleRate);
            const auto value = processCascadeSample (
                input,
                modeContext,
                stageGain,
                stageDepth,
                stages,
                calibrationStates[static_cast<size_t> (channel)]);
            const auto index = static_cast<size_t> (channel);
            const auto filtered =
                value
                - dcInput[index]
                + dcCoefficient * dcOutput[index];
            dcInput[index] = value;
            dcOutput[index] = filtered;
            auto blocked = lerp (value, filtered, dcMix);
            if (mode == Mode::schmittHysteresis)
                blocked = juce::jlimit (-0.98f, 0.98f, blocked);

            if (sample >= warmupSamples)
            {
                inputEnergy += static_cast<double> (input) * input;
                outputEnergy += static_cast<double> (blocked) * blocked;
                outputPeak = juce::jmax (
                    outputPeak, std::abs (blocked));
            }
        }
    }

    if (inputEnergy <= 1.0e-9 || outputEnergy <= 1.0e-9)
        return 1.0f;

    auto gain = static_cast<float> (std::sqrt (inputEnergy / outputEnergy));
    const auto maximumBoostDb =
        mode == Mode::slewLimiter
            ? 48.0f
            : (mode == Mode::downsample
                ? 36.0f
                : 18.0f);
    gain = juce::jlimit (
        juce::Decibels::decibelsToGain (-72.0f),
        juce::Decibels::decibelsToGain (maximumBoostDb),
        gain);
    if (mode == Mode::schmittHysteresis && outputPeak > 1.0e-6f)
        gain = juce::jmin (gain, 0.98f / outputPeak);
    return gain;
}

float DistortionEngine::foldLinear (float input, float threshold) noexcept
{
    return foldDirect (input, threshold);
}

float DistortionEngine::wrapBipolar (float input, float period) noexcept
{
    const auto safePeriod = juce::jmax (0.001f, period);
    auto wrapped = std::fmod (input + safePeriod, 2.0f * safePeriod);
    if (wrapped < 0.0f)
        wrapped += 2.0f * safePeriod;
    return wrapped / safePeriod - 1.0f;
}

float DistortionEngine::chebyshev (float input, int order) noexcept
{
    const auto x = clampBipolar (input);
    if (order <= 0)
        return 1.0f;
    if (order == 1)
        return x;

    auto previous = 1.0f;
    auto current = x;
    for (int n = 2; n <= order; ++n)
    {
        const auto next = 2.0f * x * current - previous;
        previous = current;
        current = next;
    }
    return current;
}
} // namespace dd
