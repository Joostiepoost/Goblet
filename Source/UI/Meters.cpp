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
MixerPanel::MixerPanel (Mixer& m, PluginHost& host) : mixer (m)
{
    for (int k = 0; k < slotsPerChain; ++k)
    {
        for (int i = 0; i < Mixer::numDecks; ++i)
        {
            postSlots[i][k] = std::make_unique<SlotButton> (host, Mixer::postChain (i), k, true);
            addAndMakeVisible (*postSlots[i][k]);
        }
        sumSlots[k] = std::make_unique<SlotButton> (host, Mixer::sumChain, k);
        addAndMakeVisible (*sumSlots[k]);
    }

    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        meters[i] = std::make_unique<PeakMeter> (true);
        addAndMakeVisible (*meters[i]);
        auto& l = peakLabels[i];
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (11.0f));
        l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
        l.setTooltip ("Peak: the highest level of the last 1.5 s, after the deck's plugins, before mute and solo");
        addAndMakeVisible (l);

        auto& f = faders[i];
        f.setSliderStyle (juce::Slider::LinearVertical);
        f.setRange (0.0, 1.0, 0.0);
        f.setValue (Mixer::unityPosition, juce::dontSendNotification);
        f.setDoubleClickReturnValue (true, Mixer::unityPosition);
        f.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        f.setWantsKeyboardFocus (false);
        f.setColour (juce::Slider::trackColourId, juce::Colour (0xff4a9eff).withAlpha (0.5f));
        f.setColour (juce::Slider::backgroundColourId, juce::Colours::black.withAlpha (0.4f));
        f.setColour (juce::Slider::thumbColourId, juce::Colour (0xffd8dde3));
        f.setTooltip ("Fader " + juce::String::charToString ((juce::juce_wchar) ('A' + i)) + ": up to +12 dB. Double-click for 0 dB; "
                      "Shift-drag for fine moves. MIDI... maps a controller to it.");
        f.onValueChange = [this, i]
        {
            mixer.setFaderPosition (i, (float) faders[i].getValue());
            updateFaderText (i);
        };
        f.onDragEnd = [this] { if (onFaderMoved) onFaderMoved(); };
        addAndMakeVisible (f);

        auto& fl = faderLabels[i];
        fl.setJustificationType (juce::Justification::centred);
        fl.setFont (juce::FontOptions (12.5f));
        fl.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
        fl.setEditable (false, true, false);
        fl.setTooltip ("Fader level in dB (double-click to type)");
        fl.onTextChange = [this, i]
        {
            const auto t = faderLabels[i].getText().replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-");
            if (t.containsIgnoreCase ("inf") || t.contains (juce::String::fromUTF8 ("\xe2\x88\x9e")))
                faders[i].setValue (0.0, juce::sendNotificationSync);
            else if (t.containsAnyOf ("0123456789"))
                faders[i].setValue (Mixer::faderDbToPosition ((float) juce::jlimit (-100.0, 12.0, t.retainCharacters ("0123456789.-+").getDoubleValue())),
                                    juce::sendNotificationSync);
            updateFaderText (i);
            if (onFaderMoved) onFaderMoved();
        };
        addAndMakeVisible (fl);
        updateFaderText (i);
    }
}

void MixerPanel::updateFaderText (int i)
{
    const float db = Mixer::faderPositionToDb ((float) faders[i].getValue());
    juce::String t;
    if (db <= -99.0f) t = juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e");
    else t = ((db > 0.05f ? "+" : "") + juce::String (db, 1)).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92"));
    if (! faderLabels[i].isBeingEdited())
        faderLabels[i].setText (t + " dB", juce::dontSendNotification);
}

