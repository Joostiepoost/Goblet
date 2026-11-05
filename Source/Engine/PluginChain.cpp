// PluginChain.cpp

#include "PluginChain.h"

namespace goblet
{

static void delayLine (juce::AudioBuffer<float>& buffer, int& pos, float* const* ch, int numSamples, int delay) noexcept
{
    if (delay <= 0) return;
    const int size = PluginChain::maxDelay;
    delay = juce::jmin (delay, size - 1);
    const int mask = size - 1;
    float* b0 = buffer.getWritePointer (0);
    float* b1 = buffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const int r = (pos - delay) & mask;
        b0[pos] = ch[0][i];
        b1[pos] = ch[1][i];
        ch[0][i] = b0[r];
        ch[1][i] = b1[r];
        pos = (pos + 1) & mask;
    }
}

void PluginChain::Delay::process (float* const* ch, int numSamples, int delay) noexcept
{
    delayLine (buffer, pos, ch, numSamples, delay);
}

void AlignDelay::process (float* const* ch, int numSamples, int delay) noexcept
{
    delayLine (buffer, pos, ch, numSamples, delay);
}

//==============================================================================
PluginChain::PluginChain()
{
    for (auto& s : slots)
        s.dryDelay.allocate();
    prepare (sampleRate, blockSize);
}

PluginChain::~PluginChain()
{
    for (auto& s : slots)
        if (s.processor != nullptr)
            s.processor->releaseResources();
}

bool PluginChain::configureBuses (juce::AudioProcessor& p)
{
    // Only the main buses are set (stereo if possible). Extra buses (sidechains)
    // stay enabled at their default layout and receive silence: an AU plugin
    // still asks the host for its sidechain audio when that bus is disabled, and
    // some (FabFilter) then read from an empty buffer and crash.
    using Set = juce::AudioChannelSet;
    const int numIn = p.getBusCount (true), numOut = p.getBusCount (false);
    if (numOut == 0) return false;

    const auto current = p.getBusesLayout();

    auto attempt = [&] (const Set& in, const Set& out)
    {
        auto l = current;
        for (int i = 1; i < numIn; ++i)
            if (auto* bus = p.getBus (true, i))
                if (l.inputBuses.getReference (i).isDisabled())
                    l.inputBuses.getReference (i) = bus->getDefaultLayout();
        if (numIn > 0) l.inputBuses.getReference (0) = in;
        l.outputBuses.getReference (0) = out;
        return p.setBusesLayout (l);
    };

    if (numIn > 0)
        return attempt (Set::stereo(), Set::stereo()) || attempt (Set::mono(), Set::stereo())
            || attempt (Set::mono(), Set::mono()) || p.getMainBusNumOutputChannels() > 0;

    // A generator (no input): stereo out, or whatever it offers.
    return attempt ({}, Set::stereo()) || attempt ({}, Set::mono()) || p.getMainBusNumOutputChannels() > 0;
}

void PluginChain::prepareProcessor (juce::AudioProcessor& p, std::unique_ptr<juce::AudioBuffer<float>>& bufferOut) const
{
    p.setPlayHead (playHead);
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
    const int channels = juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    bufferOut = std::make_unique<juce::AudioBuffer<float>> (channels, blockSize);
    bufferOut->clear();
}

void PluginChain::prepare (double rate, int maximumBlockSize)
{
    const juce::SpinLock::ScopedLockType sl (swapLock);
    sampleRate = rate;
    blockSize = juce::jmax (32, maximumBlockSize);

    for (auto& s : slots)
    {
        s.dry.setSize (2, blockSize);
        s.midi.ensureSize (1024);
        s.dryDelay.clear();
        s.wet.reset (sampleRate, 0.02);
        s.wet.setCurrentAndTargetValue (s.bypass.load() ? 0.0f : 1.0f);

        if (s.processor != nullptr)
        {
            s.processor->releaseResources();
            prepareProcessor (*s.processor, s.buffer);
            s.inputs = s.processor->getMainBusNumInputChannels();
            s.outputs = juce::jmax (1, s.processor->getMainBusNumOutputChannels());
        }
    }
}

