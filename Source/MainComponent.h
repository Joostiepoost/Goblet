// MainComponent.h — everything inside Goblet's main window.
//
// Step 1: one deck (A) with its source strip, fragment, loop, reverse and
// crossfade, the quality presets and the transport. Decks B and C, the mixer
// and the captures are drawn as placeholders where they will go.

#pragma once

#include <JuceHeader.h>
#include "Engine/Mixer.h"
#include "UI/DeckView.h"

class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int activeDecks = 1;      // step 1: deck A only

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void togglePlayAll();
    void showAudioSettings();
    void loadSettings();
    void saveSettings();
    void setStatus (const juce::String&);

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formats;
    juce::ApplicationProperties properties;
    goblet::Mixer mixer;

    std::vector<std::unique_ptr<goblet::DeckView>> decks;

    juce::TextButton playAllButton, stopAllButton, audioButton { "Audio..." };
    juce::ComboBox qualityBox;
    juce::Label qualityCaption { {}, "Quality" }, cpuLabel, statusLabel;
    juce::TooltipWindow tooltips { this, 600 };

    // Where the parts of later steps will go.
    juce::Rectangle<int> placeholderDecks[goblet::Mixer::numDecks], mixerArea, capturesArea;

    bool restoring = false;
    int statusAge = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
