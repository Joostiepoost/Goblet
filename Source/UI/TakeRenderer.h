// TakeRenderer.h — the re-render: a take played again offline, at Render quality.
//
// From a take's snapshot it builds a second Mixer with the same files,
// fragments, settings and plugins (fresh instances, with the settings they had
// when Rec was pressed), then replays the take's gesture into it on a
// background thread, faster than real time. The live mixer keeps running.
// The result is a new take beside the original ("<take> render"), with its
// own stems and a copy of the snapshot, as long as the original.
//
// Identical to the live take only with plugins that behave the same on every
// run; and if the decks were already playing when Rec was pressed, the
// plugins start from silence (a reverb has no tail from before the take yet).

#pragma once

#include <JuceHeader.h>
#include "../Engine/Mixer.h"
#include "../Engine/Gesture.h"

namespace goblet
{

class TakeRenderer : private juce::Thread
{
public:
    TakeRenderer (juce::AudioPluginFormatManager& pluginFormats, juce::AudioFormatManager& audioFormats);
    ~TakeRenderer() override;

    /** Starts rendering `take` (its sum file). The plugins are loaded first, on the message thread. */
    bool start (const juce::File& take, Quality quality, juce::String& error);
    void cancel();

    bool isBusy() const noexcept                 { return busy; }
    double getProgress() const noexcept          { return progress.load(); }
    juce::File getTake() const                   { return take; }

    /** On the message thread, when done: the new take, or an error. */
    std::function<void (const juce::File& result, const juce::String& error)> onFinished;

    /** Offline, for the tests: renders `gesture` through `mixer` into `writers`. */
    static void renderGesture (Mixer& mixer, const Gesture& gesture, const std::array<Mixer::Writer*, Mixer::numStreams>& writers,
                               std::function<bool (double progress)> keepGoing = {});

private:
    void run() override;
    void pluginLoaded();
    void finish (const juce::String& error);

    juce::AudioPluginFormatManager& pluginFormats;
    juce::AudioFormatManager& audioFormats;
    std::unique_ptr<Mixer> mixer;
    Gesture gesture;
    juce::File take, result;
    juce::PropertySet snapshot;
    std::array<std::unique_ptr<Mixer::Writer>, Mixer::numStreams> writers;
    int pendingPlugins = 0;
    juce::String loadErrors;
    bool busy = false;
    std::atomic<double> progress { 0.0 };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace goblet
