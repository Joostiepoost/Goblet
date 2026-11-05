// DeckView.cpp

#include "DeckView.h"
#include "TimeFormat.h"

namespace goblet
{

static const juce::Colour toggleOn (0xff2f6fb8);

// The trim knob: an arc from the centre (0 dB) to the value, filling its bounds.
struct DeckView::TrimKnobLook : juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
        const float radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 1.5f;
        const auto c = b.getCentre();
        const float mid = 0.5f * (startAngle + endAngle);
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (s.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (std::abs (angle - mid) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, radius, radius, 0.0f, juce::jmin (mid, angle), juce::jmax (mid, angle), true);
            g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (value, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const juce::Point<float> tip (c.x + (radius - 3.0f) * std::sin (angle), c.y - (radius - 3.0f) * std::cos (angle));
        g.setColour (s.findColour (juce::Slider::thumbColourId));
        g.drawLine (juce::Line<float> (c, tip), 2.0f);
    }
};

static void setupField (juce::Label& l)
{
    l.setEditable (false, true, false);   // double-click to type
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.25f));
    l.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.2f));
}

static juce::String playText (bool playing)
{
    return playing ? juce::String::fromUTF8 ("\xe2\x96\xa0 Stop") : juce::String::fromUTF8 ("\xe2\x96\xb6 Play");
}

