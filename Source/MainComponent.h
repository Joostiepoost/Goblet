// MainComponent.h — everything inside Goblet's main window.
//
// Step 6: three decks (source strip, fragment, loop, reverse, crossfade,
// Play, Mute, Solo, trim, two slots before the fader), the mixer column
// (faders, meters, two slots after each fader, the sum's two slots), the sum
// meter and clip lamp, MIDI mappings, live capture (sum and stems, with a
// snapshot of the session and its gesture), the re-render, the captures
// panel, the quality presets and the audio on/off switch.

#pragma once

#include <JuceHeader.h>
#include "Engine/Mixer.h"
#include "UI/DeckView.h"
#include "UI/Meters.h"
#include "UI/PluginHost.h"
#include "UI/MidiMappings.h"
#include "UI/MidiPanel.h"
#include "UI/CapturesPanel.h"
#include "Engine/CaptureRecorder.h"
#include "UI/TakeRenderer.h"

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // The File menu (step 7: projects).
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int) override;

    /** Asks to save unsaved changes (Save / Don't save / Cancel), then runs `next` unless cancelled. */
    void askToSaveThen (std::function<void()> next);


private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void togglePlayAll();
    void setAudioEnabled (bool);
    void showAudioSettings();
    void loadSettings();
    void saveSettings();
    // The session: the same keys go to the settings (kept between launches) and to a take's snapshot.
    void writeSession (juce::PropertySet&, bool withPluginStates = true);
    void readSession (const juce::PropertySet&, bool replacePlugins);
    void toggleRecord();
    void captureFinished (const juce::File&);
    void restoreSnapshot (const juce::File&);
    juce::File capturesFolder() const;

    // Projects (step 7): a starting setup saved anywhere, in the same format as a take's snapshot.
    void newProject();
    void openProjectDialog();
    void openProject (const juce::File&);
    void saveProject (std::function<void (bool saved)> done = {});
    void saveProjectAs (std::function<void (bool saved)> done = {});
    bool writeProjectFile (const juce::File&, juce::String& error);
    bool readProjectFile (const juce::File&, juce::String& error);
    void setProjectFile (const juce::File&);
    juce::String projectName() const;
    juce::String sessionHash();
    void markSaved();
    void updateTitle();

    juce::File projectFile;
    juce::String savedHash, currentHash, lastTitle;
    bool hashPending = false;      // the saved state is taken once the plugins have loaded
    juce::RecentlyOpenedFilesList recentProjects;
    std::unique_ptr<juce::FileChooser> projectChooser;
    bool isDirty() const           { return currentHash != savedHash; }
    enum MenuIDs { menuNew = 1, menuOpen, menuSave, menuSaveAs, menuRecentBase = 100 };
    void setStatus (const juce::String&);
    void showMidiWindow();
    void applyMidiInputs();

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formats;
    juce::ApplicationProperties properties;
    goblet::Mixer mixer;

    std::vector<std::unique_ptr<goblet::DeckView>> decks;

    goblet::PluginHost pluginHost { mixer, properties };
    goblet::MidiMappings midiMappings { mixer };
    std::unique_ptr<goblet::MidiWindow> midiWindow;
    juce::String midiInputChoice;            // "" = all inputs, "none", or a device identifier
    juce::StringArray knownMidiInputs;
    uint32_t lastMidiCount = 0;
    int midiFlash = 0;
    goblet::MixerPanel mixerPanel { mixer, pluginHost };
    goblet::SumMeter sumMeter { mixer };

    goblet::CaptureRecorder recorder { mixer };
    std::unique_ptr<goblet::CapturesPanel> capturesPanel;
    std::unique_ptr<goblet::TakeRenderer> renderer;
    void rerender (const juce::File& take);

    juce::TextButton playAllButton, stopAllButton, recordButton, audioButton { "Audio..." }, audioOnButton { "Audio on" }, pluginsButton { "Plugins..." }, midiButton { "MIDI..." };
    juce::ComboBox qualityBox;
    juce::Label qualityCaption { {}, "Quality" }, cpuLabel, statusLabel;
    juce::TooltipWindow tooltips { this, 600 };

    // Where the parts of later steps will go.

    bool restoring = false;
    bool audioEnabled = true;
    int statusAge = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
