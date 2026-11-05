// TakeRenderer.cpp

#include "TakeRenderer.h"
#include "CapturesPanel.h"
#include "../Engine/CaptureRecorder.h"

namespace goblet
{

static constexpr int renderBlock = 512;

TakeRenderer::TakeRenderer (juce::AudioPluginFormatManager& pf, juce::AudioFormatManager& af)
    : juce::Thread ("Goblet re-render"), pluginFormats (pf), audioFormats (af)
{
}

TakeRenderer::~TakeRenderer()
{
    *alive = false;
    stopThread (4000);
    mixer = nullptr;
}

void TakeRenderer::cancel()
{
    if (isThreadRunning()) signalThreadShouldExit();
}

//==============================================================================
void TakeRenderer::renderGesture (Mixer& m, const Gesture& g, const std::array<Mixer::Writer*, Mixer::numStreams>& raw,
                                  std::function<bool (double)> keepGoing)
{
    const auto& events = g.events;
    size_t next = 0;

    // Time 0: everything as it was when Rec was pressed. The gains jump to their
    // values, then the exact ramp states at Rec are put back (a fader may have
    // been half-way through a smoothing ramp).
    std::vector<GestureEvent> ramps;
    while (next < events.size() && events[next].time <= 0)
    {
        const auto& e = events[next++];
        if (e.kind == GestureEvent::rampState) ramps.push_back (e);
        else m.applyGestureEvent (e);
    }
    m.snapGains();
    for (auto& e : ramps) m.applyGestureEvent (e);

    // Each stream skips its own latency, as in a live take.
    int aligned = 0;
    for (int d = 0; d < Mixer::numDecks; ++d)
        aligned = juce::jmax (aligned, m.chain (Mixer::preChain (d)).getLatency() + m.chain (Mixer::postChain (d)).getLatency());
    std::array<int, Mixer::numStreams> skips {};
    skips[0] = aligned + m.chain (Mixer::sumChain).getLatency();
    for (int d = 0; d < Mixer::numDecks; ++d) skips[(size_t) (d + 1)] = aligned;
    m.startCapture (raw, skips, 1.0e6);

    juce::AudioBuffer<float> out (2, renderBlock);
    float* outs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
    int64_t pos = 0;
    const int64_t length = juce::jmax ((int64_t) 1, g.length);

    while (pos < length && m.getCaptureState() != Mixer::CaptureState::finished)
    {
        while (next < events.size() && events[next].time <= pos)
            m.applyGestureEvent (events[next++]);

        // Blocks end where the next event is due, so every event lands on its sample.
        int64_t end = juce::jmin (pos + renderBlock, length);
        if (next < events.size()) end = juce::jmin (end, juce::jmax (pos + 1, events[next].time));
        const int n = (int) (end - pos);
        m.process (outs, 2, n);
        pos = end;

        if (keepGoing && ! keepGoing ((double) pos / (double) length))
            break;
    }

    m.stopCapture();
}

//==============================================================================
bool TakeRenderer::start (const juce::File& takeFile, Quality quality, juce::String& error)
{
    if (busy) { error = "A re-render is already running."; return false; }

    if (! gesture.readFrom (Gesture::fileFor (takeFile))) { error = takeFile.getFileNameWithoutExtension() + " has no gesture to replay."; return false; }
    auto xml = juce::XmlDocument::parse (CapturesPanel::snapshotFor (takeFile));
    if (xml == nullptr) { error = takeFile.getFileNameWithoutExtension() + " has no snapshot."; return false; }
    snapshot.clear();
    snapshot.restoreFromXml (*xml);

    take = takeFile;
    auto name = take.getFileNameWithoutExtension() + " render";
    result = take.getSiblingFile (name + ".wav");
    for (int n = 2; result.exists(); ++n)
        result = take.getSiblingFile (name + " " + juce::String (n) + ".wav");

    // The second mixer: the take's files and settings, at the take's rate.
    mixer = std::make_unique<Mixer>();
    mixer->setMidiEnabled (false);
    mixer->setQuality (quality);
    mixer->prepare (gesture.sampleRate, renderBlock);

    std::array<bool, Mixer::numDecks> stems {};
    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        const auto key = [i] (const char* k) { return "deck" + juce::String::charToString ((juce::juce_wchar) ('A' + i)) + "." + k; };
        const juce::File file (snapshot.getValue (key ("file")));
        if (! file.existsAsFile()) continue;
        juce::String err;
        auto src = SourceFile::load (file, audioFormats, err);
        if (src == nullptr) { loadErrors << err << " "; continue; }
        auto& d = mixer->deck (i);
        d.setSource (src);
        d.setFragment ((int64_t) snapshot.getValue (key ("in"), "0").getLargeIntValue(),
                       (int64_t) snapshot.getValue (key ("out"), juce::String (src->length())).getLargeIntValue());
        stems[(size_t) i] = true;
    }

