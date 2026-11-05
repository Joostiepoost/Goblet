// Mixer.h — the audio callback: three decks summed to the stereo output.
//
// Step 2: per deck, mute and solo (with 5 ms gain ramps, so neither clicks)
// and a peak meter; on the sum, a peak meter and a clip lamp. The plugin
// slots and faders arrive in steps 3 and 4 (see the proposal).
//
// Signal per deck:  player -> meter -> mute/solo -> sum
// The deck meters read before mute and solo, so a muted deck still shows
// that it is playing and how loud it will be when it comes in.

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

    // Mute and solo. A deck is heard when it is not muted and, if any deck is
    // soloed, it is soloed too. Mute wins over solo.
    void setMute (int index, bool m)                      { strips[(size_t) index].mute.store (m); }
    bool isMuted (int index) const noexcept               { return strips[(size_t) index].mute.load(); }
    void setSolo (int index, bool s)                      { strips[(size_t) index].solo.store (s); }
    bool isSoloed (int index) const noexcept              { return strips[(size_t) index].solo.load(); }
    bool isAudible (int index) const noexcept;

    // Meters: the highest absolute sample since the last call (and resets it).
    // channel 0 = left, 1 = right. Read from the message thread.
    float takeDeckPeak (int index, int channel) noexcept  { return strips[(size_t) index].peak[(size_t) channel].exchange (0.0f); }
    float takeSumPeak (int channel) noexcept              { return sumPeak[(size_t) channel].exchange (0.0f); }

    // Clip lamp: lit once the sum reaches 0 dBFS, until reset.
    bool hasClipped() const noexcept                      { return clipped.load(); }
    void resetClip()                                      { clipped.store (false); }

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

    static constexpr double muteRampMs = 5.0;

private:
    static void raise (std::atomic<float>& peak, float value) noexcept
    {
        float old = peak.load (std::memory_order_relaxed);
        while (value > old && ! peak.compare_exchange_weak (old, value, std::memory_order_relaxed)) {}
    }

    struct Strip
    {
        std::atomic<bool> mute { false }, solo { false };
        std::array<std::atomic<float>, 2> peak {};
        float gain = 1.0f;                     // audio thread: the current mute/solo gain
        juce::AudioBuffer<float> buffer;       // audio thread: the deck's output for this block
    };

    std::array<DeckPlayer, numDecks> decks;
    std::array<Strip, numDecks> strips;
    std::atomic<int> quality { (int) Quality::Max };
    std::atomic<uint32_t> playAllCounter { 0 }, stopAllCounter { 0 };
    uint32_t lastPlayAll = 0, lastStopAll = 0;
    std::atomic<double> outputRate { 48000.0 };
    std::array<std::atomic<float>, 2> sumPeak {};
    std::atomic<bool> clipped { false };
    float gainStep = 1.0f / 240.0f;
    juce::AudioBuffer<float> sum;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Mixer)
};

} // namespace goblet
