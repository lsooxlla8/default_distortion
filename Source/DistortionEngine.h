#pragma once

#include "ChowTapeHysteresis.h"
#include "Parameters.h"
#include "TransientSplitter.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
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
        sineErosion,
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
    void process (juce::AudioBuffer<float>& buffer,
                  const Parameters& parameters,
                  const juce::AudioBuffer<float>* detectorInput = nullptr);
    void processBand (juce::AudioBuffer<float>& buffer,
                      const Parameters& parameters,
                      const juce::AudioBuffer<float>* detectorInput = nullptr,
                      bool forceTransientLatency = false,
                      const float* sharedDynamicOffsets = nullptr,
                      int sharedDynamicSamples = -1);

    [[nodiscard]] const float* getDynamicDriveOffsets() const noexcept
    {
        return dynamicDriveOffsets.data();
    }
    [[nodiscard]] int getDynamicDriveSampleCount() const noexcept
    {
        return dynamicDriveSamples;
    }
    void synchroniseDynamicStateFrom (const DistortionEngine&) noexcept;
    static std::pair<float, float> dynamicsTimingForSpeed (float) noexcept;

    [[nodiscard]] int getLatencySamples() const noexcept
    {
        return fixedLatencySamples + routingLatencySamples;
    }
    [[nodiscard]] int getMaximumLatencySamples() const noexcept
    {
        return fixedLatencySamples + transientRoutingLatencySamples;
    }
    [[nodiscard]] int getBaseLatencySamples() const noexcept
    {
        return fixedLatencySamples;
    }
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
    static int getModeForDisplayPosition (int position) noexcept;
    static int getDisplayPositionForMode (int mode) noexcept;
    static bool isCharacterBipolar (int mode) noexcept;
    static bool isCharacterStepped (int mode) noexcept;
    static float getDefaultCharacter (int mode) noexcept;
    static bool hasSecondaryControl (int mode) noexcept;
    static float getDefaultSecondary (int mode) noexcept;
    static juce::String getSecondaryName (int mode);
    static juce::String formatCharacterValue (int mode,
                                              float rawValue,
                                              double sampleRate = 48000.0);
    static juce::String formatDriveValue (int mode,
                                         float driveDb,
                                         double sampleRate = 48000.0);
    static void makeVisualization (const Parameters&,
                                   double sampleRate,
                                   Visualization&);
    // Offline reference used to regenerate Source/AutoGainTable.h whenever a
    // distortion algorithm or its parameter mapping changes.
    static float calculateReferenceAutoGain (const Parameters&,
                                             double sampleRate);

