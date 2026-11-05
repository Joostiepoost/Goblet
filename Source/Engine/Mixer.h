// Mixer.h — the audio callback: three decks summed to the stereo output.
//
// Signal per deck:
//   player -> trim -> pre-fader slots -> fader -> post-fader slots -> meter
//          -> latency alignment -> mute/solo -> sum
// and on the sum:  sum slots -> meter, clip lamp -> output
//
// * Trim (-24..+24 dB) and fader (position 0..1, see faderPositionToDb) are
//   smoothed per sample over a ramp time (20 ms from the mouse, the mapping's
//   smoothing time from MIDI), so a 7-bit controller is not heard in steps.
// * MIDI mappings are applied at the start of every block (MidiControl.h).
// * Mute and solo ramp over 5 ms, so neither clicks.
// * The deck meters read before mute and solo, so a muted deck still shows
//   that it is playing and how loud it will be when it comes in.
// * Latency alignment: each deck is delayed by the difference between its
//   plugins' latency and the slowest deck's, so the decks stay sample-aligned
//   in the sum whatever plugins they hold.
// * The Mixer is the plugins' host playhead: "playing" while any deck plays,
//   with the time counted from the last Play all.
// * Capture: the sum (after its slots) and one stem per deck (just before the
//   sum, so after mute and solo) go to threaded WAV writers. Each stream skips
//   its own latency at the start, so stems and sum line up sample for sample.
//   When no deck plays any more, the capture runs on until the sum falls below
//   -90 dBFS for 0.3 s (or a time limit): the tail. A capture can be listened
//   to (the preview), mixed into the output after everything else.
// * The gesture: while capturing, every change the mixer applies is recorded
//   with its time (Gesture.h), so a take can be rendered again offline: a
//   second Mixer replays it through applyGestureEvent(), with MIDI off.

#pragma once

#include <JuceHeader.h>
#include "DeckPlayer.h"
#include "PluginChain.h"
#include "MidiControl.h"
#include "Gesture.h"

#include <array>
#include <atomic>

namespace goblet
{

class Mixer : public juce::AudioIODeviceCallback,
              public juce::AudioPlayHead
{
public:
    static constexpr int numDecks = 3;

    // The seven plugin chains: 0-2 before each deck's fader, 3-5 after it, 6 on the sum.
    static constexpr int numChains = 2 * numDecks + 1;
    static constexpr int preChain (int deck) noexcept     { return deck; }
    static constexpr int postChain (int deck) noexcept    { return numDecks + deck; }
    static constexpr int sumChain = 2 * numDecks;

    Mixer();
    ~Mixer() override;

    PluginChain& chain (int id) noexcept                  { return *chains[(size_t) id]; }
    const PluginChain& chain (int id) const noexcept      { return *chains[(size_t) id]; }

    // Latencies in samples, as of the last processed block (any thread).
    int getDeckLatency (int deck) const noexcept          { return chain (preChain (deck)).getProcessedLatency() + chain (postChain (deck)).getProcessedLatency(); }
    int getAlignedLatency() const noexcept;               // the slowest deck: what every deck is delayed to
    int getTotalLatency() const noexcept                  { return getAlignedLatency() + chain (sumChain).getProcessedLatency(); }

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

    // Trim, in dB (-24..+24), before the deck's plugins.
    static constexpr float trimRangeDb = 24.0f;
    void setTrimDb (int index, float db, float rampMs = 20.0f) noexcept;
    float getTrimDb (int index) const noexcept            { return strips[(size_t) index].trimDb.load(); }
    static float trimDbToNormal (float db) noexcept       { return juce::jlimit (0.0f, 1.0f, (db + trimRangeDb) / (2.0f * trimRangeDb)); }
    static float normalToTrimDb (float v) noexcept        { return juce::jlimit (-trimRangeDb, trimRangeDb, (2.0f * v - 1.0f) * trimRangeDb); }

    // Fader, as a position 0..1 (the fader law: +12 dB at the top, 0 dB at 0.8,
    // -12 dB at 0.5, -30 dB at 0.25, silence at the bottom).
    static constexpr float unityPosition = 0.8f;
    void setFaderPosition (int index, float position, float rampMs = 20.0f) noexcept;
    float getFaderPosition (int index) const noexcept     { return strips[(size_t) index].faderPos.load(); }
    static float faderPositionToDb (float position) noexcept;    // -100 = silence
    static float faderDbToPosition (float db) noexcept;
    static float faderGain (float position) noexcept;

    // Capture (message thread). Stream 0 is the sum, 1..3 the stems of decks A..C;
    // a null writer means that stream is not written.
    static constexpr int numStreams = numDecks + 1;
    enum class CaptureState { idle, recording, tail, finished };
    // Where a stream goes: a threaded WAV writer when live, a plain one offline.
    struct Writer
    {
        virtual ~Writer() = default;
        virtual void write (const float* const* channels, int numSamples) = 0;
    };
    void startCapture (const std::array<Writer*, numStreams>& writers, const std::array<int, numStreams>& skipSamples, double tailLimitSeconds);
    void cutCapture() noexcept                            { captureCut.store (true); }      // ends it now, no tail
    void stopCapture();                                   // detaches the writers; after this they may be deleted
    CaptureState getCaptureState() const noexcept         { return captureState.load(); }
    double getCapturedSeconds() const noexcept            { return (double) captureWritten.load() / outputRate.load(); }
    juce::AudioTransportSource& getPreview() noexcept     { return preview; }

    // The gesture of the take being captured, picked up by the message thread.
    int readGesture (GestureEvent* dest, int maxCount) noexcept;
    bool gestureOverflowed() const noexcept               { return gestureOverflow.load(); }
    int64_t getGestureLength() const noexcept             { return gestureLength.load(); }   // samples since Rec

    // Re-render (on the thread that calls process): put the mixer and decks in a
    // recorded state; gains jump to their values instead of ramping.
    void applyGestureEvent (const GestureEvent&) noexcept;
    void snapGains() noexcept;
    void setMidiEnabled (bool e) noexcept                 { midiEnabled.store (e); }

    // A plugin parameter set by a MIDI mapping (audio thread); recorded in the gesture.
    void setPluginParameter (int chain, int slot, int index, float value) noexcept;

    // MIDI.
    CcTable& getCcTable() noexcept                        { return ccTable; }
    MidiEngine& getMidi() noexcept                        { return midi; }

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
    void audioDeviceStopped() override                    { preview.releaseResources(); }

    // juce::AudioPlayHead
    juce::Optional<PositionInfo> getPosition() const override;

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
        std::atomic<float> trimDb { 0.0f }, trimRampMs { 20.0f };
        std::atomic<float> faderPos { unityPosition }, faderRampMs { 20.0f };
        struct Ramp { float current = 1.0f, target = 1.0f, step = 0.0f; int left = 0; };
        Ramp trim, fader;                      // audio thread: the smoothed gains
        float gain = 1.0f;                     // audio thread: the current mute/solo gain
        juce::AudioBuffer<float> buffer;       // audio thread: the deck's output for this block
        AlignDelay align;
    };

