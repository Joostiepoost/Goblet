// DeckView.cpp

#include "DeckView.h"
#include "TimeFormat.h"

namespace goblet
{

static const juce::Colour toggleOn (0xff2f6fb8);

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

DeckView::DeckView (int deckIndex, DeckPlayer& p, juce::AudioFormatManager& f)
    : index (deckIndex), player (p), formats (f)
{
    addAndMakeVisible (strip);
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

    for (auto* b : { &openButton, &playButton, &loopButton, &reverseButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonOnColourId, toggleOn);
        addAndMakeVisible (b);
    }

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
    else                    player.play();
    refresh();
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
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (letter(), header.removeFromLeft (24), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (13.5f));
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    juce::String info = "no file";
    if (auto s = player.getSource())
        info = s->name + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String (s->sampleRate / 1000.0, 1) + " kHz"
             + juce::String::fromUTF8 ("  \xc2\xb7  ") + (s->fileChannels == 1 ? "mono" : s->fileChannels == 2 ? "stereo" : juce::String (s->fileChannels) + " ch (1-2 used)")
             + juce::String::fromUTF8 ("  \xc2\xb7  ") + formatTime (s->seconds());
    g.drawText (info, header.withTrimmedRight (330), juce::Justification::centredLeft, true);
}

void DeckView::resized()
{
    auto r = getLocalBounds().reduced (14, 10);

    auto header = r.removeFromTop (26);
    reverseButton.setBounds (header.removeFromRight (56));
    header.removeFromRight (6);
    loopButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (6);
    playButton.setBounds (header.removeFromRight (84));
    header.removeFromRight (12);
    openButton.setBounds (header.removeFromRight (84));

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