DeckView::DeckView (int deckIndex, Mixer& m, juce::AudioFormatManager& f, PluginHost& host)
    : index (deckIndex), mixer (m), player (m.deck (deckIndex)), formats (f),
      slot1 (host, Mixer::preChain (deckIndex), 0), slot2 (host, Mixer::preChain (deckIndex), 1)
{
    addAndMakeVisible (strip);
    addAndMakeVisible (slot1);
    addAndMakeVisible (slot2);

    trimKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    trimLook = std::make_unique<TrimKnobLook>();
    trimKnob.setLookAndFeel (trimLook.get());
    trimKnob.setRange (-Mixer::trimRangeDb, Mixer::trimRangeDb, 0.1);
    trimKnob.setValue (0.0, juce::dontSendNotification);
    trimKnob.setDoubleClickReturnValue (true, 0.0);
    trimKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    trimKnob.setMouseDragSensitivity (300);
    trimKnob.setWantsKeyboardFocus (false);
    trimKnob.setColour (juce::Slider::rotarySliderFillColourId, toggleOn.brighter (0.3f));
    trimKnob.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::white.withAlpha (0.22f));
    trimKnob.setColour (juce::Slider::thumbColourId, juce::Colours::white.withAlpha (0.9f));
    trimKnob.setTooltip ("Trim: raises or lowers deck " + letter() + " by up to 24 dB before its plugins and fader, so files of "
                         "very different loudness arrive at similar levels. Drag up/down (Shift: finer), double-click for 0 dB.");
    trimKnob.onValueChange = [this]
    {
        mixer.setTrimDb (index, (float) trimKnob.getValue());
        updateTrimText();
    };
    trimKnob.onDragEnd = [this] { changed(); };
    trimKnob.setVelocityBasedMode (false);
    addAndMakeVisible (trimKnob);

    trimCaption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    trimCaption.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (trimCaption);
    trimValue.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    trimValue.setJustificationType (juce::Justification::centredLeft);
    trimValue.setEditable (false, true, false);
    trimValue.setTooltip ("Trim in dB (double-click to type)");
    trimValue.onTextChange = [this]
    {
        const auto t = trimValue.getText().replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-").retainCharacters ("0123456789.-+");
        if (t.containsAnyOf ("0123456789"))
            trimKnob.setValue (juce::jlimit ((double) -Mixer::trimRangeDb, (double) Mixer::trimRangeDb, t.getDoubleValue()), juce::sendNotificationSync);
        updateTrimText();
        changed();
    };
    addAndMakeVisible (trimValue);
    updateTrimText();
    strip.onFragmentChanged = [this] (int64_t in, int64_t out) { setFragment (in, out); };
    strip.onFragmentDragEnded = [this] { changed(); };

    playButton.setButtonText (playText (false));
    playButton.setTooltip ("Play this deck from the start of its fragment, or stop it (key " + juce::String (index + 1) + ")");
    playButton.onClick = [this] { togglePlay(); };

    openButton.setTooltip ("Load a WAV or AIFF file into deck " + letter() + " (or drop one on the deck)");
    openButton.onClick = [this] { openFileDialog(); };

    loopButton.setClickingTogglesState (true);
    loopButton.setToggleState (true, juce::dontSendNotification);
    loopButton.setTooltip ("On: the fragment repeats. Off: it plays once and stops at the out point.");
    loopButton.onClick = [this] { player.setLoop (loopButton.getToggleState()); changed(); };

    reverseButton.setClickingTogglesState (true);
    reverseButton.setTooltip ("Play the fragment backwards (switching while playing crossfades)");
    reverseButton.onClick = [this] { player.setReverse (reverseButton.getToggleState()); changed(); };

    muteButton.setClickingTogglesState (true);
    muteButton.setTooltip ("Mute deck " + letter() + " (Shift-" + juce::String (index + 1) + "). The deck keeps playing, silently, and its meter keeps moving.");
    muteButton.onClick = [this] { mixer.setMute (index, muteButton.getToggleState()); changed(); };

    soloButton.setClickingTogglesState (true);
    soloButton.setTooltip ("Solo deck " + letter() + " (Option-" + juce::String (index + 1) + "): only soloed decks are heard. Mute wins over solo.");
    soloButton.onClick = [this] { mixer.setSolo (index, soloButton.getToggleState()); changed(); };

    for (auto* b : { &openButton, &playButton, &muteButton, &soloButton, &loopButton, &reverseButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonOnColourId, toggleOn);
        addAndMakeVisible (b);
    }
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc0632b));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd4a72c));
    soloButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);

    for (auto* l : { &inField, &outField, &fadeField })
    {
        setupField (*l);
        addAndMakeVisible (l);
    }
    for (auto* l : { &inCaption, &outCaption, &fadeCaption, &tensionCaption })
    {
        l->setJustificationType (juce::Justification::centredRight);
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (l);
    }

    inField.setTooltip ("In point (double-click to type, m:ss.mmm or seconds)");
    outField.setTooltip ("Out point (double-click to type)");
    fadeField.setTooltip ("Crossfade at the loop seam, on restart and when switching reverse (0-200 ms)");

    auto timeField = [this] (juce::Label& field, bool isIn)
    {
        field.onTextChange = [this, &field, isIn]
        {
            auto s = player.getSource();
            const double t = parseTime (field.getText());
            if (s == nullptr || t < 0.0) { updateFields(); return; }

            const auto sample = (int64_t) std::llround (t * s->sampleRate);
            const int64_t minLen = (int64_t) (0.010 * s->sampleRate);
            int64_t in = player.getInPoint(), out = player.getOutPoint();
            if (isIn) in = juce::jlimit ((int64_t) 0, juce::jmax ((int64_t) 0, out - minLen), sample);
            else      out = juce::jlimit (in + minLen, s->length(), sample);
            setFragment (in, out);
            changed();
        };
    };
    timeField (inField, true);
    timeField (outField, false);

    for (int i = 0; i < numCrossfadeShapes; ++i)
        fadeShapeBox.addItem (crossfadeShapeName ((CrossfadeShape) i), i + 1);
    fadeShapeBox.setSelectedId (1, juce::dontSendNotification);
    fadeShapeBox.setTooltip ("Shape of the crossfade");
    fadeShapeBox.setWantsKeyboardFocus (false);
    fadeShapeBox.onChange = [this] { applyCrossfade(); changed(); };
    addAndMakeVisible (fadeShapeBox);

    tension.setSliderStyle (juce::Slider::LinearHorizontal);
    tension.setRange (0.0, 1.0, 0.0);
    tension.setValue (0.5, juce::dontSendNotification);
    tension.setDoubleClickReturnValue (true, 0.5);
    tension.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    tension.setTooltip ("Bezier tension: 0 starts slowly, 1 rises fast");
    tension.setWantsKeyboardFocus (false);
    tension.onValueChange = [this] { applyCrossfade(); };
    tension.onDragEnd = [this] { changed(); };
    addAndMakeVisible (tension);

    fadeField.setText ("10.0", juce::dontSendNotification);
    fadeField.onTextChange = [this] { applyCrossfade(); changed(); };

    applyCrossfade();
    updateFields();
}

