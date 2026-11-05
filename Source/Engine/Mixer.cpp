// Mixer.cpp

#include "Mixer.h"

namespace goblet
{

Mixer::Mixer()
{
    for (auto& c : chains)
    {
        c = std::make_unique<PluginChain>();
        c->setPlayHead (this);
    }
}

Mixer::~Mixer()
{
    // The plugins go first, while the playhead they point to still exists.
    for (auto& c : chains)
        c = nullptr;
}

int Mixer::getAlignedLatency() const noexcept
{
    int l = 0;
    for (int i = 0; i < numDecks; ++i)
        l = juce::jmax (l, getDeckLatency (i));
    return l;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> Mixer::getPosition() const
{
    PositionInfo info;
    const double rate = outputRate.load();
    const auto samples = headSamples.load();
    info.setIsPlaying (headPlaying.load());
    info.setTimeInSamples (samples);
    info.setTimeInSeconds ((double) samples / rate);
    info.setBpm (120.0);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    info.setPpqPosition ((double) samples / rate * 2.0);
    info.setIsLooping (false);
    return info;
}

bool Mixer::anyPlaying() const noexcept
{
    for (auto& d : decks)
        if (d.isPlaying()) return true;
    return false;
}

bool Mixer::isAudible (int index) const noexcept
{
    bool anySolo = false;
    for (auto& s : strips) anySolo = anySolo || s.solo.load();
    const auto& s = strips[(size_t) index];
    return ! s.mute.load() && (! anySolo || s.solo.load());
}

void Mixer::prepare (double sampleRate, int maximumBlockSize)
{
    outputRate.store (sampleRate);
    lastPlayAll = playAllCounter.load();
    lastStopAll = stopAllCounter.load();
    const int n = juce::jmax (1, maximumBlockSize);
    sum.setSize (2, n, false, false, true);
    gainStep = (float) (1.0 / juce::jmax (1.0, muteRampMs * 0.001 * sampleRate));

    for (int i = 0; i < numDecks; ++i)
    {
        decks[(size_t) i].prepare (sampleRate, maximumBlockSize);
        strips[(size_t) i].buffer.setSize (2, n, false, false, true);
        strips[(size_t) i].gain = isAudible (i) ? 1.0f : 0.0f;
        strips[(size_t) i].align.clear();
    }

    for (auto& c : chains)
        c->prepare (sampleRate, maximumBlockSize);
}

void Mixer::process (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    if (numSamples > sum.getNumSamples())     // a device that sends a larger block than announced
    {
        sum.setSize (2, numSamples, false, false, true);
        for (auto& s : strips) s.buffer.setSize (2, numSamples, false, false, true);
    }

    sum.clear (0, numSamples);
    float* out[2] = { sum.getWritePointer (0), sum.getWritePointer (1) };

    const uint32_t pa = playAllCounter.load (std::memory_order_acquire);
    const bool startAll = pa != lastPlayAll;
    lastPlayAll = pa;
    const uint32_t sa = stopAllCounter.load (std::memory_order_acquire);
    const bool endAll = sa != lastStopAll;
    lastStopAll = sa;

    const SincKernel& kernel = KernelBank::get ((Quality) quality.load());

    bool anySolo = false;
    for (auto& s : strips) anySolo = anySolo || s.solo.load();

    // Host playhead for the plugins.
    if (startAll) headSamples.store (0);
    bool playingNow = false;

    // First every deck through its player and plugins...
    for (int i = 0; i < numDecks; ++i)
    {
        auto& d = decks[(size_t) i];
        auto& s = strips[(size_t) i];

        if (startAll) d.startNow();
        if (endAll)   d.stopNow();

        s.buffer.clear (0, numSamples);
        float* buf[2] = { s.buffer.getWritePointer (0), s.buffer.getWritePointer (1) };
        d.process (buf, numSamples, kernel);
        playingNow = playingNow || d.isPlaying();

        // The deck's plugins: before the fader, (fader: step 4), after it.
        juce::AudioBuffer<float> view (buf, 2, numSamples);
        chains[(size_t) preChain (i)]->process (view, numSamples);
        chains[(size_t) postChain (i)]->process (view, numSamples);

        // Meter, before mute and solo.
        for (int ch = 0; ch < 2; ++ch)
            raise (s.peak[(size_t) ch], s.buffer.getMagnitude (ch, 0, numSamples));
    }

    // ...then aligned to the slowest deck, muted or soloed, and summed.
    const int aligned = getAlignedLatency();

    for (int i = 0; i < numDecks; ++i)
    {
        auto& s = strips[(size_t) i];
        float* buf[2] = { s.buffer.getWritePointer (0), s.buffer.getWritePointer (1) };

        s.align.process (buf, numSamples, aligned - getDeckLatency (i));

        const float target = (! s.mute.load() && (! anySolo || s.solo.load())) ? 1.0f : 0.0f;

        if (juce::exactlyEqual (s.gain, target))
        {
            if (target > 0.0f)
                for (int ch = 0; ch < 2; ++ch)
                    juce::FloatVectorOperations::add (out[ch], buf[ch], numSamples);
        }
        else
        {
            float g = s.gain;
            for (int k = 0; k < numSamples; ++k)
            {
                g = target > g ? juce::jmin (target, g + gainStep) : juce::jmax (target, g - gainStep);
                out[0][k] += g * buf[0][k];
                out[1][k] += g * buf[1][k];
            }
            s.gain = g;
        }
    }

    // The sum's plugins.
    chains[(size_t) sumChain]->process (sum, numSamples);

    headPlaying.store (playingNow);
    if (playingNow) headSamples.fetch_add (numSamples);

    // Sum meter and clip lamp.
    for (int ch = 0; ch < 2; ++ch)
    {
        const float m = sum.getMagnitude (ch, 0, numSamples);
        raise (sumPeak[(size_t) ch], m);
        if (m >= 1.0f) clipped.store (true);
    }

    if (numOutputs >= 2)
    {
        juce::FloatVectorOperations::copy (outputs[0], out[0], numSamples);
        juce::FloatVectorOperations::copy (outputs[1], out[1], numSamples);
        for (int ch = 2; ch < numOutputs; ++ch)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
    }
    else if (numOutputs == 1)
    {
        juce::FloatVectorOperations::copy (outputs[0], out[0], numSamples);
        juce::FloatVectorOperations::add (outputs[0], out[1], numSamples);
        juce::FloatVectorOperations::multiply (outputs[0], 0.5f, numSamples);
    }
}

void Mixer::audioDeviceIOCallbackWithContext (const float* const*, int,
                                              float* const* outputs, int numOutputs, int numSamples,
                                              const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    process (outputs, numOutputs, numSamples);
}

void Mixer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

} // namespace goblet
