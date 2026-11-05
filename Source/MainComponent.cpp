// MainComponent.cpp — step 6: the re-render.

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
    pluginHost.loadPluginList();

    // Toolbar
    playAllButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6 Play all"));
    playAllButton.setTooltip ("Start every deck that has a file, together, from the start of its fragment (Space)");
    playAllButton.onClick = [this] { if (! audioEnabled) setAudioEnabled (true); mixer.playAll(); };
    stopAllButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xa0"));
    stopAllButton.setTooltip ("Stop every deck (Space while playing)");
    stopAllButton.onClick = [this] { mixer.stopAll(); };
    recordButton.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x8f Rec"));
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc0392b));
    recordButton.setTooltip ("Capture what you hear (R): the sum and a stem per deck, with a snapshot of the session. "
                             "Starts every deck if none is playing. After the last deck stops, the capture runs on until the "
                             "tail has died away; Rec again ends it at once.");
    recordButton.onClick = [this] { toggleRecord(); };
    audioButton.setTooltip ("Audio device, sample rate and buffer size");
    audioButton.onClick = [this] { showAudioSettings(); };
    audioOnButton.setClickingTogglesState (true);
    audioOnButton.setToggleState (true, juce::dontSendNotification);
    audioOnButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f6fb8));
    audioOnButton.setTooltip ("Audio processing on/off (Shift-Cmd-A). Off closes the audio device, so Goblet uses no CPU while it waits "
                              "in the background; the files stay loaded. Play turns it back on.");
    audioOnButton.onClick = [this] { setAudioEnabled (audioOnButton.getToggleState()); };
    pluginsButton.setTooltip ("Scan for AU and VST3 plugins");
    pluginsButton.onClick = [this] { pluginHost.showScanner(); };
    midiButton.setTooltip ("MIDI mappings: which controller drives which fader, trim or plugin parameter (Shift-Cmd-M). Lights up when MIDI arrives.");
    midiButton.onClick = [this] { showMidiWindow(); };
    midiButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f6fb8));

    for (int q = 0; q < numQualities; ++q)
        qualityBox.addItem (juce::String (kernelSpec ((Quality) q).name) + "  (" + juce::String (kernelSpec ((Quality) q).taps) + ")", q + 1);
    qualityBox.setTooltip ("Interpolation quality, used only when a file's rate differs from the device's (taps per output sample). "
                           "At equal rates the samples pass through unchanged.");
    qualityBox.onChange = [this]
    {
        mixer.setQuality ((Quality) juce::jmax (0, qualityBox.getSelectedId() - 1));
        if (! restoring) saveSettings();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &playAllButton, &stopAllButton, &recordButton, &audioButton, &audioOnButton, &pluginsButton, &midiButton, &qualityBox })
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

    addAndMakeVisible (sumMeter);
    addAndMakeVisible (mixerPanel);
    capturesPanel = std::make_unique<CapturesPanel> (mixer.getPreview());
    capturesPanel->showStatus = [this] (const juce::String& t) { setStatus (t); };
    capturesPanel->onRestoreSnapshot = [this] (const juce::File& f) { restoreSnapshot (f); };
    capturesPanel->onRerender = [this] (const juce::File& f) { rerender (f); };
    renderer = std::make_unique<TakeRenderer> (pluginHost.getFormats(), formats);
    renderer->onFinished = [this] (const juce::File& result, const juce::String& error)
    {
        capturesPanel->setRendering ({}, 0.0);
        capturesPanel->refresh();
        if (result != juce::File())
            setStatus ("Re-rendered as " + result.getFileNameWithoutExtension() + (error.isNotEmpty() ? ". Note: " + error : juce::String (".")));
        else
            setStatus (error);
    };
    capturesPanel->setFolder (capturesFolder());
    addAndMakeVisible (*capturesPanel);
    recorder.onFinished = [this] (const juce::File& f) { captureFinished (f); };
    mixerPanel.onFaderMoved = [this] { if (! restoring) saveSettings(); };
    midiMappings.onChanged = [this] { if (! restoring) saveSettings(); };

    // Decks
    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        auto d = std::make_unique<DeckView> (i, mixer, formats, pluginHost);
        d->onChanged = [this] { if (! restoring) saveSettings(); };
        d->onPlayRequested = [this] { if (! audioEnabled) setAudioEnabled (true); };
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
    deviceManager.addMidiInputDeviceCallback ({}, &mixer.getCcTable());
    midiInputChoice = settings->getValue ("midiInput", {});
    applyMidiInputs();

    pluginHost.onStatus = [this] (const juce::String& s) { setStatus (s); };
    pluginHost.onChanged = [this]
    {
        for (auto& d : decks) d->refreshSlots();
        mixerPanel.refreshSlots();
    };

    loadSettings();
    pluginHost.restore();
    if (err.isNotEmpty()) setStatus ("Audio: " + err);

    setWantsKeyboardFocus (true);
    setSize (1280, 860);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    if (recorder.isActive()) recorder.cut();
    renderer = nullptr;
    capturesPanel = nullptr;
    saveSettings();
    pluginHost.closeAllEditors();
    midiWindow = nullptr;
    deviceManager.removeMidiInputDeviceCallback ({}, &mixer.getCcTable());
    deviceManager.removeAudioCallback (&mixer);
    deviceManager.removeChangeListener (this);
}

