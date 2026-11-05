// Meters.cpp

#include "Meters.h"

namespace goblet
{

static const juce::Colour meterGreen (0xff4a9eff), meterHot (0xffe0b03c), meterRed (0xffe0483c);

//==============================================================================
void PeakMeter::push (float left, float right)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float in[2] = { left, right };
    bool changed = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = juce::Decibels::gainToDecibels (in[ch], -100.0f);

        // Bars rise at once and fall at about 26 dB per second (30 frames).
        const float fallen = levelDb[ch] - 0.87f;
        const float next = juce::jmax (db, fallen);
        if (std::abs (next - levelDb[ch]) > 0.01f) { levelDb[ch] = next; changed = true; }

        auto& h = hold[ch];
        if (db >= h.db) { h.db = db; h.since = now; changed = true; }
        else if (now - h.since > 1500.0 && h.db > -100.0f) { h.db = juce::jmax (-100.0f, h.db - 1.5f); changed = true; }
    }

    if (changed) repaint();
}

void PeakMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 2.0f);

    const float gap = 2.0f;
    const float alpha = dimmed ? 0.35f : 1.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        juce::Rectangle<float> bar;
        if (isVertical) bar = juce::Rectangle<float> (r.getX() + (float) ch * (r.getWidth() + gap) * 0.5f, r.getY(), (r.getWidth() - gap) * 0.5f, r.getHeight());
        else            bar = juce::Rectangle<float> (r.getX(), r.getY() + (float) ch * (r.getHeight() + gap) * 0.5f, r.getWidth(), (r.getHeight() - gap) * 0.5f);

        const float f = dbToFraction (levelDb[ch]);
        const float hf = dbToFraction (hold[ch].db);

        // Bar in three zones: up to -12 dB, -12 to -3 dB, above -3 dB.
        auto zone = [&] (float from, float to, juce::Colour c)
        {
            const float a = juce::jmin (f, dbToFraction (from)), b = juce::jmin (f, dbToFraction (to));
            if (b <= a) return;
            g.setColour (c.withAlpha (0.85f * alpha));
            if (isVertical) g.fillRect (bar.getX(), bar.getBottom() - b * bar.getHeight(), bar.getWidth(), (b - a) * bar.getHeight());
            else            g.fillRect (bar.getX() + a * bar.getWidth(), bar.getY(), (b - a) * bar.getWidth(), bar.getHeight());
        };
        zone (floorDb, -12.0f, meterGreen);
        zone (-12.0f, -3.0f, meterHot);
        zone (-3.0f, 0.0f, meterRed);

        if (hf > 0.0f)
        {
            g.setColour ((hold[ch].db >= 0.0f ? meterRed : juce::Colours::white).withAlpha (0.9f * alpha));
            if (isVertical) g.fillRect (bar.getX(), bar.getBottom() - hf * bar.getHeight() - 1.0f, bar.getWidth(), 2.0f);
            else            g.fillRect (bar.getX() + hf * bar.getWidth() - 1.0f, bar.getY(), 2.0f, bar.getHeight());
        }
    }
}

//==============================================================================
MixerPanel::MixerPanel (Mixer& m) : mixer (m)
{
    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        meters[i] = std::make_unique<PeakMeter> (true);
        addAndMakeVisible (*meters[i]);
        auto& l = peakLabels[i];
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (12.5f));
        l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        l.setTooltip ("Highest peak of the last 1.5 s, before mute and solo");
        addAndMakeVisible (l);
    }
}

void MixerPanel::refresh()
{
    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        meters[i]->push (mixer.takeDeckPeak (i, 0), mixer.takeDeckPeak (i, 1));
        const bool a = mixer.isAudible (i);
        meters[i]->setDimmed (! a);
        if (a != audible[i]) { audible[i] = a; repaint(); }

        const float db = meters[i]->heldDb();
        peakLabels[i].setText (db <= -99.0f ? juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e")
                                            : juce::String (db, 1).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92")),
                               juce::dontSendNotification);
    }
}

void MixerPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff23272b));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("Mixer", getLocalBounds().reduced (14, 10).removeFromTop (20), juce::Justification::topLeft);

    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (audible[i] ? 1.0f : 0.35f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)), stripAreas[i].withHeight (20), juce::Justification::centred);
    }

    // dB scale beside the first meter.
    if (meters[0] != nullptr)
    {
        auto m = meters[0]->getBounds();
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            const float y = (float) m.getBottom() - PeakMeter::dbToFraction (db) * (float) m.getHeight();
            g.drawText (juce::exactlyEqual (db, 0.0f) ? juce::String ("0") : juce::String ((int) db).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92")),
                        juce::Rectangle<float> ((float) m.getX() - 34.0f, y - 6.0f, 28.0f, 12.0f), juce::Justification::centredRight);
        }
    }

    // Where steps 3 and 4 will go.
    auto placeholder = [&g] (juce::Rectangle<int> a, const juce::String& text)
    {
        const float dashes[] = { 4.0f, 4.0f };
        juce::Path p, d;
        p.addRoundedRectangle (a.toFloat().reduced (0.5f), 6.0f);
        juce::PathStrokeType (1.0f).createDashedStroke (d, p, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillPath (d);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (text, a.reduced (8, 4), juce::Justification::centred, 2);
    };
    placeholder (slotsArea, "Faders: step 4. Post-fader slots: step 3");
    placeholder (sumArea, "Sum slots: step 3");
}

void MixerPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    r.removeFromTop (28);
    sumArea = r.removeFromBottom (40);
    r.removeFromBottom (10);
    slotsArea = r.removeFromBottom (78);
    r.removeFromBottom (10);

    r.removeFromLeft (36);    // the dB scale
    const int w = r.getWidth() / Mixer::numDecks;
    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        auto s = r.removeFromLeft (w);
        stripAreas[i] = s;
        s.removeFromTop (24);
        peakLabels[i].setBounds (s.removeFromBottom (20));
        s.removeFromBottom (4);
        meters[i]->setBounds (s.withSizeKeepingCentre (18, s.getHeight()));
    }
}

//==============================================================================
SumMeter::SumMeter (Mixer& m) : mixer (m)
{
    addAndMakeVisible (meter);
    setTooltip ("The sum: what you hear. The lamp lights when the sum reaches 0 dBFS; click it to reset.");
}

void SumMeter::refresh()
{
    meter.push (mixer.takeSumPeak (0), mixer.takeSumPeak (1));
    const bool lit = mixer.hasClipped();
    if (lit != lampLit) { lampLit = lit; repaint(); }
}

juce::Rectangle<int> SumMeter::lampArea() const
{
    return getLocalBounds().removeFromRight (16).withSizeKeepingCentre (12, 12);
}

void SumMeter::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("Sum", getLocalBounds().removeFromLeft (30), juce::Justification::centredLeft);

    auto lamp = lampArea().toFloat();
    if (lampLit) { g.setColour (meterRed); g.fillEllipse (lamp); }
    g.setColour (meterRed.withAlpha (lampLit ? 1.0f : 0.7f));
    g.drawEllipse (lamp.reduced (0.5f), 1.2f);
}

void SumMeter::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (34);
    r.removeFromRight (24);
    meter.setBounds (r.withSizeKeepingCentre (r.getWidth(), 10));
}

void SumMeter::mouseDown (const juce::MouseEvent& e)
{
    if (lampArea().expanded (4).contains (e.getPosition()))
    {
        mixer.resetClip();
        lampLit = false;
        repaint();
    }
}

} // namespace goblet