DeckView::~DeckView()
{
    trimKnob.setLookAndFeel (nullptr);
}

//==============================================================================
void DeckView::openFileDialog()
{
    auto s = player.getSource();
    chooser = std::make_unique<juce::FileChooser> ("Load a sound file into deck " + letter(),
                                                   s != nullptr ? s->file.getParentDirectory() : juce::File(),
                                                   formats.getWildcardForAllFormats());
    juce::Component::SafePointer<DeckView> safeThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (safeThis != nullptr && f.existsAsFile()) safeThis->loadFile (f);
                          });
}

void DeckView::loadFile (const juce::File& file)
{
    juce::String error;
    auto src = SourceFile::load (file, formats, error);

    if (src == nullptr)
    {
        status (error);
        return;
    }

    player.stop();
    player.setSource (src);
    strip.setSource (src);
    setFragment (0, src->length());
    status ("Deck " + letter() + ": " + src->name + ", " + juce::String (src->sampleRate / 1000.0, 1) + " kHz, "
            + juce::String (src->bitDepth) + "-bit, " + formatTime (src->seconds()));
    repaint();
    changed();
}

void DeckView::togglePlay()
{
    if (player.getSource() == nullptr) { status ("Deck " + letter() + " has no file yet."); return; }
    if (player.isPlaying()) player.stop();
    else
    {
        if (onPlayRequested) onPlayRequested();
        player.play();
    }
    refresh();
}