//==============================================================================
void MainComponent::applyMidiInputs()
{
    const auto devices = juce::MidiInput::getAvailableDevices();
    juce::StringArray ids;
    for (auto& d : devices) ids.add (d.identifier);
    const bool listChanged = ids != knownMidiInputs;
    knownMidiInputs = ids;

    for (auto& d : devices)
    {
        const bool want = midiInputChoice.isEmpty() || d.identifier == midiInputChoice;
        if (deviceManager.isMidiInputDeviceEnabled (d.identifier) != want)
            deviceManager.setMidiInputDeviceEnabled (d.identifier, want);
    }

    if (listChanged && midiWindow != nullptr)
        if (auto* panel = dynamic_cast<MidiPanel*> (midiWindow->getContentComponent()))
            panel->refreshDevices();
}

void MainComponent::showMidiWindow()
{
    if (midiWindow != nullptr)
    {
        midiWindow->toFront (true);
        return;
    }

    auto panel = std::make_unique<MidiPanel> (midiMappings, mixer);
    juce::Component::SafePointer<MainComponent> safeThis (this);
    panel->getDeviceChoice = [safeThis] { return safeThis != nullptr ? safeThis->midiInputChoice : juce::String(); };
    panel->onDeviceChosen = [safeThis] (const juce::String& choice)
    {
        if (safeThis == nullptr) return;
        safeThis->midiInputChoice = choice;
        safeThis->properties.getUserSettings()->setValue ("midiInput", choice);
        safeThis->applyMidiInputs();
    };
    panel->refreshDevices();

    midiWindow = std::make_unique<MidiWindow> (std::move (panel), [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->midiWindow = nullptr; });
    });
}

//==============================================================================
void MainComponent::togglePlayAll()
{
    if (! audioEnabled) { setAudioEnabled (true); mixer.playAll(); return; }
    if (mixer.anyPlaying()) mixer.stopAll();
    else                    mixer.playAll();
}

void MainComponent::setAudioEnabled (bool shouldBeOn)
{
    if (shouldBeOn == audioEnabled) return;
    audioEnabled = shouldBeOn;

    if (! shouldBeOn)
    {
        deviceManager.closeAudioDevice();
        for (int i = 0; i < Mixer::numDecks; ++i)
            mixer.deck (i).audioStopped();
        cpuLabel.setText ("Audio off", juce::dontSendNotification);
        setStatus ("Audio off: no audio processing, no CPU. The files stay loaded; Play or Shift-Cmd-A turns it back on.");
    }
    else
    {
        deviceManager.restartLastAudioDevice();
        setStatus ("Audio on.");
    }

    audioOnButton.setToggleState (audioEnabled, juce::dontSendNotification);
    audioOnButton.setButtonText (audioEnabled ? "Audio on" : "Audio off");
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlayAll();
        return true;
    }

    const auto mods = key.getModifiers();

    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        setAudioEnabled (! audioEnabled);
        return true;
    }

    if (key.getKeyCode() == 'R' || key.getKeyCode() == 'r')
    {
        if (! mods.isCommandDown() && ! mods.isCtrlDown() && ! mods.isAltDown() && ! mods.isShiftDown())
        {
            toggleRecord();
            return true;
        }
    }

    if (key == juce::KeyPress ('m', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        showMidiWindow();
        return true;
    }

    // A, B, C: play/stop a deck; Shift: mute; Option: solo. Letters rather than
    // numbers, so they work without modifiers on an AZERTY keyboard too.
    const int code = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    if (code >= 'A' && code < 'A' + Mixer::numDecks && ! mods.isCommandDown() && ! mods.isCtrlDown())
    {
        auto& d = *decks[(size_t) (code - 'A')];
        if (mods.isShiftDown())     d.toggleMute();
        else if (mods.isAltDown())  d.toggleSolo();
        else                        d.togglePlay();
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
    if (! audioEnabled) return;    // closing the device is not a new device choice

    if (auto xml = deviceManager.createStateXml())
    {
        properties.getUserSettings()->setValue ("audioState", xml.get());
        properties.saveIfNeeded();
    }
}

//==============================================================================
void MainComponent::loadSettings()
{
    readSession (*properties.getUserSettings(), false);
}

void MainComponent::readSession (const juce::PropertySet& s, bool replacePlugins)
{
    const juce::ScopedValueSetter<bool> svs (restoring, true);

    qualityBox.setSelectedId (juce::jlimit (0, numQualities - 1, s.getIntValue ("quality", (int) Quality::Max)) + 1, juce::dontSendNotification);
    mixer.setQuality ((Quality) (qualityBox.getSelectedId() - 1));

    for (auto& d : decks)
        d->readState (s);

    auto xml = s.getXmlValue ("midiMappings");
    midiMappings.restore (xml.get());

    if (replacePlugins)
        pluginHost.restoreFrom (s, true);
}

void MainComponent::writeSession (juce::PropertySet& s)
{
    s.setValue ("quality", qualityBox.getSelectedId() - 1);
    for (auto& d : decks)
        d->writeState (s);
    if (auto xml = midiMappings.createXml())
        s.setValue ("midiMappings", xml.get());
    if (! pluginHost.isLoading())
        pluginHost.writeTo (s);
}

void MainComponent::saveSettings()
{
    writeSession (*properties.getUserSettings());
    properties.saveIfNeeded();
}

//==============================================================================
juce::File MainComponent::capturesFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Goblet Captures");
}