bool PluginChain::insert (int slot, std::unique_ptr<juce::AudioProcessor> p,
                          std::unique_ptr<juce::AudioProcessor>& replaced, juce::String& error)
{
    if (! juce::isPositiveAndBelow (slot, slotsPerChain) || p == nullptr)
    {
        error = "No such slot.";
        return false;
    }

    if (! configureBuses (*p) || p->getTotalNumOutputChannels() == 0)
    {
        error = p->getName() + " has no audio output Goblet can use.";
        return false;
    }

    std::unique_ptr<juce::AudioBuffer<float>> buffer;
    prepareProcessor (*p, buffer);
    auto& s = slots[(size_t) slot];

    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        std::swap (s.processor, p);
        std::swap (s.buffer, buffer);
        s.inputs = s.processor->getMainBusNumInputChannels();
        s.outputs = juce::jmax (1, s.processor->getMainBusNumOutputChannels());
        s.dryDelay.clear();
    }

    if (p != nullptr) p->releaseResources();
    replaced = std::move (p);
    return true;
}

std::unique_ptr<juce::AudioProcessor> PluginChain::remove (int slot)
{
    if (! juce::isPositiveAndBelow (slot, slotsPerChain)) return {};

    std::unique_ptr<juce::AudioProcessor> old;
    std::unique_ptr<juce::AudioBuffer<float>> oldBuffer;
    auto& s = slots[(size_t) slot];
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        std::swap (old, s.processor);
        std::swap (oldBuffer, s.buffer);
        s.dryDelay.clear();
    }
    if (old != nullptr) old->releaseResources();
    return old;
}

void PluginChain::swapSlots()
{
    auto& a = slots[0];
    auto& b = slots[1];
    const juce::SpinLock::ScopedLockType sl (swapLock);
    std::swap (a.processor, b.processor);
    std::swap (a.buffer, b.buffer);
    std::swap (a.inputs, b.inputs);
    std::swap (a.outputs, b.outputs);
    const bool t = a.bypass.load();
    a.bypass.store (b.bypass.load());
    b.bypass.store (t);
    a.wet.setCurrentAndTargetValue (a.bypass.load() ? 0.0f : 1.0f);
    b.wet.setCurrentAndTargetValue (b.bypass.load() ? 0.0f : 1.0f);
    a.dryDelay.clear();
    b.dryDelay.clear();
}

juce::AudioProcessor* PluginChain::get (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, slotsPerChain)) return nullptr;
    return slots[(size_t) slot].processor.get();
}

int PluginChain::getLatency() const noexcept
{
    int l = 0;
    for (auto& s : slots)
        if (s.processor != nullptr) l += s.processor->getLatencySamples();
    return l;
}

int PluginChain::numPlugins() const noexcept
{
    int n = 0;
    for (auto& s : slots) if (s.processor != nullptr) ++n;
    return n;
}

void PluginChain::setNonRealtime (bool offline)
{
    const juce::SpinLock::ScopedLockType sl (swapLock);
    for (auto& s : slots)
        if (s.processor != nullptr)
            s.processor->setNonRealtime (offline);
}

std::unique_ptr<juce::XmlElement> PluginChain::createXml (const juce::String& tag, bool withState) const
{
    auto xml = std::make_unique<juce::XmlElement> (tag);

    for (int k = 0; k < slotsPerChain; ++k)
    {
        const auto& s = slots[(size_t) k];
        auto* plugin = dynamic_cast<juce::AudioPluginInstance*> (s.processor.get());
        if (plugin == nullptr) continue;

        auto* e = xml->createNewChildElement ("SLOT");
        e->setAttribute ("index", k);
        e->setAttribute ("bypass", s.bypass.load());
        if (auto desc = plugin->getPluginDescription().createXml())
            e->addChildElement (desc.release());
        if (withState)
        {
            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            e->setAttribute ("state", state.toBase64Encoding());
        }
    }

    return xml;
}

