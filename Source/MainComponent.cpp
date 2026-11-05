// MainComponent.cpp — step 1: one deck, transport, quality presets.

#include "MainComponent.h"
#include "UI/TimeFormat.h"

using namespace goblet;

static const juce::Colour background (0xff1b1e21);

MainComponent::MainComponent()
{
    formats.registerBasicFormats();

    juce::PropertiesFile::Options opts;
    opts.applicationName = "Goblet";
    opts.filenameSuffix = "settings";
    opts.folderName = "Goblet";
    opts.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters (opts);

    // Toolbar
    playAllButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6 Play all"));
    playAllButton.setTooltip ("Start every deck that has a file, together, from the start of its fragment (Space)");
    playAllButton.onClick = [this] { mixer.playAll(); };
    stopAllButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xa0"));
    stopAllButton.setTooltip ("Stop every deck (Space while playing)");
    stopAllButton.onClick = [this] { mixer.stopAll(); };
    audioButton.setTooltip ("Audio device, sample rate and buffer size");
    audioButton.onClick = [this] { showAudioSettings(); };

    for (int q = 0; q < numQualities; ++q)
        qualityBox.addItem (juce::String (kernelSpec ((Quality) q).name) + "  (" + juce::String (kernelSpec ((Quality) q).taps) + ")", q + 1);
    qualityBox.setTooltip ("Interpolation quality, used only when a file's rate differs from the device's (taps per output sample). "
                           "At equal rates the samples pass through unchanged.");
    qualityBox.onChange = [this]
    {
        mixer.setQuality ((Quality) juce::jmax (0, qualityBox.getSelectedId() - 1));
        if (! restoring) saveSettings();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &playAllButton, &stopAllButton, &audioButton, &qualityBox })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }
    qualityCaption.setJustificationType (juce::Justification::centredRight);
    qualityCaption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (qualityCaption);

    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    addAndMakeVisible (cpuLabel);

    statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    statusLabel.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (statusLabel);

    // Decks
    for (int i = 0; i < activeDecks; ++i)
    {
        auto d = std::make_unique<DeckView> (i, mixer.deck (i), formats);
        d->onChanged = [this] { if (! restoring) saveSettings(); };
        d->onStatus = [this] (const juce::String& s) { setStatus (s); };
        addAndMakeVisible (*d);
        decks.push_back (std::move (d));
    }

    // Audio: 2 outputs, no inputs. The device and rate are remembered.
    auto* settings = properties.getUserSettings();
    std::unique_ptr<juce::XmlElement> audioState (settings->getXmlValue ("audioState"));
    const auto err = deviceManager.initialise (0, 2, audioState.get(), true);
    deviceManager.addAudioCallback (&mixer);
    deviceManager.addChangeListener (this);

    loadSettings();
    if (err.isNotEmpty()) setStatus ("Audio: " + err);

    setWantsKeyboardFocus (true);
    setSize (1280, 860);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    saveSettings();
    deviceManager.removeAudioCallback (&mixer);
    deviceManager.removeChangeListener (this);
}

//==============================================================================
void MainComponent::togglePlayAll()
{
    if (mixer.anyPlaying()) mixer.stopAll();
    else                    mixer.playAll();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlayAll();
        return true;
    }

    const auto c = key.getTextCharacter();
    if (c >= '1' && c <= '3' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        const int i = (int) (c - '1');
        if (i < (int) decks.size()) decks[(size_t) i]->togglePlay();
        else setStatus ("Deck " + juce::String::charToString ((juce::juce_wchar) ('A' + i)) + " arrives in step 2.");
        return true;
    }

    return false;
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 0, 2, 2, false, false, true, false);
    selector->setSize (520, 380);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio settings";
    o.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = deviceManager.createStateXml())
    {
        properties.getUserSettings()->setValue ("audioState", xml.get());
        properties.saveIfNeeded();
    }
}

//==============================================================================
void MainComponent::loadSettings()
{
    const juce::ScopedValueSetter<bool> svs (restoring, true);
    auto& s = *properties.getUserSettings();

    qualityBox.setSelectedId (juce::jlimit (0, numQualities - 1, s.getIntValue ("quality", (int) Quality::Max)) + 1, juce::dontSendNotification);
    mixer.setQuality ((Quality) (qualityBox.getSelectedId() - 1));

    for (auto& d : decks)
        d->readState (s);
}