void MainComponent::toggleRecord()
{
    if (recorder.isActive()) { recorder.cut(); return; }
    if (! audioEnabled) setAudioEnabled (true);

    // A new take: "Goblet 2026-11-05 143012.wav", its stems and its snapshot.
    const auto folder = capturesFolder();
    folder.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S");
    auto file = folder.getChildFile ("Goblet " + stamp + ".wav");
    for (int n = 2; file.exists(); ++n)
        file = folder.getChildFile ("Goblet " + stamp + " " + juce::String (n) + ".wav");

    std::array<bool, Mixer::numDecks> stems {};
    for (int i = 0; i < Mixer::numDecks; ++i)
        stems[(size_t) i] = mixer.deck (i).getSource() != nullptr;

    // The snapshot: the whole session as it is now.
    {
        juce::PropertySet snapshot;
        writeSession (snapshot);
        if (pluginHost.isLoading()) pluginHost.writeTo (snapshot);
        if (auto xml = snapshot.createXml ("GOBLET_SNAPSHOT"))
            xml->writeTo (CapturesPanel::snapshotFor (file));
    }

    juce::String error;
    if (! recorder.start (file, stems, 30.0, error))
    {
        CapturesPanel::snapshotFor (file).deleteFile();
        setStatus (error);
        return;
    }

    if (! mixer.anyPlaying())
        mixer.playAll();
    recordButton.setToggleState (true, juce::dontSendNotification);
    setStatus ("Capturing " + file.getFileNameWithoutExtension() + ". When the last deck stops, the tail is captured too; R ends it at once.");
}

void MainComponent::rerender (const juce::File& take)
{
    if (renderer->isBusy()) { renderer->cancel(); return; }

    juce::String error;
    if (! renderer->start (take, Quality::Render, error)) { setStatus (error); return; }
    capturesPanel->setRendering (take.getFileNameWithoutExtension(), 0.0);
    setStatus ("Re-rendering " + take.getFileNameWithoutExtension() + " at Render quality. The live mix keeps running.");
}

void MainComponent::captureFinished (const juce::File& file)
{
    recordButton.setToggleState (false, juce::dontSendNotification);
    recordButton.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x8f Rec"));
    capturesPanel->setRecording ({}, 0.0, false);
    capturesPanel->refresh();
    setStatus ("Captured " + file.getFileNameWithoutExtension() + ".");
}

