// CaptureRecorder.h — live capture, on the message thread (adapted from Effet).
//
// A take is up to four WAV files at the device's rate, 32-bit float:
//   <name>.wav                   the sum (what you hear)
//   <name> stems/<name> A.wav    one stem per deck that has a file
//   <name>.gesture               what changed the mix, for the re-render
// It hands threaded writers to the Mixer (which writes from the audio thread
// without blocking) and closes the files once the Mixer reports the capture
// finished (after the tail).

#pragma once

#include <JuceHeader.h>
#include "Mixer.h"

namespace goblet
{

class CaptureRecorder : private juce::Timer
{
public:
    explicit CaptureRecorder (Mixer& m) : mixer (m)
    {
        writerThread.startThread();
    }

    ~CaptureRecorder() override
    {
        stopTimer();
        finishNow();
        writerThread.stopThread (2000);
    }

    static juce::File stemsFolder (const juce::File& sumFile)
    {
        return sumFile.getSiblingFile (sumFile.getFileNameWithoutExtension() + " stems");
    }

    static juce::File stemFile (const juce::File& sumFile, int deck)
    {
        return stemsFolder (sumFile).getChildFile (sumFile.getFileNameWithoutExtension() + " "
                                                   + juce::String::charToString ((juce::juce_wchar) ('A' + deck)) + ".wav");
    }

    /** Starts a take: the sum into `sumFile`, and a stem for each deck marked in `stems`. */
    bool start (const juce::File& sumFile, const std::array<bool, Mixer::numDecks>& stems, double tailLimitSeconds, juce::String& error,
                bool withGesture = true)
    {
        if (isActive()) { error = "A capture is already running."; return false; }

        std::array<Mixer::Writer*, Mixer::numStreams> raw {};
        std::array<int, Mixer::numStreams> skips {};

        for (int s = 0; s < Mixer::numStreams; ++s)
        {
            if (s > 0 && ! stems[(size_t) (s - 1)]) continue;
            const auto file = s == 0 ? sumFile : stemFile (sumFile, s - 1);
            auto w = openWriter (file, error);
            if (w == nullptr) { writers = {}; return false; }
            raw[(size_t) s] = w.get();
            writers[(size_t) s] = std::move (w);
        }

        // Each stream skips its own latency, so stems and sum start on the same sample.
        skips[0] = mixer.getTotalLatency();
        for (int d = 0; d < Mixer::numDecks; ++d)
            skips[(size_t) (d + 1)] = mixer.getAlignedLatency();

        currentFile = sumFile;
        gesture = Gesture();
        gesture.sampleRate = mixer.getOutputRate();
        recordGesture = withGesture;
        mixer.startCapture (raw, skips, tailLimitSeconds);
        startTimerHz (20);
        return true;
    }

    /** Ends the capture now, without waiting for the tail. Normally the audio thread
        does it within a block; if audio is not running, the timer does it. */
    void cut()
    {
        if (! isActive()) return;
        mixer.cutCapture();
        cutRequestedAt = juce::Time::getMillisecondCounterHiRes();
    }

    bool isActive() const                        { return writers[0] != nullptr; }
    bool isInTail() const                        { return mixer.getCaptureState() == Mixer::CaptureState::tail; }
    double getSeconds() const                    { return mixer.getCapturedSeconds(); }
    juce::File getFile() const                   { return currentFile; }

    /** Called once the files are complete and closed. */
    std::function<void (const juce::File&)> onFinished;

private:
    std::unique_ptr<Mixer::Writer> openWriter (const juce::File& file, juce::String& error)
    {
        auto writer = createWavWriter (file, mixer.getOutputRate(), error);
        if (writer == nullptr) return {};
        return std::make_unique<ThreadedSink> (writer.release(), writerThread);
    }

    // Live: writes from the audio thread without blocking, through a large buffer.
    struct ThreadedSink : Mixer::Writer
    {
        ThreadedSink (juce::AudioFormatWriter* w, juce::TimeSliceThread& t) : writer (w, t, 1 << 17) {}
        void write (const float* const* ch, int n) override   { writer.write (ch, n); }
        juce::AudioFormatWriter::ThreadedWriter writer;
    };

public:
    // Offline: writes straight to the file (for the re-render, which runs faster than real time).
    struct DirectSink : Mixer::Writer
    {
        explicit DirectSink (std::unique_ptr<juce::AudioFormatWriter> w) : writer (std::move (w)) {}
        void write (const float* const* ch, int n) override   { writer->writeFromFloatArrays (ch, 2, n); }
        std::unique_ptr<juce::AudioFormatWriter> writer;
    };

    static std::unique_ptr<juce::AudioFormatWriter> createWavWriter (const juce::File& file, double rate, juce::String& error)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        auto stream = std::make_unique<juce::FileOutputStream> (file);
        if (! stream->openedOk()) { error = "Cannot write " + file.getFullPathName(); return {}; }
        juce::WavAudioFormat wav;
        auto options = juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                        .withNumChannels (2)
                                                        .withBitsPerSample (32)
                                                        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        std::unique_ptr<juce::OutputStream> out (stream.release());
        auto writer = wav.createWriterFor (out, options);
        if (writer == nullptr) error = "Cannot create a WAV writer.";
        return writer;
    }

private:

    void drainGesture()
    {
        GestureEvent buffer[512];
        for (;;)
        {
            const int n = mixer.readGesture (buffer, 512);
            if (n <= 0) break;
            if (recordGesture) gesture.events.insert (gesture.events.end(), buffer, buffer + n);
        }
    }

    void timerCallback() override
    {
        drainGesture();
        if (mixer.getCaptureState() == Mixer::CaptureState::finished
            || (cutRequestedAt > 0.0 && juce::Time::getMillisecondCounterHiRes() - cutRequestedAt > 300.0))
            finishNow();
    }

    void finishNow()
    {
        if (! isActive()) return;
        cutRequestedAt = 0.0;
        mixer.stopCapture();
        for (auto& w : writers) w = nullptr;     // flushes and closes the files
        drainGesture();
        if (recordGesture)
        {
            gesture.length = mixer.getGestureLength();
            if (! mixer.gestureOverflowed())
                gesture.writeTo (Gesture::fileFor (currentFile));
        }
        stopTimer();
        if (onFinished) onFinished (currentFile);
    }

    Mixer& mixer;
    juce::TimeSliceThread writerThread { "Goblet capture writer" };
    std::array<std::unique_ptr<Mixer::Writer>, Mixer::numStreams> writers;
    juce::File currentFile;
    double cutRequestedAt = 0.0;
    Gesture gesture;
    bool recordGesture = true;
};

} // namespace goblet
