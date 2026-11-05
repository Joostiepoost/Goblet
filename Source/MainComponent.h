// MainComponent.h — everything inside Goblet's main window.
//
// Step 2: three decks (source strip, fragment, loop, reverse, crossfade,
// Play, Mute, Solo), the mixer column with a meter per deck, the sum meter
// and clip lamp, the quality presets and the audio on/off switch. The
// captures panel is drawn as a placeholder where it will go.

#pragma once

#include <JuceHeader.h>
#include "Engine/Mixer.h"
#include "UI/DeckView.h"
#include "UI/Meters.h"

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


private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void togglePlayAll();
    void setAudioEnabled (bool);
    void showAudioSettings();
    void loadSettings();
    void saveSettings();
    void setStatus (const juce::String&);

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formats;
    juce::ApplicationProperties properties;
    goblet::Mixer mixer;

    std::vector<std::unique_ptr<goblet::DeckView>> decks;

    goblet::MixerPanel mixerPanel { mixer };
    goblet::SumMeter sumMeter { mixer };

    juce::TextButton playAllButton, stopAllButton, audioButton { "Audio..." }, audioOnButton { "Audio on" };
    juce::ComboBox qualityBox;
    juce::Label qualityCaption { {}, "Quality" }, cpuLabel, statusLabel;
    juce::TooltipWindow tooltips { this, 600 };

    // Where the parts of later steps will go.
    juce::Rectangle<int> capturesArea;

    bool restoring = false;
    bool audioEnabled = true;
    int statusAge = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