void MainComponent::saveSettings()
{
    auto& s = *properties.getUserSettings();
    s.setValue ("quality", qualityBox.getSelectedId() - 1);
    for (auto& d : decks)
        d->writeState (s);
    properties.saveIfNeeded();
}

void MainComponent::setStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusAge = 0;
}

//==============================================================================
void MainComponent::timerCallback()
{
    for (auto& d : decks)
        d->refresh();

    // CPU twice a second.
    static int tick = 0;
    if (++tick % 15 == 0)
        cpuLabel.setText ("CPU " + juce::String (deviceManager.getCpuUsage() * 100.0, 1) + "%", juce::dontSendNotification);

    // After a message has been shown for 6 s, the status line goes back to what the engine does.
    if (++statusAge > 180 && tick % 15 == 0)
    {
        const double deviceRate = mixer.getOutputRate();
        juce::String s;
        s << "Device " << juce::String (deviceRate / 1000.0, 1) << " kHz";
        const auto& k = KernelBank::get (mixer.getQuality());

        for (int i = 0; i < (int) decks.size(); ++i)
        {
            if (auto src = mixer.deck (i).getSource())
            {
                const double r = src->sampleRate / deviceRate;
                s << juce::String::fromUTF8 ("   \xc2\xb7   ") << "Deck " << decks[(size_t) i]->letter() << ": file "
                  << juce::String (src->sampleRate / 1000.0, 1) << " kHz, ";
                if (juce::exactlyEqual (r, 1.0)) s << "same rate, samples passed through unchanged";
                else s << "read ratio " << juce::String (r, 4) << ", " << k.spec().name << " sinc, "
                       << (int) std::ceil ((double) k.spec().taps * juce::jmax (1.0, r)) << " taps";
            }
        }
        statusLabel.setText (s, juce::dontSendNotification);
    }
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (48, 0.0f, (float) getWidth());

    auto placeholder = [&g] (juce::Rectangle<int> r, const juce::String& title, const juce::String& note)
    {
        if (r.isEmpty()) return;
        const float dashes[] = { 4.0f, 4.0f };
        auto rf = r.toFloat().reduced (0.5f);
        juce::Path p;
        p.addRoundedRectangle (rf, 8.0f);
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillPath (dashed);

        auto t = r.reduced (16, 12);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (title, t.removeFromTop (22), juce::Justification::topLeft);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (note, t.removeFromTop (18), juce::Justification::topLeft);
    };

    for (int i = activeDecks; i < Mixer::numDecks; ++i)
        placeholder (placeholderDecks[i], juce::String::charToString ((juce::juce_wchar) ('A' + i)), "Deck " + juce::String::charToString ((juce::juce_wchar) ('A' + i)) + ": step 2");
    placeholder (mixerArea, "Mixer", "Faders, meters and plugin slots: steps 2-4");
    placeholder (capturesArea, "Captures", "Live capture of the sum and stems: step 5");
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    auto bar = r.removeFromTop (48).reduced (12, 9);
    playAllButton.setBounds (bar.removeFromLeft (96));
    bar.removeFromLeft (6);
    stopAllButton.setBounds (bar.removeFromLeft (40));
    bar.removeFromLeft (24);
    qualityCaption.setBounds (bar.removeFromLeft (60));
    bar.removeFromLeft (4);
    qualityBox.setBounds (bar.removeFromLeft (150));
    bar.removeFromLeft (12);
    audioButton.setBounds (bar.removeFromLeft (80));
    cpuLabel.setBounds (bar.removeFromRight (110));

    statusLabel.setBounds (r.removeFromBottom (26).reduced (12, 2));

    r.reduce (12, 10);
    capturesArea = r.removeFromBottom (130);
    r.removeFromBottom (12);
    mixerArea = r.removeFromRight (280);
    r.removeFromRight (12);

    const int gap = 12;
    const int h = (r.getHeight() - 2 * gap) / Mixer::numDecks;
    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        placeholderDecks[i] = r.removeFromTop (h);
        r.removeFromTop (gap);
        if (i < (int) decks.size())
            decks[(size_t) i]->setBounds (placeholderDecks[i]);
    }
}