//==============================================================================
void PluginChain::setParameter (int slot, int index, float value) noexcept
{
    const juce::SpinLock::ScopedTryLockType tl (swapLock);
    if (! tl.isLocked() || ! juce::isPositiveAndBelow (slot, slotsPerChain)) return;
    if (auto* p = slots[(size_t) slot].processor.get())
    {
        const auto& params = p->getParameters();
        if (juce::isPositiveAndBelow (index, params.size()))
            params.getUnchecked (index)->setValue (juce::jlimit (0.0f, 1.0f, value));
    }
}

float PluginChain::getParameter (int slot, int index) const noexcept
{
    const juce::SpinLock::ScopedTryLockType tl (swapLock);
    if (! tl.isLocked() || ! juce::isPositiveAndBelow (slot, slotsPerChain)) return -1.0f;
    if (auto* p = slots[(size_t) slot].processor.get())
    {
        const auto& params = p->getParameters();
        if (juce::isPositiveAndBelow (index, params.size()))
            return params.getUnchecked (index)->getValue();
    }
    return -1.0f;
}

void PluginChain::process (juce::AudioBuffer<float>& io, int numSamples) noexcept
{
    const juce::SpinLock::ScopedTryLockType tl (swapLock);

    if (! tl.isLocked())
    {
        io.clear (0, numSamples);   // a plugin is being swapped in this very moment
        return;
    }

    int latency = 0;
    for (auto& s : slots)
        if (s.processor != nullptr) latency += s.processor->getLatencySamples();
    processedLatency.store (latency);

    for (int start = 0; start < numSamples; start += blockSize)
    {
        const int n = juce::jmin (blockSize, numSamples - start);
        for (auto& s : slots)
            if (s.processor != nullptr && s.buffer != nullptr)
                processSlot (s, io, start, n);
    }
}

void PluginChain::processSlot (Slot& s, juce::AudioBuffer<float>& io, int start, int n) noexcept
{
    float* io0 = io.getWritePointer (0, start);
    float* io1 = io.getWritePointer (1, start);
    const int latency = s.processor->getLatencySamples();

    // Dry path, delayed by the plugin's latency, for the bypass crossfade.
    s.dry.copyFrom (0, 0, io0, n);
    s.dry.copyFrom (1, 0, io1, n);
    float* dry[2] = { s.dry.getWritePointer (0), s.dry.getWritePointer (1) };
    s.dryDelay.process (dry, n, latency);

    auto& buf = *s.buffer;
    const int channels = buf.getNumChannels();
    for (int ch = 0; ch < channels; ++ch) buf.clear (ch, 0, n);

    if (s.inputs >= 2)
    {
        buf.copyFrom (0, 0, io0, n);
        buf.copyFrom (1, 0, io1, n);
    }
    else if (s.inputs == 1)
    {
        buf.copyFrom (0, 0, io0, n, 0.5f);
        buf.addFrom (0, 0, io1, n, 0.5f);
    }

    juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), channels, n);
    s.midi.clear();
    {
        const juce::ScopedLock pl (s.processor->getCallbackLock());
        if (s.processor->isSuspended()) view.clear();
        else                            s.processor->processBlock (view, s.midi);
    }

    const float* p0 = buf.getReadPointer (0);
    const float* p1 = buf.getReadPointer (s.outputs >= 2 ? 1 : 0);
    s.wet.setTargetValue (s.bypass.load() ? 0.0f : 1.0f);

    if (! s.wet.isSmoothing() && juce::exactlyEqual (s.wet.getTargetValue(), 1.0f))
    {
        juce::FloatVectorOperations::copy (io0, p0, n);
        juce::FloatVectorOperations::copy (io1, p1, n);
    }
    else if (! s.wet.isSmoothing())
    {
        juce::FloatVectorOperations::copy (io0, dry[0], n);
        juce::FloatVectorOperations::copy (io1, dry[1], n);
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            const float w = s.wet.getNextValue();
            io0[i] = dry[0][i] + w * (p0[i] - dry[0][i]);
            io1[i] = dry[1][i] + w * (p1[i] - dry[1][i]);
        }
    }
}

} // namespace goblet
