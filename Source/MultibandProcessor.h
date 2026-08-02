#pragma once

#include "DistortionEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>

namespace dd
{
class MultibandProcessor
{
public:
    MultibandProcessor();
    ~MultibandProcessor();

    void prepare (double sampleRate, int maximumBlockSize, int channels);
    void reset();
    void process (juce::AudioBuffer<float>&,
                  const Parameters& master,
                  const MultibandParameters& multiband,
                  int soloBand);

    [[nodiscard]] int getLatencySamples (bool linearPhase) const noexcept;
    [[nodiscard]] float getSmartAutoGainProgress() const noexcept;
    [[nodiscard]] bool isSmartAutoGainLocked() const noexcept;

    static int slopeDecibelsPerOctave (int slopeIndex) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandProcessor)
};
} // namespace dd
