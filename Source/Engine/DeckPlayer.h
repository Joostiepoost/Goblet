// DeckPlayer.h — one deck: a WAV fragment played at x1.0, looped or once.
//
// Copied from Effet's PlaybackEngine, without the score, the lanes and the
// speed: the read ratio is only the file's rate over the device's, so the
// sinc player works only when those differ. At equal rates every sample is
// passed through unchanged.
//
// Transport per deck:
//   * play() starts at the beginning of the fragment (the out point when
//     reversed), with a 3 ms fade-in so a fragment that starts mid-waveform
//     doesn't click;
//   * stop() fades out over 3 ms, then the deck waits at the start again;
//   * with Loop off the deck plays the fragment once and fades out over the
//     last 3 ms before the out point.
// Loop seam and reverse switch crossfade exactly as in Effet.
//
// The message thread calls the setters (all lock-free); the audio thread
// calls process() once per block, through the Mixer.

#pragma once

#include <JuceHeader.h>
#include "SincKernel.h"
#include "SincReader.h"
#include "SourceFile.h"

#include <atomic>
#include <memory>

namespace goblet
{

enum class CrossfadeShape { equalPower = 0, linear, sCurve, bezier };
constexpr int numCrossfadeShapes = 4;
const char* crossfadeShapeName (CrossfadeShape);

// Gains of the incoming (gIn) and outgoing (gOut) voice at progress g (0..1).
void crossfadeGains (CrossfadeShape shape, double tension, double g, double& gIn, double& gOut) noexcept;

class DeckPlayer
{
public:
    DeckPlayer();

    //== Message thread ======================================================
    void setSource (std::shared_ptr<const SourceFile> file);     // nullptr = no file
    std::shared_ptr<const SourceFile> getSource() const          { return source; }

    void setFragment (int64_t inSample, int64_t outSample);      // source samples, out exclusive
    int64_t getInPoint() const noexcept                          { return inPoint.load(); }
    int64_t getOutPoint() const noexcept                         { return outPoint.load(); }

    void setLoop (bool shouldLoop)                               { loop.store (shouldLoop); }
    bool isLooping() const noexcept                              { return loop.load(); }

    void setReverse (bool r)                                     { reverse.store (r); }
    bool isReversed() const noexcept                             { return reverse.load(); }

    void setCrossfade (double milliseconds, CrossfadeShape shape, double tension);
    double getCrossfadeMs() const noexcept                       { return fadeMs.load(); }
    CrossfadeShape getCrossfadeShape() const noexcept            { return (CrossfadeShape) fadeShape.load(); }
    double getCrossfadeTension() const noexcept                  { return fadeTension.load(); }

    void audioStopped();    // the device was closed: the deck shows as stopped
    void play();     // from the start of the fragment
    void stop();     // fade out, then back to the start

    //== Read by the UI ======================================================
    bool isPlaying() const noexcept                              { return uiPlaying.load(); }
    double getSourcePosition() const noexcept                    { return uiSourcePos.load(); }   // source samples, -1 = none

    //== Audio thread ========================================================
    void prepare (double sampleRate, int maximumBlockSize);
    void startNow() noexcept;                                    // play() for this block (Play all)
    void stopNow() noexcept;

    // Adds numSamples of stereo output to out[0] and out[1].
    void process (float* const* out, int numSamples, const SincKernel& kernel) noexcept;

    static constexpr double declickMs = 3.0;

private:
    void retire (std::shared_ptr<const void> old);

    //== Parameters (message thread -> audio thread) =========================
    std::shared_ptr<const SourceFile> source;
    std::atomic<const SourceFile*> sourcePtr { nullptr };
    std::atomic<int64_t> inPoint { 0 }, outPoint { 0 };
    std::atomic<bool> loop { true }, reverse { false };
    std::atomic<double> fadeMs { 10.0 }, fadeTension { 0.5 };
    std::atomic<int> fadeShape { (int) CrossfadeShape::equalPower };
    std::atomic<uint32_t> playCounter { 0 }, stopCounter { 0 };

    struct Retired { std::shared_ptr<const void> object; double time; };
    std::vector<Retired> retired;

    //== Audio-thread state ==================================================
    enum class State { stopped, playing, stopping };
    State state = State::stopped;
    double outputRate = 48000.0;
    const SourceFile* lastSource = nullptr;
    uint32_t lastPlay = 0, lastStop = 0;
    bool lastReverse = false;
    double q = 0.0;                    // distance travelled into the fragment, source samples
    int declickLen = 144, rampIn = 0, rampOut = 0, rampOutLen = 1;

    struct Fade { double pos = 0.0; int dir = 1; int remaining = 0; int length = 1; } fade;

    SincReader reader;

    //== Audio thread -> UI ==================================================
    std::atomic<bool> uiPlaying { false };
    std::atomic<double> uiSourcePos { -1.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckPlayer)
};

} // namespace goblet
