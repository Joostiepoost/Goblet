// Mixer.cpp

#include "Mixer.h"

namespace goblet
{

bool Mixer::anyPlaying() const noexcept
{
    for (auto& d : decks)
        if (d.isPlaying()) return true;
    return false;
}

void Mixer::prepare (double sampleRate, int maximumBlockSize)
{
    outputRate.store (sampleRate);
    sum.setSize (2, juce::jmax (1, maximumBlockSize), false, false, true);
    for (auto& d : decks)
        d.prepare (sampleRate, maximumBlockSize);
}

void Mixer::process (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    if (numSamples > sum.getNumSamples())     // a device that sends a larger block than announced
        sum.setSize (2, numSamples, false, false, true);

    sum.clear (0, numSamples);
    float* s[2] = { sum.getWritePointer (0), sum.getWritePointer (1) };

    const uint32_t pa = playAllCounter.load (std::memory_order_acquire);
    const bool startAll = pa != lastPlayAll;
    lastPlayAll = pa;
    const uint32_t sa = stopAllCounter.load (std::memory_order_acquire);
    const bool endAll = sa != lastStopAll;
    lastStopAll = sa;

    const SincKernel& kernel = KernelBank::get ((Quality) quality.load());

    for (auto& d : decks)
    {
        if (startAll) d.startNow();
        if (endAll)   d.stopNow();
        d.process (s, numSamples, kernel);
    }

    if (numOutputs >= 2)
    {
        juce::FloatVectorOperations::copy (outputs[0], s[0], numSamples);
        juce::FloatVectorOperations::copy (outputs[1], s[1], numSamples);
        for (int ch = 2; ch < numOutputs; ++ch)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
    }
    else if (numOutputs == 1)
    {
        juce::FloatVectorOperations::copy (outputs[0], s[0], numSamples);
        juce::FloatVectorOperations::add (outputs[0], s[1], numSamples);
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