void MixerPanel::refresh()
{
    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        // The fader follows a MIDI controller (or a restored setting).
        const float pos = mixer.getFaderPosition (i);
        if (! faders[i].isMouseButtonDown() && std::abs (faders[i].getValue() - (double) pos) > 1.0e-5)
        {
            faders[i].setValue (pos, juce::dontSendNotification);
            updateFaderText (i);
        }

        meters[i]->push (mixer.takeDeckPeak (i, 0), mixer.takeDeckPeak (i, 1));
        const bool a = mixer.isAudible (i);
        meters[i]->setDimmed (! a);
        if (a != audible[i]) { audible[i] = a; repaint(); }

        const float db = meters[i]->heldDb();
        peakLabels[i].setText ("pk " + (db <= -99.0f ? juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e")
                                                     : juce::String (db, 1).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92"))),
                               juce::dontSendNotification);
    }
}

void MixerPanel::refreshSlots()
{
    for (int k = 0; k < slotsPerChain; ++k)
    {
        for (int i = 0; i < Mixer::numDecks; ++i)
            postSlots[i][k]->refresh();
        sumSlots[k]->refresh();
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

    // The fader scale, beside the first fader.
    {
        const auto& f = faders[0];
        auto b = f.getBounds();
        g.setFont (juce::FontOptions (10.5f));
        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f, -100.0f })
        {
            const float y = (float) b.getY() + (float) f.getPositionOfValue (Mixer::faderDbToPosition (db));
            juce::String t = db <= -99.0f ? juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e")
                                         : (db > 0.0f ? "+" : "") + juce::String ((int) db).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92"));
            g.setColour (juce::Colours::white.withAlpha (juce::exactlyEqual (db, 0.0f) ? 0.8f : 0.45f));
            g.drawText (t, juce::Rectangle<float> ((float) stripAreas[0].getX() - 36.0f, y - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
            g.setColour (juce::Colours::white.withAlpha (juce::exactlyEqual (db, 0.0f) ? 0.35f : 0.12f));
            for (int i = 0; i < Mixer::numDecks; ++i)
            {
                auto fb = faders[i].getBounds();
                g.drawHorizontalLine ((int) y, (float) fb.getX() - 2.0f, (float) fb.getX() + 4.0f);
            }
        }
    }

    g.setFont (juce::FontOptions (11.5f));
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawText ("after the fader", postCaption, juce::Justification::centred);

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine (sumArea.getY() - 8, (float) sumArea.getX(), (float) sumArea.getRight());
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Sum", sumArea.withWidth (40), juce::Justification::centredLeft);
}

void MixerPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    r.removeFromTop (28);

    sumArea = r.removeFromBottom (28);
    {
        auto a = sumArea.withTrimmedLeft (44);
        const int w = (a.getWidth() - 6) / 2;
        sumSlots[0]->setBounds (a.removeFromLeft (w));
        a.removeFromLeft (6);
        sumSlots[1]->setBounds (a);
    }
    r.removeFromBottom (18);

    // Under each meter: the two slots after the fader, then a caption.
    postCaption = r.removeFromBottom (16).withTrimmedLeft (36);
    auto slotRows = r.removeFromBottom (54).withTrimmedLeft (36);
    r.removeFromBottom (8);

    r.removeFromLeft (36);    // the dB scale
    const int w = r.getWidth() / Mixer::numDecks;
    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        auto s = r.removeFromLeft (w);
        stripAreas[i] = s;
        s.removeFromTop (24);
        peakLabels[i].setBounds (s.removeFromBottom (16));
        faderLabels[i].setBounds (s.removeFromBottom (20));
        s.removeFromBottom (4);
        auto pair = s.withSizeKeepingCentre (48, s.getHeight());
        faders[i].setBounds (pair.removeFromLeft (28));
        pair.removeFromLeft (6);
        meters[i]->setBounds (pair.withTrimmedTop (8).withTrimmedBottom (8));

        auto col = slotRows.withX (stripAreas[i].getX()).withWidth (w).reduced (3, 0);
        postSlots[i][0]->setBounds (col.removeFromTop (25));
        col.removeFromTop (4);
        postSlots[i][1]->setBounds (col.removeFromTop (25));
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