    // Output files: the sum and a stem per deck with a file.
    for (int s = 0; s < Mixer::numStreams; ++s)
    {
        if (s > 0 && ! stems[(size_t) (s - 1)]) continue;
        const auto file = s == 0 ? result : CaptureRecorder::stemFile (result, s - 1);
        auto w = CaptureRecorder::createWavWriter (file, gesture.sampleRate, error);
        if (w == nullptr) { writers = {}; mixer = nullptr; return false; }
        writers[(size_t) s] = std::make_unique<CaptureRecorder::DirectSink> (std::move (w));
    }

    busy = true;
    progress.store (0.0);
    loadErrors.clear();

    // The plugins, loaded the same way as at start-up; the render starts once all are in.
    pendingPlugins = 1;     // held until every load has been started
    if (auto plugins = snapshot.getXmlValue ("plugins"))
        for (auto* ce : plugins->getChildWithTagNameIterator ("CHAIN"))
        {
            const int c = ce->getIntAttribute ("id", -1);
            if (! juce::isPositiveAndBelow (c, Mixer::numChains)) continue;
            for (auto* e : ce->getChildWithTagNameIterator ("SLOT"))
                if (auto* d = e->getChildByName ("PLUGIN"))
                {
                    juce::PluginDescription desc;
                    if (! desc.loadFromXml (*d)) continue;
                    const int slot = e->getIntAttribute ("index", -1);
                    if (! juce::isPositiveAndBelow (slot, slotsPerChain)) continue;
                    auto state = std::make_shared<juce::MemoryBlock>();
                    state->fromBase64Encoding (e->getStringAttribute ("state"));
                    const bool bypass = e->getBoolAttribute ("bypass", false);
                    ++pendingPlugins;
                    std::weak_ptr<bool> stillAlive = alive;
                    pluginFormats.createPluginInstanceAsync (desc, gesture.sampleRate, renderBlock,
                        [this, stillAlive, desc, c, slot, state, bypass] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& err)
                        {
                            if (stillAlive.expired()) return;
                            if (instance == nullptr)
                                loadErrors << "Could not load " << desc.name << (err.isNotEmpty() ? ": " + err : juce::String()) << ". ";
                            else if (mixer != nullptr)
                            {
                                if (state->getSize() > 0)
                                    instance->setStateInformation (state->getData(), (int) state->getSize());
                                std::unique_ptr<juce::AudioProcessor> replaced;
                                juce::String e2;
                                mixer->chain (c).setBypass (slot, bypass);
                                if (! mixer->chain (c).insert (slot, std::move (instance), replaced, e2))
                                    loadErrors << e2 << " ";
                            }
                            pluginLoaded();
                        });
                }
        }
    pluginLoaded();     // releases the hold
    return true;
}

void TakeRenderer::pluginLoaded()
{
    if (--pendingPlugins > 0 || mixer == nullptr) return;

    for (int c = 0; c < Mixer::numChains; ++c)
        mixer->chain (c).setNonRealtime (true);
    startThread();
}

void TakeRenderer::run()
{
    std::array<Mixer::Writer*, Mixer::numStreams> raw {};
    for (int s = 0; s < Mixer::numStreams; ++s) raw[(size_t) s] = writers[(size_t) s].get();

    renderGesture (*mixer, gesture, raw, [this] (double p)
    {
        progress.store (p);
        return ! threadShouldExit();
    });

    const bool cancelled = threadShouldExit();
    std::weak_ptr<bool> stillAlive = alive;
    juce::MessageManager::callAsync ([this, stillAlive, cancelled]
    {
        if (stillAlive.expired()) return;
        stopThread (1000);
        finish (cancelled ? "Re-render cancelled." : juce::String());
    });
}

void TakeRenderer::finish (const juce::String& error)
{
    for (auto& w : writers) w = nullptr;     // closes the files
    mixer = nullptr;                          // the plugins go, on the message thread
    busy = false;

    if (error.isNotEmpty())
    {
        result.deleteFile();
        CaptureRecorder::stemsFolder (result).deleteRecursively();
    }
    else
    {
        CapturesPanel::snapshotFor (take).copyFileTo (CapturesPanel::snapshotFor (result));
    }

    if (onFinished) onFinished (error.isEmpty() ? result : juce::File(), error.isNotEmpty() ? error : loadErrors.trim());
}

} // namespace goblet
