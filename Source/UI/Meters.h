// Meters.h — peak meters for the decks and the sum, and the mixer panel.
//
// A meter shows the two channels as bars on a dB scale (-60 to 0 dBFS),
// with a peak-hold line that stays 1.5 s and then falls. Values come from
// the Mixer through push(); MainComponent's timer calls it 30 times a second.

#pragma once

#include <JuceHeader.h>
#include "../Engine/Mixer.h"
#include "PluginHost.h"

namespace goblet
{

class PeakMeter : public juce::Component
{
public:
    explicit PeakMeter (bool vertical) : isVertical (vertical) { setInterceptsMouseClicks (false, false); }

    void push (float left, float right);     // newest block peaks, linear
    void setDimmed (bool d)                   { if (d != dimmed) { dimmed = d; repaint(); } }
    float heldDb() const noexcept             { return juce::jmax (hold[0].db, hold[1].db); }

    void paint (juce::Graphics&) override;

    static constexpr float floorDb = -60.0f;
    static float dbToFraction (float db) noexcept   { return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb); }

private:
    struct Hold { float db = -100.0f; double since = 0.0; };
    const bool isVertical;
    float levelDb[2] = { -100.0f, -100.0f };
    Hold hold[2];
    bool dimmed = false;
};

// The mixer column: one strip per deck (meter, peak readout, the two slots
// after the fader), and the sum's two slots. The faders arrive in step 4.
class MixerPanel : public juce::Component
{
public:
    MixerPanel (Mixer& m, PluginHost& host);

    void refresh();      // from MainComponent's timer
    void refreshSlots(); // a plugin was loaded, removed or bypassed

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Mixer& mixer;
    std::unique_ptr<PeakMeter> meters[Mixer::numDecks];
    juce::Label peakLabels[Mixer::numDecks];
    std::unique_ptr<SlotButton> postSlots[Mixer::numDecks][slotsPerChain], sumSlots[slotsPerChain];
    juce::Rectangle<int> stripAreas[Mixer::numDecks], faderNote, postCaption, sumArea;
    bool audible[Mixer::numDecks] = { true, true, true };
};

// The sum meter in the toolbar, with the clip lamp (click the lamp to reset it).
class SumMeter : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    explicit SumMeter (Mixer& m);

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> lampArea() const;

    Mixer& mixer;
    PeakMeter meter { false };
    bool lampLit = false;
};

} // namespace goblet