private:
    struct StageState
    {
        float previousInput = 0.0f;
        float previousOutput = 0.0f;
        float memory = 0.0f;
        float secondary = 0.0f;
        float envelope = 0.0f;
        float heldSample = 0.0f;
        float tailGain = 1.0f;
        float pinkA = 0.0f;
        float pinkB = 0.0f;
        float pinkC = 0.0f;
        float bandpassIc1 = 0.0f;
        float bandpassIc2 = 0.0f;
        float bandpass2Ic1 = 0.0f;
        float bandpass2Ic2 = 0.0f;
        float smoothedDelaySamples = 0.0f;
        double phase = 0.0;
        std::uint32_t noiseState = UINT32_C (0x9e3779b9);
        int counter = 0;
        int silenceSamples = 0;
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
            tailGain = 1.0f;
            pinkA = 0.0f;
            pinkB = 0.0f;
            pinkC = 0.0f;
            bandpassIc1 = 0.0f;
            bandpassIc2 = 0.0f;
            bandpass2Ic1 = 0.0f;
            bandpass2Ic2 = 0.0f;
            smoothedDelaySamples = 0.0f;
            phase = 0.0;
            noiseState = UINT32_C (0x9e3779b9);
            counter = 0;
            silenceSamples = 0;
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
        float secondaryParameter = 0.0f;
        float asymmetry = 0.0f;
        float driveNormalised = 0.0f;
        double processingSampleRate = 44100.0;
        double hostSampleRate = 44100.0;
        bool tapeDcBlockerEnabled = true;
        chowtape::Model tapeModel;
        chowtape::detail::IntegrationCoefficients tapeIntegration;
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
                                const Parameters& startParameters,
                                const Parameters& endParameters,
                                double processingSampleRate,
                                double hostSampleRate,
                                int firstStage = 0,
                                int stageCountOverride = -1,
                                bool deferTapeDcBlocker = false);
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
                                        float secondaryParameter,
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
        std::array<StageState, maximumStages>& states,
        int firstStage = 0);

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
    void resetSmartAutoGain() noexcept;
    void prepareKWeightingFilters();
    void accumulateLoudnessSample (float dry, float wet, int channel) noexcept;
    void finishLoudnessSlice() noexcept;
    static double calculateGatedLoudnessEnergy (
        const std::array<double, 8>& blocks,
        int blockCount) noexcept;
    static float lookupDeterministicGain (
        const Parameters&, double sampleRate) noexcept;
    void processInternal (juce::AudioBuffer<float>&,
                          const Parameters&,
                          bool allowSmartAutoGain,
                          bool clampFinalOutput,
                          const juce::AudioBuffer<float>* detectorInput,
                          bool forceTransientLatency,
                          const float* sharedDynamicOffsets,
                          int sharedDynamicSamples);
    void prepareDynamicDrive (const juce::AudioBuffer<float>&,
                              const Parameters&,
                              int samples) noexcept;
    void updateInputHighPass (float cutoffHz);
    void updateOutputLowPass (float cutoffHz);
    void processInputHighPass (juce::AudioBuffer<float>&);
    float processOutputLowPassSample (float input, int channel) noexcept;
    void applyPlacementRouting (juce::AudioBuffer<float>& wet,
                                juce::AudioBuffer<float>& dry,
                                const Parameters&,
                                bool forceTransientLatency);
    void delayForTransientRouting (juce::AudioBuffer<float>& wet,
                                   juce::AudioBuffer<float>& dry);

    double sampleRate = 44100.0;
    int preparedChannels = 2;
    int preparedBlockSize = 512;
    int fixedLatencySamples = fftSize;
    int lastMode = -1;
    int lastAutoGainMode = -1;

    std::array<std::array<StageState, maximumStages>, maximumChannels> stageStates {};
    std::array<SpectralState, maximumChannels> spectralStates {};
    std::array<ToneFilters, maximumChannels> toneFilters {};
    std::array<juce::IIRFilter, maximumChannels> inputHpFirst {};
    std::array<juce::IIRFilter, maximumChannels> inputHpSecond {};
    std::array<juce::IIRFilter, maximumChannels> outputLpFirst {};
    std::array<juce::IIRFilter, maximumChannels> outputLpSecond {};
    std::array<KWeightingFilter, maximumChannels> smartDryKWeighting {};
    std::array<KWeightingFilter, maximumChannels> smartWetKWeighting {};
    std::array<float, maximumChannels> dcPreviousInput {};
    std::array<float, maximumChannels> dcPreviousOutput {};
    std::array<float, maximumChannels> dcMixState {};

    std::array<std::unique_ptr<Oversampler>, 3> oversamplers;
    std::array<
        std::array<std::unique_ptr<Oversampler>, maximumStages>,
        3> tapeStageOversamplers;
    std::array<int, 3> oversamplingLatencies {};

    std::array<std::vector<float>, maximumChannels> dryDelayBuffers;
    std::array<std::vector<float>, maximumChannels> wetDelayBuffers;
    std::array<int, maximumChannels> dryDelayPositions {};
    std::array<int, maximumChannels> wetDelayPositions {};
    std::array<std::vector<float>, maximumChannels> routeDryDelayBuffers;
    std::array<std::vector<float>, maximumChannels> routeWetDelayBuffers;
    std::array<int, maximumChannels> routeDryDelayPositions {};
    std::array<int, maximumChannels> routeWetDelayPositions {};

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> dryTransientBuffer;
    juce::AudioBuffer<float> drySustainBuffer;
    juce::AudioBuffer<float> wetTransientBuffer;
    juce::AudioBuffer<float> wetSustainBuffer;
    TransientSplitter dryTransientSplitter;
    TransientSplitter wetTransientSplitter;
    std::vector<float> dynamicDriveOffsets;
    const float* activeDynamicDriveOffsets = nullptr;
    int dynamicDriveSamples = 0;
    float dynamicEnvelope = 0.0f;
    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> spectralWindow {};

    float smoothedDriveDb = 0.0f;
    float smoothedCharacter = 0.0f;
    float smoothedSecondary = 0.0f;
    float smoothedAsymmetry = 0.0f;
    float smoothedTone = 0.0f;
    float smoothedMix = 1.0f;
    float smoothedOutputDb = 0.0f;
    float smoothedDynamicPercent = 0.0f;
    float smoothedInputHpHz = 0.0f;
    float smoothedOutputLpHz = 20000.0f;
    float lastInputHpCoefficientHz = std::numeric_limits<float>::quiet_NaN();
    float lastOutputLpCoefficientHz = std::numeric_limits<float>::quiet_NaN();
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputHpMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputLpMix;
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
    std::atomic<float> smartProgress { 0.0f };
    std::atomic<bool> smartLockedForUi { false };
    std::uint64_t lastGainSignature = 0;
    std::uint64_t lastGainLookupSignature = 0;
    std::uint64_t lastSmartGainSignature = 0;
    float lastToneCoefficientAmount = std::numeric_limits<float>::quiet_NaN();
    bool toneFiltersBypassed = true;
    int routingLatencySamples = 0;
    int transientRoutingLatencySamples = 0;
};
} // namespace dd