void DeckView::updateTrimText()
{
    const double db = trimKnob.getValue();
    juce::String t = (db > 0.05 ? "+" : "") + juce::String (db, 1);
    if (! trimValue.isBeingEdited())
        trimValue.setText (t.replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92")) + " dB", juce::dontSendNotification);
}

void DeckView::refreshSlots()
{
    slot1.refresh();
    slot2.refresh();
}

void DeckView::toggleMute()
{
    muteButton.setToggleState (! muteButton.getToggleState(), juce::sendNotification);
}

void DeckView::toggleSolo()
{
    soloButton.setToggleState (! soloButton.getToggleState(), juce::sendNotification);
}

void DeckView::setFragment (int64_t in, int64_t out)
{
    player.setFragment (in, out);
    strip.setFragment (player.getInPoint(), player.getOutPoint());
    updateFields();
}

void DeckView::updateFields()
{
    if (auto s = player.getSource())
    {
        inField.setText (formatTime ((double) player.getInPoint() / s->sampleRate), juce::dontSendNotification);
        outField.setText (formatTime ((double) player.getOutPoint() / s->sampleRate), juce::dontSendNotification);
    }
    else
    {
        inField.setText ("-", juce::dontSendNotification);
        outField.setText ("-", juce::dontSendNotification);
    }
}

void DeckView::applyCrossfade()
{
    const double ms = juce::jlimit (0.0, 200.0, fadeField.getText().getDoubleValue());
    const auto shape = (CrossfadeShape) juce::jmax (0, fadeShapeBox.getSelectedId() - 1);
    player.setCrossfade (ms, shape, tension.getValue());
    fadeField.setText (juce::String (ms, 1), juce::dontSendNotification);
    const bool bezier = shape == CrossfadeShape::bezier;
    tension.setEnabled (bezier);
    tensionCaption.setEnabled (bezier);
}

void DeckView::refresh()
{
    strip.setSourcePosition (player.getSourcePosition());

    const bool playing = player.isPlaying();
    if (playing != shownPlaying)
    {
        shownPlaying = playing;
        playButton.setButtonText (playText (playing));
        playButton.setToggleState (playing, juce::dontSendNotification);
    }

    // The trim follows a MIDI controller.
    const float trimNow = mixer.getTrimDb (index);
    if (! trimKnob.isMouseButtonDown() && std::abs (trimKnob.getValue() - (double) trimNow) > 0.049)
    {
        trimKnob.setValue (trimNow, juce::dontSendNotification);
        updateTrimText();
    }

    // Silenced by mute or by another deck's solo: the deck's name dims.
    const bool audible = mixer.isAudible (index);
    if (audible != shownAudible)
    {
        shownAudible = audible;
        repaint();
    }
}

//==============================================================================
void DeckView::writeState (juce::PropertySet& s) const
{
    auto src = player.getSource();
    s.setValue (key ("file"), src != nullptr ? src->file.getFullPathName() : juce::String());
    s.setValue (key ("in"), juce::String (player.getInPoint()));
    s.setValue (key ("out"), juce::String (player.getOutPoint()));
    s.setValue (key ("loop"), player.isLooping());
    s.setValue (key ("reverse"), player.isReversed());
    s.setValue (key ("mute"), mixer.isMuted (index));
    s.setValue (key ("trim"), (double) mixer.getTrimDb (index));
    s.setValue (key ("fader"), (double) mixer.getFaderPosition (index));
    s.setValue (key ("solo"), mixer.isSoloed (index));
    s.setValue (key ("fadeMs"), player.getCrossfadeMs());
    s.setValue (key ("fadeShape"), (int) player.getCrossfadeShape());
    s.setValue (key ("fadeTension"), player.getCrossfadeTension());
}

void DeckView::readState (const juce::PropertySet& s)
{
    loopButton.setToggleState (s.getBoolValue (key ("loop"), true), juce::dontSendNotification);
    player.setLoop (loopButton.getToggleState());
    reverseButton.setToggleState (s.getBoolValue (key ("reverse"), false), juce::dontSendNotification);
    player.setReverse (reverseButton.getToggleState());
    trimKnob.setValue (s.getDoubleValue (key ("trim"), 0.0), juce::dontSendNotification);
    mixer.setTrimDb (index, (float) trimKnob.getValue());
    updateTrimText();
    mixer.setFaderPosition (index, (float) s.getDoubleValue (key ("fader"), Mixer::unityPosition));
    muteButton.setToggleState (s.getBoolValue (key ("mute"), false), juce::dontSendNotification);
    mixer.setMute (index, muteButton.getToggleState());
    soloButton.setToggleState (s.getBoolValue (key ("solo"), false), juce::dontSendNotification);
    mixer.setSolo (index, soloButton.getToggleState());

    fadeField.setText (juce::String (s.getDoubleValue (key ("fadeMs"), 10.0), 1), juce::dontSendNotification);
    fadeShapeBox.setSelectedId (juce::jlimit (0, numCrossfadeShapes - 1, s.getIntValue (key ("fadeShape"), 0)) + 1, juce::dontSendNotification);
    tension.setValue (s.getDoubleValue (key ("fadeTension"), 0.5), juce::dontSendNotification);
    applyCrossfade();

    const juce::File file (s.getValue (key ("file")));
    if (file.existsAsFile())
    {
        juce::String error;
        if (auto src = SourceFile::load (file, formats, error))
        {
            player.setSource (src);
            strip.setSource (src);
            const auto in = (int64_t) s.getValue (key ("in"), "0").getLargeIntValue();
            const auto out = (int64_t) s.getValue (key ("out"), juce::String (src->length())).getLargeIntValue();
            setFragment (in, out > in ? out : src->length());
        }
        else status (error);
    }
    else if (file != juce::File())
    {
        status ("Deck " + letter() + ": " + file.getFileName() + " was not found.");
    }
    repaint();
}

//==============================================================================
void DeckView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff23272b));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (dragHover ? toggleOn : juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, dragHover ? 2.0f : 1.0f);

    auto header = getLocalBounds().reduced (14, 10).removeFromTop (26);
    g.setColour (juce::Colours::white.withAlpha (shownAudible ? 1.0f : 0.35f));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (letter(), header.removeFromLeft (24), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (13.5f));
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    juce::String info = "no file";
    if (auto s = player.getSource())
        info = s->name + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String (s->sampleRate / 1000.0, 1) + " kHz"
             + juce::String::fromUTF8 ("  \xc2\xb7  ") + (s->fileChannels == 1 ? "mono" : s->fileChannels == 2 ? "stereo" : juce::String (s->fileChannels) + " ch (1-2 used)")
             + juce::String::fromUTF8 ("  \xc2\xb7  ") + formatTime (s->seconds());
    g.drawText (info, header.withTrimmedRight (400), juce::Justification::centredLeft, true);

    // Between the two slots, and the way out to the fader.
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    if (! slotArrow.isEmpty())
        g.drawArrow (juce::Line<float> ((float) slotArrow.getX(), (float) slotArrow.getCentreY(),
                                        (float) slotArrow.getRight(), (float) slotArrow.getCentreY()), 1.0f, 6.0f, 6.0f);
    g.setFont (juce::FontOptions (12.5f));
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawText ("to fader " + letter() + juce::String::fromUTF8 (" \xe2\x86\x92"), toFaderArea, juce::Justification::centredRight);
}