void MainComponent::restoreSnapshot (const juce::File& snapshot)
{
    auto xml = juce::XmlDocument::parse (snapshot);
    if (xml == nullptr) { setStatus ("Cannot read " + snapshot.getFileName()); return; }

    juce::Component::SafePointer<MainComponent> safeThis (this);
    auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                            .withTitle ("Restore snapshot")
                                            .withMessage ("Go back to the session that produced " + snapshot.getFileNameWithoutExtension()
                                                          + "? The current files, fragments, plugins, trims, faders and MIDI mappings are replaced.")
                                            .withButton ("Restore")
                                            .withButton ("Cancel");
    juce::AlertWindow::showAsync (options, [safeThis, snapshot] (int result)
    {
        if (safeThis == nullptr || result != 1) return;
        auto doc = juce::XmlDocument::parse (snapshot);
        if (doc == nullptr) return;
        juce::PropertySet set;
        set.restoreFromXml (*doc);
        safeThis->mixer.stopAll();
        safeThis->readSession (set, true);
        safeThis->saveSettings();
        safeThis->setStatus ("Restored the session of " + snapshot.getFileNameWithoutExtension() + ".");
    });
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
    mixerPanel.refresh();
    sumMeter.refresh();

    if (renderer != nullptr && renderer->isBusy())
        capturesPanel->setRendering (renderer->getTake().getFileNameWithoutExtension(), renderer->getProgress());

    if (recorder.isActive())
    {
        const double t = recorder.getSeconds();
        recordButton.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x8f ") + formatTime (t).dropLastCharacters (2));
        capturesPanel->setRecording (recorder.getFile(), t, recorder.isInTail());
    }

    // The MIDI button lights briefly when MIDI arrives; new MIDI devices are picked up.
    const auto count = mixer.getCcTable().messageCount();
    if (count != lastMidiCount) { lastMidiCount = count; midiFlash = 4; }
    if (midiFlash > 0 && --midiFlash == 0) midiButton.setToggleState (false, juce::dontSendNotification);
    else if (midiFlash > 0) midiButton.setToggleState (true, juce::dontSendNotification);

    // CPU twice a second.
    static int tick = 0;
    if (++tick % 15 == 0 && audioEnabled)
        cpuLabel.setText ("CPU " + juce::String (deviceManager.getCpuUsage() * 100.0, 1) + "%", juce::dontSendNotification);

    // After a message has been shown for 6 s, the status line goes back to what the engine does.
    if (++statusAge > 180 && tick % 15 == 0 && audioEnabled)
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
        const int latency = mixer.getTotalLatency();
        if (latency > 0)
        {
            s << juce::String::fromUTF8 ("   \xc2\xb7   ") << "Plugin latency " << juce::String (1000.0 * latency / deviceRate, 1) << " ms";
            if (mixer.getAlignedLatency() > 0)
            {
                s << " (decks aligned:";
                for (int i = 0; i < Mixer::numDecks; ++i)
                    s << " " << decks[(size_t) i]->letter() << " +" << juce::String (1000.0 * (mixer.getAlignedLatency() - mixer.getDeckLatency (i)) / deviceRate, 1);
                s << " ms)";
            }
        }
        statusLabel.setText (s, juce::dontSendNotification);
    }

    // Plugin latencies can change at any time (some plugins change theirs with a setting).
    if (tick % 30 == 0)
    {
        applyMidiInputs();
        for (auto& d : decks) d->refreshSlots();
        mixerPanel.refreshSlots();
    }
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (48, 0.0f, (float) getWidth());


}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    auto bar = r.removeFromTop (48).reduced (12, 9);
    playAllButton.setBounds (bar.removeFromLeft (96));
    bar.removeFromLeft (6);
    stopAllButton.setBounds (bar.removeFromLeft (40));
    bar.removeFromLeft (6);
    recordButton.setBounds (bar.removeFromLeft (96));
    bar.removeFromLeft (18);
    qualityCaption.setBounds (bar.removeFromLeft (54));
    bar.removeFromLeft (4);
    qualityBox.setBounds (bar.removeFromLeft (132));
    bar.removeFromLeft (12);
    audioButton.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (6);
    audioOnButton.setBounds (bar.removeFromLeft (84));
    bar.removeFromLeft (6);
    pluginsButton.setBounds (bar.removeFromLeft (84));
    bar.removeFromLeft (6);
    midiButton.setBounds (bar.removeFromLeft (70));
    cpuLabel.setBounds (bar.removeFromRight (100));
    bar.removeFromRight (16);
    sumMeter.setBounds (bar.removeFromRight (200));

    statusLabel.setBounds (r.removeFromBottom (26).reduced (12, 2));

    r.reduce (12, 10);
    capturesPanel->setBounds (r.removeFromBottom (150));
    r.removeFromBottom (12);
    mixerPanel.setBounds (r.removeFromRight (280));
    r.removeFromRight (12);

    const int gap = 12;
    const int h = (r.getHeight() - 2 * gap) / Mixer::numDecks;
    for (int i = 0; i < Mixer::numDecks; ++i)
    {
        decks[(size_t) i]->setBounds (r.removeFromTop (h));
        r.removeFromTop (gap);
    }
}
