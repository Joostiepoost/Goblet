// Mixer.h — the audio callback: three decks summed to the stereo output.
//
// Step 1 plays the decks and adds them up. The plugin slots, faders, mute,
// solo, meters and the sum's slots arrive in steps 2 to 4 (see the proposal).

#pragma once

#include <JuceHeader.h>
#include "DeckPlayer.h"

#include <array>
#include <atomic>

namespace goblet
{

class Mixer : public juce::AudioIODeviceCallback
{
public:
    static constexpr int numDecks = 3;

    Mixer() = default;

    DeckPlayer& deck (int index) noexcept                 { return decks[(size_t) index]; }
    const DeckPlayer& deck (int index) const noexcept     { return decks[(size_t) index]; }

    void setQuality (Quality q)                           { quality.store ((int) q); }
    Quality getQuality() const noexcept                   { return (Quality) quality.load(); }

    // Start or stop every deck that has a file, together, in the same sample.
    void playAll()                                        { playAllCounter.fetch_add (1, std::memory_order_release); }
    void stopAll()                                        { stopAllCounter.fetch_add (1, std::memory_order_release); }
    bool anyPlaying() const noexcept;

    double getOutputRate() const noexcept                 { return outputRate.load(); }

    //== Processing (also usable offline) ====================================
    void prepare (double sampleRate, int maximumBlockSize);
    void process (float* const* outputs, int numOutputs, int numSamples) noexcept;

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

private:
    std::array<DeckPlayer, numDecks> decks;
    std::atomic<int> quality { (int) Quality::Max };
    std::atomic<uint32_t> playAllCounter { 0 }, stopAllCounter { 0 };
    uint32_t lastPlayAll = 0, lastStopAll = 0;
    std::atomic<double> outputRate { 48000.0 };
    juce::AudioBuffer<float> sum;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Mixer)
};

} // namespace goblet