void DeckView::resized()
{
    auto r = getLocalBounds().reduced (14, 10);

    auto header = r.removeFromTop (26);
    reverseButton.setBounds (header.removeFromRight (56));
    header.removeFromRight (6);
    loopButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (12);
    soloButton.setBounds (header.removeFromRight (30));
    header.removeFromRight (4);
    muteButton.setBounds (header.removeFromRight (30));
    header.removeFromRight (12);
    playButton.setBounds (header.removeFromRight (84));
    header.removeFromRight (12);
    openButton.setBounds (header.removeFromRight (84));

    auto slots = r.removeFromBottom (26);
    r.removeFromBottom (8);
    trimCaption.setBounds (slots.removeFromLeft (34));
    trimKnob.setBounds (slots.removeFromLeft (32).expanded (0, 4));
    slots.removeFromLeft (2);
    trimValue.setBounds (slots.removeFromLeft (66));
    slots.removeFromLeft (8);
    slot1.setBounds (slots.removeFromLeft (200));
    slotArrow = slots.removeFromLeft (28).reduced (5, 0);
    slot2.setBounds (slots.removeFromLeft (200));
    toFaderArea = slots;

    auto fields = r.removeFromBottom (26);
    r.removeFromBottom (8);
    r.removeFromTop (8);
    strip.setBounds (r);

    inCaption.setBounds (fields.removeFromLeft (26));
    inField.setBounds (fields.removeFromLeft (92));
    fields.removeFromLeft (10);
    outCaption.setBounds (fields.removeFromLeft (36));
    outField.setBounds (fields.removeFromLeft (92));
    fields.removeFromLeft (18);
    fadeCaption.setBounds (fields.removeFromLeft (104));
    fadeField.setBounds (fields.removeFromLeft (60));
    fields.removeFromLeft (8);
    fadeShapeBox.setBounds (fields.removeFromLeft (120));
    fields.removeFromLeft (12);
    tensionCaption.setBounds (fields.removeFromLeft (60));
    tension.setBounds (fields.removeFromLeft (120));
}

//==============================================================================
bool DeckView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (formats.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void DeckView::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    for (auto& f : files)
    {
        const juce::File file (f);
        if (formats.findFormatForFileExtension (file.getFileExtension()) != nullptr)
        {
            loadFile (file);
            break;
        }
    }
    repaint();
}

} // namespace goblet
