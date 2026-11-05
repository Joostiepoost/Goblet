// PluginChain.h — two plugin slots in series (adapted from Effet's EffectChain).
//
//   input ─ slot 1 ─ slot 2 ─ output
//
// Goblet has seven of these: before and after each deck's fader, and on the
// sum. An empty slot passes the signal unchanged.
//
// * Bypass ramps a slot's wet amount to 0 over 20 ms against a dry path
//   delayed by the plugin's latency; the plugin keeps running, so the chain's
//   latency does not change and nothing clicks.
// * Plugins are prepared on the message thread and swapped in under a short
//   lock. The audio thread only tries the lock: if a swap is happening at
//   that moment, that one block is silent.
// * Buses: as in Effet, only the main buses are set; sidechain buses stay
//   enabled and receive silence (some AU plugins crash otherwise).

#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>

namespace goblet
{

constexpr int slotsPerChain = 2;

class PluginChain
{
public:
    PluginChain();
    ~PluginChain();

    //== Message thread ======================================================
    // (Re)prepares every plugin; call while audio is stopped (device about to start).
    void prepare (double sampleRate, int maximumBlockSize);
    double getSampleRate() const noexcept    { return sampleRate; }
    int getBlockSize() const noexcept        { return blockSize; }
    void setPlayHead (juce::AudioPlayHead* head) { playHead = head; }

    // Puts a plugin into a slot; what was there is handed back in `replaced`
    // so the caller can delete it after closing its window. Returns false with
    // an error if the plugin cannot be used.
    bool insert (int slot, std::unique_ptr<juce::AudioProcessor> processor,
                 std::unique_ptr<juce::AudioProcessor>& replaced, juce::String& error);
    std::unique_ptr<juce::AudioProcessor> remove (int slot);
    void swapSlots();                                     // slot 1 <-> slot 2

    juce::AudioProcessor* get (int slot) const noexcept;
    void setBypass (int slot, bool b)                    { slots[(size_t) slot].bypass.store (b); }
    bool isBypassed (int slot) const noexcept            { return slots[(size_t) slot].bypass.load(); }

    int getLatency() const noexcept;                      // samples, both slots (bypassed ones too); message thread
    int getProcessedLatency() const noexcept             { return processedLatency.load(); }   // as of the last process(); any thread
    int numPlugins() const noexcept;
    void setNonRealtime (bool offline);

    // Layout with every plugin's description, state and bypass (for saving);
    // plugins are loaded back by the caller.
    std::unique_ptr<juce::XmlElement> createXml (const juce::String& tag) const;

    //== Audio thread (MIDI) =================================================
    // A plugin parameter, normalised 0..1. They take the chain's lock only if it
    // is free (a plugin being swapped in is skipped); getParameter returns -1 then.
    void setParameter (int slot, int index, float value) noexcept;
    float getParameter (int slot, int index) const noexcept;

    //== Audio thread ========================================================
    // Processes a stereo buffer in place.
    void process (juce::AudioBuffer<float>& io, int numSamples) noexcept;

    static constexpr int maxDelay = 1 << 16;

private:
    struct Delay
    {
        void allocate()                         { buffer.setSize (2, maxDelay, false, true); buffer.clear(); pos = 0; }
        void clear()                            { buffer.clear(); pos = 0; }
        void process (float* const* ch, int numSamples, int delay) noexcept;
        juce::AudioBuffer<float> buffer;
        int pos = 0;
    };

    struct Slot
    {
        std::unique_ptr<juce::AudioProcessor> processor;
        std::unique_ptr<juce::AudioBuffer<float>> buffer;   // the plugin's own channels
        juce::AudioBuffer<float> dry;
        juce::MidiBuffer midi;
        int inputs = 2, outputs = 2;
        std::atomic<bool> bypass { false };
        juce::SmoothedValue<float> wet;
        Delay dryDelay;
    };

    static bool configureBuses (juce::AudioProcessor&);
    void prepareProcessor (juce::AudioProcessor&, std::unique_ptr<juce::AudioBuffer<float>>& bufferOut) const;
    void processSlot (Slot&, juce::AudioBuffer<float>& io, int start, int numSamples) noexcept;

    std::array<Slot, slotsPerChain> slots;
    mutable juce::SpinLock swapLock;
    juce::AudioPlayHead* playHead = nullptr;
    std::atomic<int> processedLatency { 0 };
    double sampleRate = 48000.0;
    int blockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginChain)
};

// A plain stereo delay line, for aligning the decks' latencies.
class AlignDelay
{
public:
    AlignDelay()                                     { buffer.setSize (2, PluginChain::maxDelay, false, true); buffer.clear(); }
    void clear()                                     { buffer.clear(); pos = 0; }
    void process (float* const* ch, int numSamples, int delay) noexcept;

private:
    juce::AudioBuffer<float> buffer;
    int pos = 0;
};

} // namespace goblet