    std::array<DeckPlayer, numDecks> decks;
    std::array<Strip, numDecks> strips;
    std::array<std::unique_ptr<PluginChain>, numChains> chains;
    CcTable ccTable;
    MidiEngine midi;
    static void applyRamp (Strip::Ramp&, float target, float rampMs, double rate, float* const* buf, int numSamples) noexcept;
    void writeCapture (int stream, const float* const* channels, int numSamples) noexcept;
    void captureTailCheck (bool anyPlaying, int numSamples) noexcept;

    juce::SpinLock captureLock;
    std::array<Writer*, numStreams> captureWriters {};
    std::array<int, numStreams> captureSkip {};
    std::atomic<CaptureState> captureState { CaptureState::idle };
    std::atomic<bool> captureCut { false };
    std::atomic<int64_t> captureWritten { 0 };
    int64_t captureTail = 0, captureSilent = 0, captureTailLimit = 0;
    juce::AudioTransportSource preview;
    juce::AudioBuffer<float> previewBuffer { 2, 4096 };

    // Gesture recording (audio thread -> message thread).
    void pushGesture (int kind, int deck, int64_t a, int64_t b, double v, double w, double x = 0.0, double y = 0.0) noexcept;
    void recordGestureState() noexcept;
    struct DeckRecord
    {
        float fader = -1.0f, faderRamp = -1.0f, trim = -1000.0f, trimRamp = -1.0f, fadeMs = -1.0f, tension = -1.0f;
        int mute = -1, solo = -1, loop = -1, reverse = -1, shape = -1;
        int64_t in = -1, out = -1;
    };
    std::array<DeckRecord, numDecks> recorded;
    std::array<std::array<int, slotsPerChain>, numChains> recordedBypass {};
    juce::AbstractFifo gestureFifo { 1 << 17 };
    std::vector<GestureEvent> gestureBuffer = std::vector<GestureEvent> ((size_t) (1 << 17));
    std::atomic<bool> gestureOverflow { false }, gestureFirst { false }, midiEnabled { true };
    int64_t gestureClock = 0;
    std::atomic<int64_t> gestureLength { 0 };
    bool recordingGesture = false;
    std::atomic<bool> headPlaying { false };
    std::atomic<juce::int64> headSamples { 0 };
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
