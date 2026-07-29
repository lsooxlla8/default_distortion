#pragma once

#include "ChowTapeHysteresis.h"
#include "Parameters.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dd
{
class DistortionEngine
{
public:
    static constexpr int modeCount = 30;
    static constexpr int maximumStages = 8;
    static constexpr int maximumChannels = 2;

    enum class Mode
    {
        morphSoftClip = 0,
        hardClip,
        diodeClipper,
        triodeStage,
        transistorFet,
        tapeHysteresis,
        harmonicMorph,
        phaseDistortion,
        spectralClip,
        signSquare,
        zeroSquare,
        fullWaveRectifier,
        softFullWaveRectifier,
        transformerCore,
        halfWaveRectifier,
        classBSaturation,
        topologyFold,
        recursiveFoldback,
        sineFold,
        chebyshevFold,
        moduloWrap,
        downsample,
        bitCrusher,
        bitRotation,
        deltaCrusher,
        slewLimiter,
        schmittHysteresis,
        feedbackSaturator,
        resonantFeedbackClip,
        dynamicSag
    };

    DistortionEngine();
    ~DistortionEngine();

    struct Visualization
    {
        static constexpr int pointCount = 192;
        std::array<float, pointCount> input {};
        std::array<float, pointCount> output {};
        bool timeDomain = false;
        bool spectralDomain = false;
    };

    void prepare (double newSampleRate, int maximumBlockSize, int channels);
    void primeAutoGain (const Parameters&);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const Parameters& parameters);

    [[nodiscard]] int getLatencySamples() const noexcept { return fixedLatencySamples; }
    [[nodiscard]] float getSmartAutoGainProgress() const noexcept
    {
        return smartProgress.load (std::memory_order_relaxed);
    }
    [[nodiscard]] bool isSmartAutoGainLocked() const noexcept
    {
        return smartLockedForUi.load (std::memory_order_relaxed);
    }

    static const std::array<juce::String, modeCount>& getModeNames();
    static const std::array<juce::String, modeCount>& getCharacterNames();
    static bool isCharacterBipolar (int mode) noexcept;
    static bool isCharacterStepped (int mode) noexcept;
    static float getDefaultCharacter (int mode) noexcept;
    static juce::String formatCharacterValue (int mode,
                                              float rawValue,
                                              double sampleRate = 48000.0);
    static juce::String formatDriveValue (int mode,
                                         float driveDb,
                                         double sampleRate = 48000.0);
    static void makeVisualization (const Parameters&,
                                   double sampleRate,
                                   Visualization&);

private:
    struct StageState
    {
        float previousInput = 0.0f;
        float previousOutput = 0.0f;
        float memory = 0.0f;
        float secondary = 0.0f;
        float envelope = 0.0f;
        float heldSample = 0.0f;
        double phase = 0.0;
        int counter = 0;
        int phaseWritePosition = 0;
        bool gateHigh = false;
        chowtape::State tape;
        std::vector<float> phaseDelay;

        void ensurePhaseDelaySize (int requiredSamples)
        {
            const auto safeSize = juce::jmax (2, requiredSamples);
            if (static_cast<int> (phaseDelay.size()) != safeSize)
                phaseDelay.resize (static_cast<size_t> (safeSize), 0.0f);
        }

        void reset() noexcept
        {
            previousInput = 0.0f;
            previousOutput = 0.0f;
            memory = 0.0f;
            secondary = 0.0f;
            envelope = 0.0f;
            heldSample = 0.0f;
            phase = 0.0;
            counter = 0;
            phaseWritePosition = 0;
            gateHigh = false;
            tape = {};
            std::fill (phaseDelay.begin(), phaseDelay.end(), 0.0f);
        }
    };

    struct ModeContext
    {
        Mode mode = Mode::morphSoftClip;
        float character = 0.0f;
        float character01 = 0.0f;
        float asymmetry = 0.0f;
        float driveNormalised = 0.0f;
        double processingSampleRate = 44100.0;
        double hostSampleRate = 44100.0;
        std::array<float, 8> coefficients {};
        std::array<int, 2> integers {};
    };

    static constexpr int fftOrder = 8;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int fftHop = fftSize / 4;

    struct SpectralState
    {
        std::array<float, fftSize> input {};
        std::array<float, fftSize> output {};
        std::array<float, fftSize * 2> fftData {};
        int position = 0;
        int hopCounter = 0;

        void reset() noexcept
        {
            input.fill (0.0f);
            output.fill (0.0f);
            fftData.fill (0.0f);
            position = 0;
            hopCounter = 0;
        }
    };

    struct ToneFilters
    {
        juce::IIRFilter preLow;
        juce::IIRFilter preHigh;
        juce::IIRFilter postLow;
        juce::IIRFilter postHigh;

        void reset()
        {
            preLow.reset();
            preHigh.reset();
            postLow.reset();
            postHigh.reset();
        }
    };

    struct KWeightingFilter
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

    using Oversampler = juce::dsp::Oversampling<float>;

    void updateToneFilters (float toneAmount);
    void processTonePre (juce::AudioBuffer<float>&);
    void processTonePost (juce::AudioBuffer<float>&);
    void processNonlinearBlock (juce::dsp::AudioBlock<float> block,
                                const Parameters& parameters,
                                double processingSampleRate,
                                double hostSampleRate);
    void processSpectralBlock (juce::AudioBuffer<float>&,
                               const Parameters& parameters);
    float processSpectralSample (float input,
                                 int channel,
                                 const Parameters& parameters);
    static float processSpectralSampleCore (
        float input,
        SpectralState&,
        const Parameters&,
        juce::dsp::FFT&,
        const std::array<float, fftSize>& window);
    static void processSpectralFrameCore (
        SpectralState&,
        const Parameters&,
        juce::dsp::FFT&,
        const std::array<float, fftSize>& window);

    static ModeContext makeModeContext (Mode mode,
                                        float character,
                                        float asymmetry,
                                        float driveNormalised,
                                        double processingSampleRate,
                                        double hostSampleRate);
    static float processModeSample (float input,
                                    const ModeContext&,
                                    StageState& state);
    static float processCascadeSample (
        float input,
        const ModeContext&,
        float stageGain,
        float stageDepth,
        int stages,
        std::array<StageState, maximumStages>& states);

    float delaySample (float input,
                       int channel,
                       int delaySamples,
                       std::array<std::vector<float>, maximumChannels>& delayBuffers,
                       std::array<int, maximumChannels>& positions) noexcept;

    static float applyAsymmetry (float input, float asymmetry) noexcept;
    static float cubicSoftClip (float input) noexcept;
    static float foldLinear (float input, float threshold) noexcept;
    static float wrapBipolar (float input, float period) noexcept;
    static float chebyshev (float input, int order) noexcept;
    static float quinticSoftClip (float input) noexcept;
    static float shapeSpectralMagnitude (float magnitude,
                                         float threshold,
                                         float character,
                                         int stages) noexcept;
    static float downsampleTargetRate (float character,
                                       double hostSampleRate) noexcept;
    static bool usesLegacyDrivePath (Mode mode) noexcept;
    static bool usesDriveAsAlgorithmParameter (Mode mode) noexcept;
    static bool usesOversampling (Mode mode) noexcept;
    float calculateDeterministicGain (const Parameters& parameters,
                                      double sampleRate);
    void resetSmartAutoGain() noexcept;
    void prepareKWeightingFilters();
    void accumulateLoudnessSample (float dry, float wet, int channel) noexcept;
    void finishLoudnessSlice() noexcept;
    static double calculateGatedLoudnessEnergy (
        const std::array<double, 8>& blocks,
        int blockCount) noexcept;
    void requestDeterministicGain (
        const Parameters&, double, std::uint64_t) noexcept;
    void deterministicGainWorkerLoop();

    double sampleRate = 44100.0;
    int preparedChannels = 2;
    int preparedBlockSize = 512;
    int fixedLatencySamples = fftSize;
    int lastMode = -1;
    int lastAutoGainMode = -1;

    std::array<std::array<StageState, maximumStages>, maximumChannels> stageStates {};
    std::array<SpectralState, maximumChannels> spectralStates {};
    std::array<ToneFilters, maximumChannels> toneFilters {};
    std::array<KWeightingFilter, maximumChannels> smartDryKWeighting {};
    std::array<KWeightingFilter, maximumChannels> smartWetKWeighting {};
    std::array<float, maximumChannels> dcPreviousInput {};
    std::array<float, maximumChannels> dcPreviousOutput {};

    std::array<std::unique_ptr<Oversampler>, 3> oversamplers;
    std::array<int, 3> oversamplingLatencies {};

    std::array<std::vector<float>, maximumChannels> dryDelayBuffers;
    std::array<std::vector<float>, maximumChannels> wetDelayBuffers;
    std::array<int, maximumChannels> dryDelayPositions {};
    std::array<int, maximumChannels> wetDelayPositions {};

    juce::AudioBuffer<float> dryBuffer;
    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> spectralWindow {};

    float smoothedDriveDb = 0.0f;
    float smoothedCharacter = 0.0f;
    float smoothedAsymmetry = 0.0f;
    float smoothedTone = 0.0f;
    float smoothedMix = 1.0f;
    float smoothedOutputDb = 0.0f;
    float autoGainLinear = 1.0f;
    float deterministicGainLinear = 1.0f;
    float smartGainLinear = 1.0f;
    double smartWetPeak = 0.0;
    double smartDrySliceEnergy = 0.0;
    double smartWetSliceEnergy = 0.0;
    std::array<double, 4> smartDryRecentSlices {};
    std::array<double, 4> smartWetRecentSlices {};
    std::array<double, 8> smartDryLoudnessBlocks {};
    std::array<double, 8> smartWetLoudnessBlocks {};
    int smartSliceSamples = 0;
    int smartSliceWritePosition = 0;
    int smartCompletedSlices = 0;
    int smartLoudnessBlockCount = 0;
    int smartStableSamples = 0;
    int smartMeasuredSamples = 0;
    bool smartGainLocked = false;
    bool gainCalibrationPending = false;
    std::atomic<float> smartProgress { 0.0f };
    std::atomic<bool> smartLockedForUi { false };
    std::uint64_t lastGainSignature = 0;
    std::uint64_t lastSmartGainSignature = 0;
    std::uint64_t appliedGainSignature = 0;
    float lastToneCoefficientAmount = std::numeric_limits<float>::quiet_NaN();
    bool toneFiltersBypassed = true;

    std::thread gainWorker;
    std::mutex gainWorkerMutex;
    std::condition_variable gainWorkerCondition;
    Parameters queuedGainParameters;
    double queuedGainSampleRate = 44100.0;
    std::uint64_t queuedGainSignature = 0;
    bool gainJobPending = false;
    bool gainWorkerShouldExit = false;
    std::atomic<float> completedDeterministicGain { 1.0f };
    std::atomic<std::uint64_t> completedGainSignature { 0 };
};
} // namespace dd
