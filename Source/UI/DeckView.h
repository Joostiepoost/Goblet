// DeckView.h — one deck on screen: header, source strip, fragment fields.
//
//   A  file name · rate · length   [Open...] [Play] [M] [S] [Loop] [Rev]
//   [ waveform with in and out flags, playhead                          ]
//   In [..]  Out [..]  Crossfade [..] ms  [shape]  Tension [----]
//   Trim (o) +3.0 dB  [1 · plugin] -> [2 · plugin]       to fader A ->
//
// It talks to its DeckPlayer and to the Mixer (mute, solo) directly;
// MainComponent only saves and restores its state and forwards shortcuts.

#pragma once

#include <JuceHeader.h>
#include "../Engine/Mixer.h"
#include "SourceStrip.h"
#include "PluginHost.h"

namespace goblet
{

class DeckView : public juce::Component,
                 public juce::FileDragAndDropTarget
{
public:
    DeckView (int index, Mixer& mixer, juce::AudioFormatManager& formats, PluginHost& host);
    ~DeckView() override;

    void loadFile (const juce::File& file);
    void openFileDialog();
    void togglePlay();
    void toggleMute();
    void toggleSolo();

    // Settings, under keys "deckA.file", "deckA.in", ... (A, B or C).
    void writeState (juce::PropertySet&) const;
    void readState (const juce::PropertySet&);

    void refresh();            // called by MainComponent's timer: playhead and Play button
    void refreshSlots();       // a plugin was loaded, removed or bypassed

    std::function<void()> onChanged;                       // something worth saving changed
    std::function<void()> onPlayRequested;                 // before a Play: turns audio on if it was off
    std::function<void (const juce::String&)> onStatus;    // a line for the status bar

    juce::String letter() const          { return juce::String::charToString ((juce::juce_wchar) ('A' + index)); }

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override    { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override               { dragHover = false; repaint(); }

private:
    void setFragment (int64_t in, int64_t out);
    void updateFields();
    void updateTrimText();
    void applyCrossfade();
    void changed()                       { if (onChanged) onChanged(); }
    void status (const juce::String& s)  { if (onStatus) onStatus (s); }
    juce::String key (const char* name) const   { return "deck" + letter() + "." + name; }

    const int index;
    Mixer& mixer;
    DeckPlayer& player;
    juce::AudioFormatManager& formats;

    SourceStrip strip;
    SlotButton slot1, slot2;
    juce::Label trimCaption { {}, "Trim" }, trimValue;
    struct TrimKnobLook;
    std::unique_ptr<TrimKnobLook> trimLook;     // before the knob, so it outlives it
    juce::Slider trimKnob;
    juce::Rectangle<int> slotArrow, toFaderArea;
    juce::TextButton openButton { "Open..." }, playButton, muteButton { "M" }, soloButton { "S" }, loopButton { "Loop" }, reverseButton { "Rev" };
    juce::Label inCaption { {}, "In" }, inField, outCaption { {}, "Out" }, outField;
    juce::Label fadeCaption { {}, "Crossfade (ms)" }, fadeField;
    juce::ComboBox fadeShapeBox;
    juce::Label tensionCaption { {}, "Tension" };
    juce::Slider tension;

    std::unique_ptr<juce::FileChooser> chooser;
    bool dragHover = false;
    bool shownPlaying = false, shownAudible = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckView)
};

} // namespace goblet
